#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

#include "compat/a32_android_library_search.h"
#include "elf/elf32_dependency_resolver.h"

namespace {

using liba32android::compat::A32AndroidLibrarySearchOptions;
using liba32android::compat::A32AndroidLibrarySearchProvider;
using liba32android::compat::A32AndroidLibrarySearchRoot;
using liba32android::compat::A32AndroidLibrarySource;
using liba32android::compat::A32AndroidLibrarySourceError;
using liba32android::compat::A32AndroidLibrarySourceResult;
using liba32android::compat::A32FilesystemLibrarySource;
using liba32android::compat::A32FilesystemLibrarySourceOptions;
using liba32android::elf::Elf32DependencyProviderError;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

struct SourceEntry {
    std::string_view path;
    std::string_view identity;
    std::span<const std::uint8_t> image;
    A32AndroidLibrarySourceError error{A32AndroidLibrarySourceError::None};
};

class RecordingSource final : public A32AndroidLibrarySource {
public:
    explicit RecordingSource(std::span<const SourceEntry> entries)
        : entries_(entries) {}

    std::vector<std::string> calls;
    std::vector<std::uint64_t> ceilings;

    A32AndroidLibrarySourceResult load(
        std::string_view path,
        std::uint64_t max_image_bytes) override {
        calls.emplace_back(path);
        ceilings.push_back(max_image_bytes);
        for (const SourceEntry& entry : entries_) {
            if (entry.path != path) {
                continue;
            }
            A32AndroidLibrarySourceResult result;
            result.error = entry.error;
            if (entry.error != A32AndroidLibrarySourceError::None) {
                return result;
            }
            result.identity.assign(
                entry.identity.data(), entry.identity.size());
            result.image.assign(entry.image.begin(), entry.image.end());
            return result;
        }
        A32AndroidLibrarySourceResult result;
        result.error = A32AndroidLibrarySourceError::NotFound;
        return result;
    }

private:
    std::span<const SourceEntry> entries_;
};

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::error_code error;
        const auto base = std::filesystem::temp_directory_path(error);
        if (error) {
            return;
        }
        path_ = base /
            ("liba32android-fs-source-" +
             std::to_string(static_cast<long long>(::getpid())));
        std::filesystem::remove_all(path_, error);
        error.clear();
        std::filesystem::create_directories(path_, error);
        ready_ = !error;
    }

    ~TemporaryDirectory() {
        if (!path_.empty()) {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }
    }

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
    bool ready_{};
};

int test_filesystem_source_exact_read_and_provider_composition() {
    TemporaryDirectory temporary;
    if (!temporary.ready()) {
        return fail("could not create temporary filesystem-source directory");
    }

    constexpr std::array<std::uint8_t, 4> image{0x7f, 'E', 'L', 'F'};
    const auto file_path = temporary.path() / "libfixture.so";
    {
        std::ofstream output{
            file_path, std::ios::binary | std::ios::trunc};
        output.write(
            reinterpret_cast<const char*>(image.data()),
            static_cast<std::streamsize>(image.size()));
        if (!output) {
            return fail("could not write filesystem-source fixture");
        }
    }

    const std::string file = file_path.string();
    const std::string root = temporary.path().string();
    A32FilesystemLibrarySource source{
        A32FilesystemLibrarySourceOptions{.max_path_bytes = 4096U}};

    const auto direct = source.load(file, image.size());
    if (!direct ||
        direct.identity != file ||
        direct.image !=
            std::vector<std::uint8_t>(image.begin(), image.end())) {
        return fail("filesystem source did not read exact regular-file bytes");
    }

    const auto missing =
        source.load((temporary.path() / "missing.so").string(), image.size());
    if (missing.error != A32AndroidLibrarySourceError::NotFound) {
        return fail("missing filesystem library was not NotFound");
    }

    const auto oversize = source.load(file, image.size() - 1U);
    if (oversize.error != A32AndroidLibrarySourceError::Failed) {
        return fail("filesystem source ignored image byte ceiling");
    }

    const auto directory = source.load(root, 4096U);
    if (directory.error != A32AndroidLibrarySourceError::Failed) {
        return fail("filesystem source accepted a directory as an ELF image");
    }

    A32FilesystemLibrarySource short_path_source{
        A32FilesystemLibrarySourceOptions{.max_path_bytes = 1U}};
    if (short_path_source.load(file, image.size()).error !=
        A32AndroidLibrarySourceError::Failed) {
        return fail("filesystem source ignored path byte ceiling");
    }

    std::string nul_path = file;
    nul_path.push_back('\0');
    nul_path.append("suffix");
    if (source.load(
            std::string_view{nul_path.data(), nul_path.size()},
            image.size()).error != A32AndroidLibrarySourceError::Failed) {
        return fail("filesystem source accepted embedded NUL path");
    }

    const std::array<A32AndroidLibrarySearchRoot, 1> roots{{
        {"root.so", root},
    }};
    A32AndroidLibrarySearchProvider provider{
        std::span{roots},
        source,
        A32AndroidLibrarySearchOptions{.max_path_bytes = 4096U},
    };
    const auto resolved =
        provider.resolve_for("root.so", "libfixture.so", image.size());
    if (!resolved ||
        resolved.source.identity != file ||
        resolved.source.image !=
            std::vector<std::uint8_t>(image.begin(), image.end())) {
        return fail("filesystem source did not compose with requester search");
    }

    return 0;
}

