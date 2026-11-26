#include "compat/a32_android_library_search.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include <zlib.h>

namespace liba32android::compat {
namespace {

constexpr std::uint32_t kZipLocalHeaderSignature = 0x04034b50U;
constexpr std::uint32_t kZipCentralHeaderSignature = 0x02014b50U;
constexpr std::uint32_t kZipEocdSignature = 0x06054b50U;
constexpr std::size_t kZipLocalHeaderBytes = 30U;
constexpr std::size_t kZipCentralHeaderBytes = 46U;
constexpr std::size_t kZipEocdBytes = 22U;
constexpr std::size_t kZipMaxCommentBytes = 0xffffU;
constexpr std::uint16_t kZipFlagEncrypted = 0x0001U;
constexpr std::uint16_t kZipFlagDataDescriptor = 0x0008U;
constexpr std::uint16_t kZipMethodStored = 0U;
constexpr std::uint16_t kZipMethodDeflate = 8U;

[[nodiscard]] A32AndroidLibrarySourceResult source_failure(
    A32AndroidLibrarySourceError error) {
    A32AndroidLibrarySourceResult result;
    result.error = error;
    return result;
}

[[nodiscard]] A32ApkLibraryCatalogResult catalog_failure(
    A32ApkLibraryCatalogError error) {
    A32ApkLibraryCatalogResult result;
    result.error = error;
    return result;
}

[[nodiscard]] bool contains_nul(std::string_view value) noexcept {
    return value.find('\0') != std::string_view::npos;
}

[[nodiscard]] std::uint16_t read_u16(
    const std::uint8_t* data) noexcept {
    return static_cast<std::uint16_t>(data[0]) |
           (static_cast<std::uint16_t>(data[1]) << 8U);
}

[[nodiscard]] std::uint32_t read_u32(
    const std::uint8_t* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

class ScopedFileDescriptor {
public:
    ScopedFileDescriptor() noexcept = default;
    explicit ScopedFileDescriptor(int fd) noexcept : fd_(fd) {}

    ~ScopedFileDescriptor() {
        reset();
    }

    ScopedFileDescriptor(const ScopedFileDescriptor&) = delete;
    ScopedFileDescriptor& operator=(const ScopedFileDescriptor&) = delete;

    ScopedFileDescriptor(ScopedFileDescriptor&& other) noexcept
        : fd_(std::exchange(other.fd_, -1)) {}

    ScopedFileDescriptor& operator=(
        ScopedFileDescriptor&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }

private:
    void reset() noexcept {
        if (fd_ >= 0) {
            static_cast<void>(::close(fd_));
            fd_ = -1;
        }
    }

    int fd_{-1};
};

[[nodiscard]] bool pread_exact(
    int fd,
    std::uint64_t offset,
    std::span<std::uint8_t> output) noexcept {
    if (offset >
        static_cast<std::uint64_t>(
            std::numeric_limits<off_t>::max())) {
        return false;
    }

    std::size_t completed = 0U;
    while (completed < output.size()) {
        const std::size_t remaining = output.size() - completed;
        const std::size_t chunk = std::min(
            remaining,
            static_cast<std::size_t>(
                std::numeric_limits<ssize_t>::max()));
        const std::uint64_t current =
            offset + static_cast<std::uint64_t>(completed);
        if (current >
            static_cast<std::uint64_t>(
                std::numeric_limits<off_t>::max())) {
            return false;
        }

        const ssize_t count = ::pread(
            fd,
            output.data() + completed,
            chunk,
            static_cast<off_t>(current));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (count == 0) {
            return false;
        }
        completed += static_cast<std::size_t>(count);
    }
    return true;
}

struct ZipEntry {
    std::uint16_t flags{};
    std::uint16_t method{};
    std::uint32_t crc32{};
    std::uint32_t compressed_size{};
    std::uint32_t uncompressed_size{};
    std::uint32_t local_header_offset{};
    std::string name;
};

struct ZipDirectory {
    A32AndroidLibrarySourceError error{
        A32AndroidLibrarySourceError::None};
    ScopedFileDescriptor fd;
    std::uint32_t central_offset{};
    std::vector<ZipEntry> entries;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == A32AndroidLibrarySourceError::None;
    }
};

[[nodiscard]] bool options_valid(
    const A32ApkLibrarySourceOptions& options) noexcept {
    return options.max_virtual_path_bytes != 0U &&
           options.max_archive_bytes != 0U &&
           options.max_entries != 0U &&
           options.max_central_directory_bytes != 0U &&
           options.max_entry_name_bytes != 0U;
}

[[nodiscard]] bool catalog_options_valid(
    const A32ApkLibraryCatalogOptions& options) noexcept {
    return options.max_libraries != 0U &&
           options.max_soname_bytes != 0U &&
           options.max_total_soname_bytes != 0U &&
           options.max_abi_directory_bytes != 0U;
}

[[nodiscard]] bool checked_add(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool valid_abi_directory(
    std::string_view value,
    std::uint32_t max_bytes) noexcept {
    if (value.empty() ||
        value.size() > max_bytes ||
        contains_nul(value) ||
        value.front() == '/' ||
        value.back() == '/' ||
        value.find('\\') != std::string_view::npos ||
        value.find('!') != std::string_view::npos) {
        return false;
    }

    std::size_t cursor = 0U;
    while (cursor < value.size()) {
        const std::size_t separator = value.find('/', cursor);
        const std::size_t end =
            separator == std::string_view::npos
                ? value.size()
                : separator;
        const std::string_view component =
            value.substr(cursor, end - cursor);
        if (component.empty() ||
            component == "." ||
            component == "..") {
            return false;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        cursor = separator + 1U;
    }
    return true;
}

[[nodiscard]] bool bare_shared_object_name(
    std::string_view value) noexcept {
    return value.size() > 3U &&
           value.ends_with(".so") &&
           !contains_nul(value) &&
           value.find('/') == std::string_view::npos &&
           value.find('\\') == std::string_view::npos;
}

[[nodiscard]] bool verify_crc(
    std::span<const std::uint8_t> image,
    std::uint32_t expected_crc) noexcept {
    if (image.size() >
        static_cast<std::size_t>(
            std::numeric_limits<uInt>::max())) {
        return false;
    }
    uLong crc = ::crc32(0L, Z_NULL, 0U);
    crc = ::crc32(
        crc,
        reinterpret_cast<const Bytef*>(image.data()),
        static_cast<uInt>(image.size()));
    return static_cast<std::uint32_t>(crc) == expected_crc;
}

[[nodiscard]] ZipDirectory read_zip_directory(
    std::string_view archive_view,
    const A32ApkLibrarySourceOptions& options) {
    ZipDirectory result;
    if (!options_valid(options) ||
        archive_view.empty() ||
        contains_nul(archive_view) ||
        archive_view.size() > options.max_virtual_path_bytes) {
        result.error = A32AndroidLibrarySourceError::Failed;
        return result;
    }

    std::string archive_path;
    try {
        archive_path.assign(archive_view.data(), archive_view.size());
    } catch (const std::bad_alloc&) {
        result.error = A32AndroidLibrarySourceError::Failed;
        return result;
    }

    const int raw_fd =
        ::open(archive_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (raw_fd < 0) {
        result.error =
            (errno == ENOENT || errno == ENOTDIR)
                ? A32AndroidLibrarySourceError::NotFound
                : A32AndroidLibrarySourceError::Failed;
        return result;
    }
    ScopedFileDescriptor fd{raw_fd};

    struct stat status {};
    if (::fstat(fd.get(), &status) != 0 ||
        !S_ISREG(status.st_mode) ||
        status.st_size <= 0) {
        result.error = A32AndroidLibrarySourceError::Failed;
        return result;
    }
    const std::uint64_t archive_size =
        static_cast<std::uint64_t>(status.st_size);
    if (archive_size > options.max_archive_bytes ||
        archive_size < kZipEocdBytes) {
        result.error = A32AndroidLibrarySourceError::Failed;
        return result;
    }

    const std::uint64_t tail_limit =
        kZipEocdBytes + kZipMaxCommentBytes;
    const std::size_t tail_size = static_cast<std::size_t>(
        std::min(archive_size, tail_limit));
    std::vector<std::uint8_t> tail;
    try {
        tail.resize(tail_size);
    } catch (const std::bad_alloc&) {
        result.error = A32AndroidLibrarySourceError::Failed;
        return result;
    }
    const std::uint64_t tail_offset = archive_size - tail_size;
    if (!pread_exact(fd.get(), tail_offset, tail)) {
        result.error = A32AndroidLibrarySourceError::Failed;
        return result;
    }

    std::optional<std::size_t> eocd_tail_offset;
    for (std::size_t position = tail.size() - kZipEocdBytes;; --position) {
        if (read_u32(tail.data() + position) == kZipEocdSignature) {
            const std::uint16_t comment_bytes =
                read_u16(tail.data() + position + 20U);
            const std::uint64_t record_end =
                static_cast<std::uint64_t>(position) +
                kZipEocdBytes + comment_bytes;
            if (record_end == tail.size()) {
                eocd_tail_offset = position;
                break;
            }
        }
        if (position == 0U) {
            break;
        }
    }
    if (!eocd_tail_offset.has_value()) {
        result.error = A32AndroidLibrarySourceError::Failed;
        return result;
    }

    const std::uint8_t* eocd =
        tail.data() + *eocd_tail_offset;
    const std::uint16_t disk_number = read_u16(eocd + 4U);
    const std::uint16_t central_disk = read_u16(eocd + 6U);
    const std::uint16_t entries_on_disk = read_u16(eocd + 8U);
    const std::uint16_t entry_count = read_u16(eocd + 10U);
    const std::uint32_t central_bytes = read_u32(eocd + 12U);
    const std::uint32_t central_offset = read_u32(eocd + 16U);

    if (disk_number != 0U ||
        central_disk != 0U ||
        entries_on_disk != entry_count ||
        entry_count == 0xffffU ||
        central_bytes == 0xffffffffU ||
        central_offset == 0xffffffffU ||
        entry_count > options.max_entries ||
        central_bytes > options.max_central_directory_bytes) {
        result.error = A32AndroidLibrarySourceError::Failed;
        return result;
    }

    const std::uint64_t eocd_absolute =
        tail_offset + *eocd_tail_offset;
    std::uint64_t central_end = 0U;
    if (!checked_add(central_offset, central_bytes, central_end) ||
        central_end != eocd_absolute ||
        central_end > archive_size) {
        result.error = A32AndroidLibrarySourceError::Failed;
        return result;
    }

    try {
        result.entries.reserve(entry_count);
    } catch (const std::bad_alloc&) {
        result.error = A32AndroidLibrarySourceError::Failed;
        return result;
    }

    std::uint64_t cursor = central_offset;
    for (std::uint32_t index = 0U;
         index < entry_count;
         ++index) {
        std::uint64_t fixed_end = 0U;
        if (!checked_add(cursor, kZipCentralHeaderBytes, fixed_end) ||
            fixed_end > central_end) {
            result.error = A32AndroidLibrarySourceError::Failed;
            return result;
        }

        std::array<std::uint8_t, kZipCentralHeaderBytes> header{};
        if (!pread_exact(fd.get(), cursor, header) ||
            read_u32(header.data()) != kZipCentralHeaderSignature) {
            result.error = A32AndroidLibrarySourceError::Failed;
            return result;
        }

        const std::uint16_t flags = read_u16(header.data() + 8U);
        const std::uint16_t method = read_u16(header.data() + 10U);
        const std::uint32_t expected_crc = read_u32(header.data() + 16U);
        const std::uint32_t compressed_size =
            read_u32(header.data() + 20U);
        const std::uint32_t uncompressed_size =
            read_u32(header.data() + 24U);
        const std::uint16_t name_bytes = read_u16(header.data() + 28U);
        const std::uint16_t extra_bytes = read_u16(header.data() + 30U);
        const std::uint16_t comment_bytes = read_u16(header.data() + 32U);
        const std::uint16_t disk_start = read_u16(header.data() + 34U);
        const std::uint32_t local_offset =
            read_u32(header.data() + 42U);

        if (disk_start != 0U ||
            compressed_size == 0xffffffffU ||
            uncompressed_size == 0xffffffffU ||
            local_offset == 0xffffffffU ||
            name_bytes == 0U ||
            name_bytes > options.max_entry_name_bytes) {
            result.error = A32AndroidLibrarySourceError::Failed;
            return result;
        }

        std::uint64_t record_end = fixed_end;
        if (!checked_add(record_end, name_bytes, record_end) ||
            !checked_add(record_end, extra_bytes, record_end) ||
            !checked_add(record_end, comment_bytes, record_end) ||
            record_end > central_end) {
            result.error = A32AndroidLibrarySourceError::Failed;
            return result;
        }

        ZipEntry entry{
            .flags = flags,
            .method = method,
            .crc32 = expected_crc,
            .compressed_size = compressed_size,
            .uncompressed_size = uncompressed_size,
            .local_header_offset = local_offset,
        };
        try {
            entry.name.resize(name_bytes);
        } catch (const std::bad_alloc&) {
            result.error = A32AndroidLibrarySourceError::Failed;
            return result;
        }
        if (!pread_exact(
                fd.get(),
                cursor + kZipCentralHeaderBytes,
                std::span<std::uint8_t>{
                    reinterpret_cast<std::uint8_t*>(entry.name.data()),
                    entry.name.size()})) {
            result.error = A32AndroidLibrarySourceError::Failed;
            return result;
        }
        try {
            result.entries.push_back(std::move(entry));
        } catch (const std::bad_alloc&) {
            result.error = A32AndroidLibrarySourceError::Failed;
            return result;
        }
        cursor = record_end;
    }

    if (cursor != central_end) {
        result.error = A32AndroidLibrarySourceError::Failed;
        return result;
    }

    result.fd = std::move(fd);
    result.central_offset = central_offset;
    return result;
}

}  // namespace

A32AndroidLibrarySourceResult A32ApkLibrarySource::load(
    std::string_view virtual_path,
    std::uint64_t max_image_bytes) {
    if (!options_valid(options_) ||
        max_image_bytes == 0U ||
        virtual_path.empty() ||
        contains_nul(virtual_path) ||
        virtual_path.size() > options_.max_virtual_path_bytes) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }

    const std::size_t delimiter = virtual_path.find("!/");
    if (delimiter == std::string_view::npos ||
        delimiter == 0U ||
        delimiter + 2U >= virtual_path.size()) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }

    const std::string_view archive_view =
        virtual_path.substr(0U, delimiter);
    const std::string_view entry_view =
        virtual_path.substr(delimiter + 2U);
    if (entry_view.empty() ||
        entry_view.size() > options_.max_entry_name_bytes ||
        contains_nul(entry_view)) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }

    ZipDirectory directory =
        read_zip_directory(archive_view, options_);
    if (!directory) {
        return source_failure(directory.error);
    }

    const ZipEntry* selected = nullptr;
    for (const ZipEntry& entry : directory.entries) {
        if (entry.name != entry_view) {
            continue;
        }
        if (selected != nullptr) {
            return source_failure(A32AndroidLibrarySourceError::Failed);
        }
        selected = &entry;
    }
    if (selected == nullptr) {
        return source_failure(A32AndroidLibrarySourceError::NotFound);
    }

    const ZipEntry& entry = *selected;
    if ((entry.flags & kZipFlagEncrypted) != 0U ||
        (entry.method != kZipMethodStored &&
         entry.method != kZipMethodDeflate) ||
        entry.uncompressed_size == 0U ||
        entry.uncompressed_size > max_image_bytes ||
        entry.uncompressed_size >
            std::numeric_limits<std::size_t>::max() ||
        entry.compressed_size >
            std::numeric_limits<std::size_t>::max()) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }

    std::uint64_t local_fixed_end = 0U;
    if (!checked_add(
            entry.local_header_offset,
            kZipLocalHeaderBytes,
            local_fixed_end) ||
        local_fixed_end > directory.central_offset) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }

    std::array<std::uint8_t, kZipLocalHeaderBytes> local{};
    if (!pread_exact(
            directory.fd.get(),
            entry.local_header_offset,
            local) ||
        read_u32(local.data()) != kZipLocalHeaderSignature) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }

    const std::uint16_t local_flags = read_u16(local.data() + 6U);
    const std::uint16_t local_method = read_u16(local.data() + 8U);
    const std::uint32_t local_crc = read_u32(local.data() + 14U);
    const std::uint32_t local_compressed = read_u32(local.data() + 18U);
    const std::uint32_t local_uncompressed = read_u32(local.data() + 22U);
    const std::uint16_t local_name_bytes = read_u16(local.data() + 26U);
    const std::uint16_t local_extra_bytes = read_u16(local.data() + 28U);

    if (local_flags != entry.flags ||
        local_method != entry.method ||
        (local_flags & kZipFlagEncrypted) != 0U ||
        local_name_bytes == 0U ||
        local_name_bytes > options_.max_entry_name_bytes) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }
    if ((local_flags & kZipFlagDataDescriptor) == 0U &&
        (local_crc != entry.crc32 ||
         local_compressed != entry.compressed_size ||
         local_uncompressed != entry.uncompressed_size)) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }

    std::string local_name;
    try {
        local_name.resize(local_name_bytes);
    } catch (const std::bad_alloc&) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }
    if (!pread_exact(
            directory.fd.get(),
            entry.local_header_offset + kZipLocalHeaderBytes,
            std::span<std::uint8_t>{
                reinterpret_cast<std::uint8_t*>(local_name.data()),
                local_name.size()}) ||
        local_name != entry.name) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }

    std::uint64_t data_offset = local_fixed_end;
    if (!checked_add(data_offset, local_name_bytes, data_offset) ||
        !checked_add(data_offset, local_extra_bytes, data_offset)) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }
    std::uint64_t data_end = 0U;
    if (!checked_add(data_offset, entry.compressed_size, data_end) ||
        data_end > directory.central_offset) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }

    std::vector<std::uint8_t> compressed;
    try {
        compressed.resize(entry.compressed_size);
    } catch (const std::bad_alloc&) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }
    if (!pread_exact(directory.fd.get(), data_offset, compressed)) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }

    std::vector<std::uint8_t> image;
    if (entry.method == kZipMethodStored) {
        if (entry.compressed_size != entry.uncompressed_size) {
            return source_failure(A32AndroidLibrarySourceError::Failed);
        }
        image = std::move(compressed);
    } else {
        try {
            image.resize(entry.uncompressed_size);
        } catch (const std::bad_alloc&) {
            return source_failure(A32AndroidLibrarySourceError::Failed);
        }
        if (compressed.empty() ||
            compressed.size() >
                static_cast<std::size_t>(
                    std::numeric_limits<uInt>::max()) ||
            image.size() >
                static_cast<std::size_t>(
                    std::numeric_limits<uInt>::max())) {
            return source_failure(A32AndroidLibrarySourceError::Failed);
        }

        z_stream stream{};
        stream.next_in = reinterpret_cast<Bytef*>(compressed.data());
        stream.avail_in = static_cast<uInt>(compressed.size());
        stream.next_out = reinterpret_cast<Bytef*>(image.data());
        stream.avail_out = static_cast<uInt>(image.size());

        if (::inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            return source_failure(A32AndroidLibrarySourceError::Failed);
        }
        const int inflate_status = ::inflate(&stream, Z_FINISH);
        const bool inflated =
            inflate_status == Z_STREAM_END &&
            stream.total_in == compressed.size() &&
            stream.total_out == image.size();
        const int end_status = ::inflateEnd(&stream);
        if (!inflated || end_status != Z_OK) {
            return source_failure(A32AndroidLibrarySourceError::Failed);
        }
    }

    if (!verify_crc(image, entry.crc32)) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }

    A32AndroidLibrarySourceResult result;
    try {
        result.identity.assign(
            virtual_path.data(), virtual_path.size());
    } catch (const std::bad_alloc&) {
        return source_failure(A32AndroidLibrarySourceError::Failed);
    }
    result.image = std::move(image);
    return result;
}

