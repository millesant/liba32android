#include "compat/a32_android_library_search.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace liba32android::compat {
namespace {

[[nodiscard]] elf::Elf32DependencyProviderResult provider_failure(
    elf::Elf32DependencyProviderError error) {
    elf::Elf32DependencyProviderResult result;
    result.error = error;
    return result;
}

[[nodiscard]] A32AndroidLibrarySourceResult source_failure(
    A32AndroidLibrarySourceError error) {
    A32AndroidLibrarySourceResult result;
    result.error = error;
    return result;
}

[[nodiscard]] bool contains_nul(std::string_view value) noexcept {
    return value.find('\0') != std::string_view::npos;
}

class ScopedFileDescriptor {
public:
    explicit ScopedFileDescriptor(int fd) noexcept : fd_(fd) {}
    ~ScopedFileDescriptor() {
        if (fd_ >= 0) {
            static_cast<void>(::close(fd_));
        }
    }

    ScopedFileDescriptor(const ScopedFileDescriptor&) = delete;
    ScopedFileDescriptor& operator=(const ScopedFileDescriptor&) = delete;

    [[nodiscard]] int get() const noexcept { return fd_; }

private:
    int fd_{-1};
};

}  // namespace

A32AndroidLibrarySourceResult A32FilesystemLibrarySource::load(
    std::string_view virtual_path,
    std::uint64_t max_image_bytes) {
    if (options_.max_path_bytes == 0U ||
        virtual_path.empty() ||
        contains_nul(virtual_path) ||
        virtual_path.size() > options_.max_path_bytes ||
        max_image_bytes == 0U) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }

    std::string path{virtual_path};
    const int raw_fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (raw_fd < 0) {
        if (errno == ENOENT || errno == ENOTDIR) {
            return source_failure(A32AndroidLibrarySourceError::NotFound);
        }
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }
    ScopedFileDescriptor fd{raw_fd};

    struct stat status {};
    if (::fstat(fd.get(), &status) != 0 ||
        !S_ISREG(status.st_mode) ||
        status.st_size <= 0) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }

    const std::uint64_t image_size =
        static_cast<std::uint64_t>(status.st_size);
    if (image_size > max_image_bytes ||
        image_size > std::numeric_limits<std::size_t>::max()) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }

    std::vector<std::uint8_t> image;
    try {
        image.resize(static_cast<std::size_t>(image_size));
    } catch (const std::bad_alloc&) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }

    std::size_t offset = 0U;
    while (offset < image.size()) {
        constexpr std::size_t kReadChunk = 1U << 20U;
        const std::size_t remaining = image.size() - offset;
        const std::size_t chunk = std::min(remaining, kReadChunk);
        const ssize_t read_count =
            ::read(fd.get(), image.data() + offset, chunk);
        if (read_count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return source_failure(A32AndroidLibrarySourceError::Failed);
        }
        if (read_count == 0) {
            return source_failure(A32AndroidLibrarySourceError::Failed);
        }
        offset += static_cast<std::size_t>(read_count);
    }

    A32AndroidLibrarySourceResult result;
    result.identity.assign(virtual_path.data(), virtual_path.size());
    result.image = std::move(image);
    return result;
}

bool A32AndroidLibrarySearchProvider::valid_bare_name(
    std::string_view requested_name) const noexcept {
    if (requested_name.empty() ||
        contains_nul(requested_name) ||
        requested_name.find('/') != std::string_view::npos ||
        requested_name.find('\\') != std::string_view::npos) {
        return false;
    }
    return true;
}

bool A32AndroidLibrarySearchProvider::build_candidate(
    std::string_view root,
    std::string_view requested_name,
    std::string& output) const {
    if (options_.max_path_bytes == 0U ||
        root.empty() ||
        contains_nul(root)) {
        return false;
    }

    const bool has_separator = root.back() == '/';
    const std::uint64_t size =
        static_cast<std::uint64_t>(root.size()) +
        (has_separator ? 0U : 1U) +
        requested_name.size();
    if (size == 0U ||
        size > options_.max_path_bytes ||
        size > std::numeric_limits<std::size_t>::max()) {
        return false;
    }

    output.clear();
    output.reserve(static_cast<std::size_t>(size));
    output.append(root.data(), root.size());
    if (!has_separator) {
        output.push_back('/');
    }
    output.append(requested_name.data(), requested_name.size());
    return true;
}

elf::Elf32DependencyProviderResult
A32AndroidLibrarySearchProvider::resolve(
    std::string_view requested_name,
    std::uint64_t max_image_bytes) {
    return resolve_for({}, requested_name, max_image_bytes);
}

elf::Elf32DependencyProviderResult
A32AndroidLibrarySearchProvider::resolve_for(
    std::string_view requester_identity,
    std::string_view requested_name,
    std::uint64_t max_image_bytes) {
    if (requester_identity.empty() ||
        contains_nul(requester_identity) ||
        !valid_bare_name(requested_name)) {
        return provider_failure(elf::Elf32DependencyProviderError::NotFound);
    }

    std::string candidate;
    for (const A32AndroidLibrarySearchRoot& root : roots_) {
        if (root.requester_identity != requester_identity) {
            continue;
        }
        if (root.requester_identity.empty() ||
            !build_candidate(root.root, requested_name, candidate)) {
            return provider_failure(elf::Elf32DependencyProviderError::Failed);
        }

        A32AndroidLibrarySourceResult source_result =
            source_.load(candidate, max_image_bytes);
        switch (source_result.error) {
        case A32AndroidLibrarySourceError::NotFound:
            continue;
        case A32AndroidLibrarySourceError::Failed:
            return provider_failure(elf::Elf32DependencyProviderError::Failed);
        case A32AndroidLibrarySourceError::None:
            break;
        }

        if (source_result.identity.empty() ||
            source_result.image.empty() ||
            source_result.image.size() > max_image_bytes) {
            return provider_failure(elf::Elf32DependencyProviderError::Failed);
        }

        elf::Elf32DependencyProviderResult result;
        result.source.identity = std::move(source_result.identity);
        result.source.image = std::move(source_result.image);
        return result;
    }

    return provider_failure(elf::Elf32DependencyProviderError::NotFound);
}

}  // namespace liba32android::compat