int test_apk_root_exact_success() {
    constexpr std::array<std::uint8_t, 4> image{
        0x7f, 'E', 'L', 'F',
    };
    const std::array<SourceEntry, 1> entries{{
        {
            "base.apk!/lib/armeabi-v7a/libvlc.so",
            "apk:base!/lib/armeabi-v7a/libvlc.so",
            std::span{image},
        },
    }};
    RecordingSource source{std::span{entries}};
    const std::array<A32AndroidLibrarySearchRoot, 1> roots{{
        {"libvlcjni.so", "base.apk!/lib/armeabi-v7a"},
    }};
    A32AndroidLibrarySearchProvider provider{
        std::span{roots}, source, A32AndroidLibrarySearchOptions{128U}};

    const auto result =
        provider.resolve_for("libvlcjni.so", "libvlc.so", image.size());
    if (!result ||
        result.source.identity !=
            "apk:base!/lib/armeabi-v7a/libvlc.so" ||
        result.source.image !=
            std::vector<std::uint8_t>(image.begin(), image.end()) ||
        source.calls != std::vector<std::string>{
            "base.apk!/lib/armeabi-v7a/libvlc.so"} ||
        source.ceilings != std::vector<std::uint64_t>{image.size()} ||
        provider.root_count() != 1U) {
        return fail("APK-root exact library search failed");
    }
    return 0;
}

int test_ordered_root_fallback() {
    constexpr std::array<std::uint8_t, 3> image{1, 2, 3};
    const std::array<SourceEntry, 1> entries{{
        {
            "/data/app/pkg/lib/arm/libc++_shared.so",
            "fs:/data/app/pkg/lib/arm/libc++_shared.so",
            std::span{image},
        },
    }};
    RecordingSource source{std::span{entries}};
    const std::array<A32AndroidLibrarySearchRoot, 2> roots{{
        {"libmla.so", "base.apk!/lib/armeabi-v7a/"},
        {"libmla.so", "/data/app/pkg/lib/arm"},
    }};
    A32AndroidLibrarySearchProvider provider{
        std::span{roots}, source, A32AndroidLibrarySearchOptions{128U}};

    const auto result = provider.resolve_for(
        "libmla.so", "libc++_shared.so", image.size());
    const std::vector<std::string> expected_calls{
        "base.apk!/lib/armeabi-v7a/libc++_shared.so",
        "/data/app/pkg/lib/arm/libc++_shared.so",
    };
    if (!result ||
        result.source.identity !=
            "fs:/data/app/pkg/lib/arm/libc++_shared.so" ||
        source.calls != expected_calls) {
        return fail("ordered Android search roots did not fall through");
    }
    return 0;
}