A32ApkLibraryCatalogResult A32ApkLibrarySource::catalog(
    std::string_view apk_path,
    std::string_view abi_directory,
    A32ApkLibraryCatalogOptions options) {
    if (!options_valid(options_) ||
        !catalog_options_valid(options) ||
        apk_path.empty() ||
        contains_nul(apk_path) ||
        apk_path.find("!/") != std::string_view::npos ||
        apk_path.size() > options_.max_virtual_path_bytes ||
        !valid_abi_directory(
            abi_directory, options.max_abi_directory_bytes)) {
        return catalog_failure(A32ApkLibraryCatalogError::Failed);
    }

    if (abi_directory.size() >
        options_.max_entry_name_bytes - 1U) {
        return catalog_failure(A32ApkLibraryCatalogError::Failed);
    }

    ZipDirectory directory =
        read_zip_directory(apk_path, options_);
    if (!directory) {
        return catalog_failure(
            directory.error == A32AndroidLibrarySourceError::NotFound
                ? A32ApkLibraryCatalogError::NotFound
                : A32ApkLibraryCatalogError::Failed);
    }

    std::string prefix;
    try {
        prefix.reserve(abi_directory.size() + 1U);
        prefix.append(abi_directory.data(), abi_directory.size());
        prefix.push_back('/');
    } catch (const std::bad_alloc&) {
        return catalog_failure(A32ApkLibraryCatalogError::Failed);
    }

    A32ApkLibraryCatalogResult result;
    std::uint64_t total_soname_bytes = 0U;
    for (const ZipEntry& entry : directory.entries) {
        if (!entry.name.starts_with(prefix)) {
            continue;
        }
        const std::string_view remainder{
            entry.name.data() + prefix.size(),
            entry.name.size() - prefix.size()};
        if (!bare_shared_object_name(remainder)) {
            continue;
        }
        if (remainder.size() > options.max_soname_bytes) {
            return catalog_failure(A32ApkLibraryCatalogError::Failed);
        }

        std::uint64_t next_total = 0U;
        if (!checked_add(
                total_soname_bytes,
                remainder.size(),
                next_total) ||
            next_total > options.max_total_soname_bytes ||
            result.sonames.size() >= options.max_libraries) {
            return catalog_failure(A32ApkLibraryCatalogError::Failed);
        }

        const bool duplicate = std::any_of(
            result.sonames.begin(),
            result.sonames.end(),
            [&](const std::string& existing) {
                return existing == remainder;
            });
        if (duplicate) {
            return catalog_failure(A32ApkLibraryCatalogError::Failed);
        }

        try {
            result.sonames.emplace_back(remainder);
        } catch (const std::bad_alloc&) {
            return catalog_failure(A32ApkLibraryCatalogError::Failed);
        }
        total_soname_bytes = next_total;
    }

    std::sort(result.sonames.begin(), result.sonames.end());
    return result;
}

}  // namespace liba32android::compat
