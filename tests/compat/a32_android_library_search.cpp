#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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
using liba32android::compat::A32ApkLibrarySource;
using liba32android::compat::A32ApkLibrarySourceOptions;
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

struct ZipFixtureEntry {
    std::string name;
    std::vector<std::uint8_t> image;
    std::vector<std::uint8_t> payload;
    std::uint16_t method{};
    std::uint16_t flags{};
    std::optional<std::uint32_t> crc_override;
    std::optional<std::uint16_t> local_method_override;
};

void append_u16(
    std::vector<std::uint8_t>& output,
    std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_u32(
    std::vector<std::uint8_t>& output,
    std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
}

std::uint32_t fixture_crc32(
    std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (const std::uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask =
                0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

std::vector<std::uint8_t> make_zip_fixture(
    std::span<const ZipFixtureEntry> entries,
    bool zip64_eocd = false) {
    std::vector<std::uint8_t> output;
    std::vector<std::uint32_t> local_offsets;
    local_offsets.reserve(entries.size());

    for (const ZipFixtureEntry& entry : entries) {
        const std::vector<std::uint8_t>& payload =
            entry.payload.empty() && entry.method == 0U
                ? entry.image
                : entry.payload;
        local_offsets.push_back(
            static_cast<std::uint32_t>(output.size()));
        const std::uint32_t crc = entry.crc_override.value_or(
            fixture_crc32(entry.image));
        const std::uint16_t local_method =
            entry.local_method_override.value_or(entry.method);

        append_u32(output, 0x04034b50U);
        append_u16(output, 20U);
        append_u16(output, entry.flags);
        append_u16(output, local_method);
        append_u16(output, 0U);
        append_u16(output, 0U);
        append_u32(output, crc);
        append_u32(
            output,
            static_cast<std::uint32_t>(payload.size()));
        append_u32(
            output,
            static_cast<std::uint32_t>(entry.image.size()));
        append_u16(
            output,
            static_cast<std::uint16_t>(entry.name.size()));
        append_u16(output, 0U);
        output.insert(
            output.end(), entry.name.begin(), entry.name.end());
        output.insert(
            output.end(), payload.begin(), payload.end());
    }

    const std::uint32_t central_offset =
        static_cast<std::uint32_t>(output.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const ZipFixtureEntry& entry = entries[index];
        const std::vector<std::uint8_t>& payload =
            entry.payload.empty() && entry.method == 0U
                ? entry.image
                : entry.payload;
        const std::uint32_t crc = entry.crc_override.value_or(
            fixture_crc32(entry.image));

        append_u32(output, 0x02014b50U);
        append_u16(output, 20U);
        append_u16(output, 20U);
        append_u16(output, entry.flags);
        append_u16(output, entry.method);
        append_u16(output, 0U);
        append_u16(output, 0U);
        append_u32(output, crc);
        append_u32(
            output,
            static_cast<std::uint32_t>(payload.size()));
        append_u32(
            output,
            static_cast<std::uint32_t>(entry.image.size()));
        append_u16(
            output,
            static_cast<std::uint16_t>(entry.name.size()));
        append_u16(output, 0U);
        append_u16(output, 0U);
        append_u16(output, 0U);
        append_u16(output, 0U);
        append_u32(output, 0U);
        append_u32(output, local_offsets[index]);
        output.insert(
            output.end(), entry.name.begin(), entry.name.end());
    }

    const std::uint32_t central_bytes =
        static_cast<std::uint32_t>(
            output.size() - central_offset);
    append_u32(output, 0x06054b50U);
    append_u16(output, 0U);
    append_u16(output, 0U);
    const std::uint16_t count = zip64_eocd
        ? 0xffffU
        : static_cast<std::uint16_t>(entries.size());
    append_u16(output, count);
    append_u16(output, count);
    append_u32(output, central_bytes);
    append_u32(output, central_offset);
    append_u16(output, 0U);
    return output;
}

bool write_fixture_file(
    const std::filesystem::path& path,
    std::span<const std::uint8_t> bytes) {
    std::ofstream output{
        path, std::ios::binary | std::ios::trunc};
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

A32ApkLibrarySourceOptions apk_options() {
    return A32ApkLibrarySourceOptions{
        .max_virtual_path_bytes = 4096U,
        .max_archive_bytes = 1U << 20,
        .max_entries = 16U,
        .max_central_directory_bytes = 64U << 10,
        .max_entry_name_bytes = 512U,
    };
}

int test_apk_source_stored_deflate_and_provider_composition() {
    TemporaryDirectory temporary;
    if (!temporary.ready()) {
        return fail("could not create temporary APK-source directory");
    }

    const std::vector<std::uint8_t> stored_image{
        0x7fU, 'E', 'L', 'F', '-', 's', 't', 'o', 'r', 'e', 'd'};
    const std::vector<std::uint8_t> deflated_image{
        0x7fU, 'E', 'L', 'F', 'a', 'p', 'k', '-', 'd', 'e', 'f', 'l',
        'a', 't', 'e', '-', 'f', 'i', 'x', 't', 'u', 'r', 'e'};
    const std::vector<std::uint8_t> deflated_payload{
        0xabU,0x77U,0xf5U,0x71U,0x4bU,0x2cU,0xc8U,0xd6U,
        0x4dU,0x49U,0x4dU,0xcbU,0x49U,0x2cU,0x49U,0xd5U,
        0x4dU,0xcbU,0xacU,0x28U,0x29U,0x2dU,0x4aU,0x05U,0x00U,
    };
    const std::array<ZipFixtureEntry, 2> entries{{
        {
            .name = "lib/armeabi-v7a/libstored.so",
            .image = stored_image,
            .method = 0U,
        },
        {
            .name = "lib/armeabi-v7a/libdeflated.so",
            .image = deflated_image,
            .payload = deflated_payload,
            .method = 8U,
        },
    }};
    const auto archive = make_zip_fixture(entries);
    const auto apk_path = temporary.path() / "base.apk";
    if (!write_fixture_file(apk_path, archive)) {
        return fail("could not write APK-source fixture");
    }

    A32ApkLibrarySource source{apk_options()};
    const std::string stored_virtual =
        apk_path.string() + "!/lib/armeabi-v7a/libstored.so";
    const std::string deflated_virtual =
        apk_path.string() + "!/lib/armeabi-v7a/libdeflated.so";

    const auto stored = source.load(
        stored_virtual, stored_image.size());
    if (!stored ||
        stored.identity != stored_virtual ||
        stored.image != stored_image) {
        return fail("APK source did not read stored entry");
    }

    const auto deflated = source.load(
        deflated_virtual, deflated_image.size());
    if (!deflated ||
        deflated.identity != deflated_virtual ||
        deflated.image != deflated_image) {
        return fail("APK source did not inflate raw DEFLATE entry");
    }

    const std::string root =
        apk_path.string() + "!/lib/armeabi-v7a";
    const std::array<A32AndroidLibrarySearchRoot, 1> roots{{
        {"root.so", root},
    }};
    A32AndroidLibrarySearchProvider provider{
        std::span{roots},
        source,
        A32AndroidLibrarySearchOptions{.max_path_bytes = 4096U},
    };
    const auto resolved = provider.resolve_for(
        "root.so", "libdeflated.so", deflated_image.size());
    if (!resolved ||
        resolved.source.identity != deflated_virtual ||
        resolved.source.image != deflated_image) {
        return fail("APK source did not compose with requester search");
    }
    return 0;
}

int test_apk_source_missing_and_resource_failures() {
    TemporaryDirectory temporary;
    if (!temporary.ready()) {
        return fail("could not create APK resource-test directory");
    }

    const std::vector<std::uint8_t> image{
        0x7fU, 'E', 'L', 'F', 1U, 2U, 3U, 4U};
    const std::array<ZipFixtureEntry, 2> entries{{
        {
            .name = "lib/armeabi-v7a/libone.so",
            .image = image,
            .method = 0U,
        },
        {
            .name = "lib/armeabi-v7a/libtwo.so",
            .image = image,
            .method = 0U,
        },
    }};
    const auto archive = make_zip_fixture(entries);
    const auto apk_path = temporary.path() / "limits.apk";
    if (!write_fixture_file(apk_path, archive)) {
        return fail("could not write APK resource-test fixture");
    }
    const std::string prefix = apk_path.string() + "!/";

    A32ApkLibrarySource source{apk_options()};
    if (source.load(
            (temporary.path() / "missing.apk").string() +
                "!/lib/armeabi-v7a/libone.so",
            image.size()).error !=
            A32AndroidLibrarySourceError::NotFound ||
        source.load(
            prefix + "lib/armeabi-v7a/missing.so",
            image.size()).error !=
            A32AndroidLibrarySourceError::NotFound) {
        return fail("APK source missing archive/entry was not NotFound");
    }

    std::string nul_path = prefix + "lib/armeabi-v7a/libone.so";
    nul_path.push_back('\0');
    nul_path.append("tail");
    const std::array<std::string, 3> malformed{{
        apk_path.string(),
        apk_path.string() + "!/",
        nul_path,
    }};
    for (const std::string& candidate : malformed) {
        if (source.load(
                std::string_view{candidate.data(), candidate.size()},
                image.size()).error !=
            A32AndroidLibrarySourceError::Failed) {
            return fail("APK source accepted malformed virtual path");
        }
    }

    auto limited = apk_options();
    limited.max_archive_bytes = archive.size() - 1U;
    if (A32ApkLibrarySource{limited}.load(
            prefix + "lib/armeabi-v7a/libone.so",
            image.size()).error !=
        A32AndroidLibrarySourceError::Failed) {
        return fail("APK source ignored archive byte ceiling");
    }

    limited = apk_options();
    limited.max_entries = 1U;
    if (A32ApkLibrarySource{limited}.load(
            prefix + "lib/armeabi-v7a/libone.so",
            image.size()).error !=
        A32AndroidLibrarySourceError::Failed) {
        return fail("APK source ignored entry-count ceiling");
    }

    limited = apk_options();
    limited.max_central_directory_bytes = 1U;
    if (A32ApkLibrarySource{limited}.load(
            prefix + "lib/armeabi-v7a/libone.so",
            image.size()).error !=
        A32AndroidLibrarySourceError::Failed) {
        return fail("APK source ignored central-directory ceiling");
    }

    limited = apk_options();
    limited.max_entry_name_bytes = 4U;
    if (A32ApkLibrarySource{limited}.load(
            prefix + "lib/armeabi-v7a/libone.so",
            image.size()).error !=
        A32AndroidLibrarySourceError::Failed) {
        return fail("APK source ignored entry-name ceiling");
    }

    limited = apk_options();
    limited.max_virtual_path_bytes = 8U;
    if (A32ApkLibrarySource{limited}.load(
            prefix + "lib/armeabi-v7a/libone.so",
            image.size()).error !=
        A32AndroidLibrarySourceError::Failed) {
        return fail("APK source ignored virtual-path ceiling");
    }

    if (source.load(
            prefix + "lib/armeabi-v7a/libone.so",
            image.size() - 1U).error !=
        A32AndroidLibrarySourceError::Failed) {
        return fail("APK source ignored image byte ceiling");
    }
    return 0;
}

int test_apk_source_rejects_malformed_entries() {
    TemporaryDirectory temporary;
    if (!temporary.ready()) {
        return fail("could not create malformed-APK directory");
    }

    const std::vector<std::uint8_t> image{
        0x7fU, 'E', 'L', 'F', 9U, 8U, 7U, 6U};
    const std::string name = "lib/armeabi-v7a/libbad.so";
    const auto apk_path = temporary.path() / "bad.apk";
    const std::string virtual_path =
        apk_path.string() + "!/" + name;

    const auto expect_failed =
        [&](std::span<const ZipFixtureEntry> entries,
            bool zip64 = false) -> bool {
            const auto archive =
                make_zip_fixture(entries, zip64);
            if (!write_fixture_file(apk_path, archive)) {
                return false;
            }
            A32ApkLibrarySource source{apk_options()};
            return source.load(
                virtual_path, image.size()).error ==
                A32AndroidLibrarySourceError::Failed;
        };

    const std::array<ZipFixtureEntry, 1> encrypted{{
        {
            .name = name,
            .image = image,
            .method = 0U,
            .flags = 1U,
        },
    }};
    if (!expect_failed(encrypted)) {
        return fail("APK source accepted encrypted entry");
    }

    const std::array<ZipFixtureEntry, 1> unsupported{{
        {
            .name = name,
            .image = image,
            .payload = image,
            .method = 12U,
        },
    }};
    if (!expect_failed(unsupported)) {
        return fail("APK source accepted unsupported compression");
    }

    const std::array<ZipFixtureEntry, 1> bad_crc{{
        {
            .name = name,
            .image = image,
            .method = 0U,
            .crc_override = 0x12345678U,
        },
    }};
    if (!expect_failed(bad_crc)) {
        return fail("APK source accepted CRC mismatch");
    }

    const std::array<ZipFixtureEntry, 1> local_mismatch{{
        {
            .name = name,
            .image = image,
            .method = 0U,
            .local_method_override = 8U,
        },
    }};
    if (!expect_failed(local_mismatch)) {
        return fail("APK source accepted local/central method mismatch");
    }

    const std::array<ZipFixtureEntry, 2> duplicate{{
        {
            .name = name,
            .image = image,
            .method = 0U,
        },
        {
            .name = name,
            .image = image,
            .method = 0U,
        },
    }};
    if (!expect_failed(duplicate)) {
        return fail("APK source accepted duplicate exact entry name");
    }

    const std::array<ZipFixtureEntry, 1> ordinary{{
        {
            .name = name,
            .image = image,
            .method = 0U,
        },
    }};
    if (!expect_failed(ordinary, true)) {
        return fail("APK source accepted ZIP64 EOCD sentinel");
    }
    return 0;
}

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
            test_apk_source_stored_deflate_and_provider_composition();
        status != 0) {
        return status;
    }
    if (const int status = test_apk_source_missing_and_resource_failures();
        status != 0) {
        return status;
    }
    if (const int status = test_apk_source_rejects_malformed_entries();
        status != 0) {
        return status;
    }
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