int test_invalid_requests_do_not_probe_source() {
    constexpr std::array<std::uint8_t, 1> image{1};
    const std::array<SourceEntry, 0> entries{};
    RecordingSource source{std::span{entries}};
    const std::array<A32AndroidLibrarySearchRoot, 1> roots{{
        {"requester", "base.apk!/lib/armeabi-v7a"},
    }};
    A32AndroidLibrarySearchProvider provider{
        std::span{roots}, source, A32AndroidLibrarySearchOptions{128U}};

    const char nul_name_bytes[] = {
        'l','i','b','x','\0','.','s','o',
    };
    const std::array<std::string_view, 5> names{{
        "",
        "../libx.so",
        "dir/libx.so",
        "dir\\libx.so",
        std::string_view{nul_name_bytes, sizeof(nul_name_bytes)},
    }};
    for (const std::string_view name : names) {
        const auto result =
            provider.resolve_for("requester", name, image.size());
        if (result.error != Elf32DependencyProviderError::NotFound) {
            return fail("unsupported explicit/path library request was not NotFound");
        }
    }
    if (!source.calls.empty()) {
        return fail("invalid library request unexpectedly probed source");
    }
    return 0;
}

int test_hard_failure_and_resource_validation() {
    constexpr std::array<std::uint8_t, 4> image{1, 2, 3, 4};
    const std::array<SourceEntry, 3> entries{{
        {
            "root/libbad.so",
            "",
            std::span{image},
            A32AndroidLibrarySourceError::Failed,
        },
        {
            "root/libemptyid.so",
            "",
            std::span{image},
        },
        {
            "root/liboversize.so",
            "oversize",
            std::span{image},
        },
    }};
    RecordingSource source{std::span{entries}};
    const std::array<A32AndroidLibrarySearchRoot, 1> roots{{
        {"requester", "root"},
    }};
    A32AndroidLibrarySearchProvider provider{
        std::span{roots}, source, A32AndroidLibrarySearchOptions{64U}};

    auto result =
        provider.resolve_for("requester", "libbad.so", image.size());
    if (result.error != Elf32DependencyProviderError::Failed) {
        return fail("source hard failure did not stop Android search");
    }

    result = provider.resolve_for(
        "requester", "libemptyid.so", image.size());
    if (result.error != Elf32DependencyProviderError::Failed) {
        return fail("empty source identity was accepted");
    }

    result = provider.resolve_for(
        "requester", "liboversize.so", image.size() - 1U);
    if (result.error != Elf32DependencyProviderError::Failed) {
        return fail("oversize source image was accepted");
    }
    return 0;
}

int test_malformed_root_and_context_free_behavior() {
    constexpr std::array<SourceEntry, 0> entries{};
    RecordingSource source{std::span{entries}};
    const std::array<A32AndroidLibrarySearchRoot, 1> roots{{
        {"requester", ""},
    }};
    A32AndroidLibrarySearchProvider provider{
        std::span{roots}, source, A32AndroidLibrarySearchOptions{32U}};

    auto result =
        provider.resolve_for("requester", "libx.so", 16U);
    if (result.error != Elf32DependencyProviderError::Failed) {
        return fail("empty matching search root was not rejected");
    }

    result = provider.resolve("libx.so", 16U);
    if (result.error != Elf32DependencyProviderError::NotFound) {
        return fail("context-free Android search should not select requester roots");
    }

    const std::array<A32AndroidLibrarySearchRoot, 1> long_roots{{
        {"requester", "base.apk!/lib/armeabi-v7a"},
    }};
    A32AndroidLibrarySearchProvider bounded{
        std::span{long_roots}, source, A32AndroidLibrarySearchOptions{8U}};
    result = bounded.resolve_for("requester", "libx.so", 16U);
    if (result.error != Elf32DependencyProviderError::Failed) {
        return fail("candidate path ceiling was not enforced");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status =
            test_filesystem_source_exact_read_and_provider_composition();
        status != 0) {
        return status;
    }
    if (const int status = test_apk_root_exact_success(); status != 0) {
        return status;
    }
    if (const int status = test_ordered_root_fallback(); status != 0) {
        return status;
    }
    if (const int status = test_invalid_requests_do_not_probe_source();
        status != 0) {
        return status;
    }
    if (const int status = test_hard_failure_and_resource_validation();
        status != 0) {
        return status;
    }
    return test_malformed_root_and_context_free_behavior();
}
