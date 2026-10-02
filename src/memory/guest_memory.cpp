#include "memory/guest_memory.h"

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace liba32android::memory {
namespace {

constexpr std::uint64_t kGuestAddressSpaceSize = std::uint64_t{1} << 32;
constexpr std::uint8_t kMappedBit = 1U << 7;
constexpr std::uint8_t kPermissionMask =
    static_cast<std::uint8_t>(MemoryPermission::Read) |
    static_cast<std::uint8_t>(MemoryPermission::Write) |
    static_cast<std::uint8_t>(MemoryPermission::Execute);

[[nodiscard]] std::uint8_t encode_page_state(MemoryPermission permissions) noexcept {
    return kMappedBit | static_cast<std::uint8_t>(permissions);
}

[[nodiscard]] bool page_is_mapped(std::uint8_t state) noexcept {
    return (state & kMappedBit) != 0;
}

[[nodiscard]] MemoryPermission decode_permissions(std::uint8_t state) noexcept {
    return static_cast<MemoryPermission>(state & kPermissionMask);
}

[[nodiscard]] bool guest_range_fits(
    std::uint32_t address,
    std::size_t length) noexcept {
    return static_cast<std::uint64_t>(length) <=
           kGuestAddressSpaceSize - static_cast<std::uint64_t>(address);
}

[[nodiscard]] std::int32_t normalize_compare_result(int result) noexcept {
    if (result < 0) return -1;
    if (result > 0) return 1;
    return 0;
}

}  // namespace

bool GuestMemory::copy_bytes(
    std::uint32_t destination,
    std::uint32_t source,
    std::size_t length) {
    if (length == 0) return true;
    if (!guest_range_fits(destination, length) ||
        !guest_range_fits(source, length)) {
        return false;
    }
    std::vector<std::uint8_t> bytes(length);
    return read(source, bytes) && write(destination, bytes);
}

bool GuestMemory::fill_bytes(
    std::uint32_t address,
    std::uint8_t value,
    std::size_t length) {
    if (length == 0) return true;
    if (!guest_range_fits(address, length)) return false;
    const std::vector<std::uint8_t> bytes(length, value);
    return write(address, bytes);
}

bool GuestMemory::compare_bytes(
    std::uint32_t lhs_address,
    std::uint32_t rhs_address,
    std::size_t length,
    std::int32_t& result) const {
    result = 0;
    if (length == 0) return true;
    if (!guest_range_fits(lhs_address, length) ||
        !guest_range_fits(rhs_address, length)) {
        return false;
    }
    std::vector<std::uint8_t> lhs(length);
    std::vector<std::uint8_t> rhs(length);
    if (!read(lhs_address, lhs) || !read(rhs_address, rhs)) return false;
    result = normalize_compare_result(std::memcmp(lhs.data(), rhs.data(), length));
    return true;
}

bool GuestMemory::find_byte(
    std::uint32_t address,
    std::uint8_t value,
    std::size_t length,
    std::optional<std::uint32_t>& found_address) const {
    found_address.reset();
    if (length == 0) return true;
    if (!guest_range_fits(address, length)) return false;
    std::vector<std::uint8_t> bytes(length);
    if (!read(address, bytes)) return false;
    const auto found = std::find(bytes.begin(), bytes.end(), value);
    if (found != bytes.end()) {
        found_address = address + static_cast<std::uint32_t>(
            std::distance(bytes.begin(), found));
    }
    return true;
}

LinearGuestMemory::LinearGuestMemory(std::size_t size, std::uint32_t base)
        : base_{base}, bytes_(size) {
    if (static_cast<std::uint64_t>(size) > kGuestAddressSpaceSize - static_cast<std::uint64_t>(base_)) {
        throw std::invalid_argument("linear guest memory exceeds the AArch32 address space");
    }
}

bool LinearGuestMemory::read(std::uint32_t address, std::span<std::uint8_t> output) const {
    std::size_t offset = 0;
    if (!translate(address, output.size(), offset)) {
        return false;
    }

    std::copy(bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
              bytes_.begin() + static_cast<std::ptrdiff_t>(offset + output.size()), output.begin());
    return true;
}

bool LinearGuestMemory::write(std::uint32_t address, std::span<const std::uint8_t> input) {
    std::size_t offset = 0;
    if (!translate(address, input.size(), offset)) {
        return false;
    }

    std::copy(input.begin(), input.end(), bytes_.begin() + static_cast<std::ptrdiff_t>(offset));
    if (!input.empty()) {
        ++code_generation_;
    }
    return true;
}

bool LinearGuestMemory::copy_bytes(
    std::uint32_t destination,
    std::uint32_t source,
    std::size_t length) {
    if (length == 0) return true;
    std::size_t destination_offset = 0;
    std::size_t source_offset = 0;
    if (!translate(destination, length, destination_offset) ||
        !translate(source, length, source_offset)) {
        return false;
    }
    std::memmove(
        bytes_.data() + destination_offset,
        bytes_.data() + source_offset,
        length);
    ++code_generation_;
    return true;
}

bool LinearGuestMemory::fill_bytes(
    std::uint32_t address,
    std::uint8_t value,
    std::size_t length) {
    if (length == 0) return true;
    std::size_t offset = 0;
    if (!translate(address, length, offset)) return false;
    std::memset(bytes_.data() + offset, value, length);
    ++code_generation_;
    return true;
}

bool LinearGuestMemory::compare_bytes(
    std::uint32_t lhs_address,
    std::uint32_t rhs_address,
    std::size_t length,
    std::int32_t& result) const {
    result = 0;
    if (length == 0) return true;
    std::size_t lhs_offset = 0;
    std::size_t rhs_offset = 0;
    if (!translate(lhs_address, length, lhs_offset) ||
        !translate(rhs_address, length, rhs_offset)) {
        return false;
    }
    result = normalize_compare_result(std::memcmp(
        bytes_.data() + lhs_offset,
        bytes_.data() + rhs_offset,
        length));
    return true;
}

bool LinearGuestMemory::find_byte(
    std::uint32_t address,
    std::uint8_t value,
    std::size_t length,
    std::optional<std::uint32_t>& found_address) const {
    found_address.reset();
    if (length == 0) return true;
    std::size_t offset = 0;
    if (!translate(address, length, offset)) return false;
    const void* found = std::memchr(bytes_.data() + offset, value, length);
    if (found != nullptr) {
        const auto* byte = static_cast<const std::uint8_t*>(found);
        const auto index = static_cast<std::size_t>(byte - bytes_.data());
        found_address = base_ + static_cast<std::uint32_t>(index);
    }
    return true;
}

std::optional<std::uint64_t> LinearGuestMemory::code_generation() const noexcept {
    return code_generation_;
}

std::uint32_t LinearGuestMemory::base() const noexcept {
    return base_;
}

std::size_t LinearGuestMemory::size() const noexcept {
    return bytes_.size();
}

bool LinearGuestMemory::translate(std::uint32_t address, std::size_t length, std::size_t& offset) const noexcept {
    if (address < base_) {
        return false;
    }

    const std::uint64_t translated = static_cast<std::uint64_t>(address) - static_cast<std::uint64_t>(base_);
    if (translated > static_cast<std::uint64_t>(bytes_.size())) {
        return false;
    }

    const auto translated_size = static_cast<std::size_t>(translated);
    if (length > bytes_.size() - translated_size) {
        return false;
    }

    offset = translated_size;
    return true;
}

MappedGuestMemory::MappedGuestMemory() {
    static_assert(sizeof(std::size_t) >= sizeof(std::uint64_t),
                  "MappedGuestMemory requires a 64-bit host address space");

    const long host_page_size = sysconf(_SC_PAGESIZE);
    if (host_page_size <= 0) {
        throw std::system_error(errno != 0 ? errno : EINVAL, std::generic_category(), "sysconf(_SC_PAGESIZE)");
    }

    page_size_ = static_cast<std::size_t>(host_page_size);
    if ((kAddressSpaceSize % page_size_) != 0) {
        throw std::runtime_error("host page size does not divide the AArch32 address space");
    }

    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE;
#endif

    const std::size_t length = static_cast<std::size_t>(kAddressSpaceSize);
    reservation_ = mmap(nullptr, length, PROT_NONE, flags, -1, 0);
    if (reservation_ == MAP_FAILED) {
        reservation_ = nullptr;
        throw std::system_error(errno, std::generic_category(), "reserve 4 GiB guest address space");
    }

    page_state_.resize(length / page_size_, 0);
}

MappedGuestMemory::~MappedGuestMemory() {
    if (reservation_ != nullptr) {
        munmap(reservation_, static_cast<std::size_t>(kAddressSpaceSize));
    }
}

bool MappedGuestMemory::map(std::uint32_t address, std::size_t length, MemoryPermission permissions_value) {
    if (!valid_page_range(address, length) || !valid_permissions(permissions_value)) {
        return false;
    }

    const std::size_t first = page_index(address);
    const std::size_t count = length / page_size_;
    for (std::size_t index = 0; index < count; ++index) {
        if (page_is_mapped(page_state_[first + index])) {
            return false;
        }
    }

    if (mprotect(host_address(address), length, host_protection(permissions_value)) != 0) {
        return false;
    }

    const std::uint8_t state = encode_page_state(permissions_value);
    std::fill_n(page_state_.begin() + static_cast<std::ptrdiff_t>(first),
                static_cast<std::ptrdiff_t>(count), state);
    if (has_permission(permissions_value, MemoryPermission::Write) &&
        has_permission(permissions_value, MemoryPermission::Execute)) {
        writable_executable_pages_ += count;
    }
    if (has_permission(permissions_value, MemoryPermission::Execute)) {
        ++code_generation_;
    }
    return true;
}

bool MappedGuestMemory::protect(std::uint32_t address, std::size_t length,
                                MemoryPermission permissions_value) {
    if (!valid_page_range(address, length) || !valid_permissions(permissions_value)) {
        return false;
    }

    const std::size_t first = page_index(address);
    const std::size_t count = length / page_size_;
    std::size_t old_writable_executable = 0;
    bool affects_code = has_permission(permissions_value, MemoryPermission::Execute);
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint8_t old_state = page_state_[first + index];
        if (!page_is_mapped(old_state)) {
            return false;
        }
        const MemoryPermission old_permissions = decode_permissions(old_state);
        affects_code = affects_code ||
                       has_permission(old_permissions, MemoryPermission::Execute);
        if (has_permission(old_permissions, MemoryPermission::Write) &&
            has_permission(old_permissions, MemoryPermission::Execute)) {
            ++old_writable_executable;
        }
    }

    if (mprotect(host_address(address), length, host_protection(permissions_value)) != 0) {
        return false;
    }

    const std::uint8_t state = encode_page_state(permissions_value);
    std::fill_n(page_state_.begin() + static_cast<std::ptrdiff_t>(first),
                static_cast<std::ptrdiff_t>(count), state);
    writable_executable_pages_ -= old_writable_executable;
    if (has_permission(permissions_value, MemoryPermission::Write) &&
        has_permission(permissions_value, MemoryPermission::Execute)) {
        writable_executable_pages_ += count;
    }
    if (affects_code) {
        ++code_generation_;
    }
    return true;
}

bool MappedGuestMemory::unmap(std::uint32_t address, std::size_t length) {
    if (!valid_page_range(address, length)) {
        return false;
    }

    const std::size_t first = page_index(address);
    const std::size_t count = length / page_size_;
    std::size_t old_writable_executable = 0;
    bool affects_code = false;
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint8_t old_state = page_state_[first + index];
        if (!page_is_mapped(old_state)) {
            return false;
        }
        const MemoryPermission old_permissions = decode_permissions(old_state);
        affects_code = affects_code ||
                       has_permission(old_permissions, MemoryPermission::Execute);
        if (has_permission(old_permissions, MemoryPermission::Write) &&
            has_permission(old_permissions, MemoryPermission::Execute)) {
            ++old_writable_executable;
        }
    }

    void* const host = host_address(address);
#ifdef MADV_DONTNEED
    if (madvise(host, length, MADV_DONTNEED) != 0) {
        return false;
    }
#endif
    if (mprotect(host, length, PROT_NONE) != 0) {
        return false;
    }

    std::fill_n(page_state_.begin() + static_cast<std::ptrdiff_t>(first),
                static_cast<std::ptrdiff_t>(count), 0);
    writable_executable_pages_ -= old_writable_executable;
    if (affects_code) {
        ++code_generation_;
    }
    return true;
}

bool MappedGuestMemory::read(std::uint32_t address, std::span<std::uint8_t> output) const {
    if (!valid_access(address, output.size(), MemoryPermission::Read)) {
        return false;
    }
    if (!output.empty()) {
        std::memcpy(output.data(), host_address(address), output.size());
    }
    return true;
}

bool MappedGuestMemory::read_code(std::uint32_t address, std::span<std::uint8_t> output) const {
    if (!valid_access(address, output.size(), MemoryPermission::Execute)) {
        return false;
    }
    if (!output.empty()) {
        std::memcpy(output.data(), host_address(address), output.size());
    }
    return true;
}

bool MappedGuestMemory::write(std::uint32_t address, std::span<const std::uint8_t> input) {
    if (!valid_access(address, input.size(), MemoryPermission::Write)) {
        return false;
    }
    const bool affects_code =
        !input.empty() && range_has_execute(address, input.size());
    if (!input.empty()) {
        std::memcpy(host_address(address), input.data(), input.size());
    }
    if (affects_code) {
        ++code_generation_;
    }
    return true;
}

bool MappedGuestMemory::copy_bytes(
    std::uint32_t destination,
    std::uint32_t source,
    std::size_t length) {
    if (length == 0) return true;
    if (!valid_access(source, length, MemoryPermission::Read) ||
        !valid_access(destination, length, MemoryPermission::Write)) {
        return false;
    }
    const bool affects_code = range_has_execute(destination, length);
    std::memmove(host_address(destination), host_address(source), length);
    if (affects_code) ++code_generation_;
    return true;
}

bool MappedGuestMemory::fill_bytes(
    std::uint32_t address,
    std::uint8_t value,
    std::size_t length) {
    if (length == 0) return true;
    if (!valid_access(address, length, MemoryPermission::Write)) return false;
    const bool affects_code = range_has_execute(address, length);
    std::memset(host_address(address), value, length);
    if (affects_code) ++code_generation_;
    return true;
}

bool MappedGuestMemory::compare_bytes(
    std::uint32_t lhs_address,
    std::uint32_t rhs_address,
    std::size_t length,
    std::int32_t& result) const {
    result = 0;
    if (length == 0) return true;
    if (!valid_access(lhs_address, length, MemoryPermission::Read) ||
        !valid_access(rhs_address, length, MemoryPermission::Read)) {
        return false;
    }
    result = normalize_compare_result(std::memcmp(
        host_address(lhs_address),
        host_address(rhs_address),
        length));
    return true;
}

bool MappedGuestMemory::find_byte(
    std::uint32_t address,
    std::uint8_t value,
    std::size_t length,
    std::optional<std::uint32_t>& found_address) const {
    found_address.reset();
    if (length == 0) return true;
    if (!valid_access(address, length, MemoryPermission::Read)) return false;
    const void* found = std::memchr(host_address(address), value, length);
    if (found != nullptr) {
        const auto* start = static_cast<const std::uint8_t*>(host_address(address));
        const auto* byte = static_cast<const std::uint8_t*>(found);
        found_address = address + static_cast<std::uint32_t>(byte - start);
    }
    return true;
}

std::optional<std::uintptr_t> MappedGuestMemory::fastmem_base() const noexcept {
    if (reservation_ == nullptr) {
        return std::nullopt;
    }
    return reinterpret_cast<std::uintptr_t>(reservation_);
}

std::optional<std::uint64_t> MappedGuestMemory::code_generation() const noexcept {
    return code_generation_;
}

bool MappedGuestMemory::direct_executable_writes_possible() const noexcept {
    return writable_executable_pages_ != 0;
}

std::size_t MappedGuestMemory::page_size() const noexcept {
    return page_size_;
}

bool MappedGuestMemory::is_mapped(std::uint32_t address) const noexcept {
    return page_is_mapped(page_state_[page_index(address)]);
}

MemoryPermission MappedGuestMemory::permissions(std::uint32_t address) const noexcept {
    const std::uint8_t state = page_state_[page_index(address)];
    if (!page_is_mapped(state)) {
        return MemoryPermission::None;
    }
    return decode_permissions(state);
}

bool MappedGuestMemory::valid_page_range(std::uint32_t address, std::size_t length) const noexcept {
    if (length == 0 || page_size_ == 0 || (static_cast<std::uint64_t>(address) % page_size_) != 0 ||
        (length % page_size_) != 0) {
        return false;
    }

    const std::uint64_t end = static_cast<std::uint64_t>(address) + static_cast<std::uint64_t>(length);
    return end <= kAddressSpaceSize;
}

bool MappedGuestMemory::valid_access(std::uint32_t address, std::size_t length,
                                     MemoryPermission required) const noexcept {
    if (length == 0) {
        return true;
    }

    const std::uint64_t end = static_cast<std::uint64_t>(address) + static_cast<std::uint64_t>(length);
    if (end > kAddressSpaceSize) {
        return false;
    }

    const std::size_t first = page_index(address);
    const std::size_t last = static_cast<std::size_t>((end - 1) / page_size_);
    for (std::size_t index = first; index <= last; ++index) {
        const std::uint8_t state = page_state_[index];
        if (!page_is_mapped(state) || !has_permission(decode_permissions(state), required)) {
            return false;
        }
    }
    return true;
}

bool MappedGuestMemory::range_has_execute(
    std::uint32_t address,
    std::size_t length) const noexcept {
    if (length == 0) {
        return false;
    }
    const std::uint64_t end =
        static_cast<std::uint64_t>(address) + static_cast<std::uint64_t>(length);
    if (end > kAddressSpaceSize) {
        return false;
    }
    const std::size_t first = page_index(address);
    const std::size_t last = static_cast<std::size_t>((end - 1) / page_size_);
    for (std::size_t index = first; index <= last; ++index) {
        const std::uint8_t state = page_state_[index];
        if (page_is_mapped(state) &&
            has_permission(decode_permissions(state), MemoryPermission::Execute)) {
            return true;
        }
    }
    return false;
}

bool MappedGuestMemory::valid_permissions(MemoryPermission permissions_value) const noexcept {
    const std::uint8_t raw = static_cast<std::uint8_t>(permissions_value);
    if ((raw & ~kPermissionMask) != 0) {
        return false;
    }

    // The first fastmem backend intentionally supports the permission shapes
    // used by normal ELF PT_LOAD segments. Requiring read alongside write or
    // execute prevents host-page permissions from accidentally granting a
    // guest data read that the metadata would deny on hosts where W implies R.
    if ((has_permission(permissions_value, MemoryPermission::Write) ||
         has_permission(permissions_value, MemoryPermission::Execute)) &&
        !has_permission(permissions_value, MemoryPermission::Read)) {
        return false;
    }
    return true;
}

int MappedGuestMemory::host_protection(MemoryPermission permissions_value) const noexcept {
    int protection = PROT_NONE;
    if (has_permission(permissions_value, MemoryPermission::Read) ||
        has_permission(permissions_value, MemoryPermission::Execute)) {
        protection |= PROT_READ;
    }
    if (has_permission(permissions_value, MemoryPermission::Write)) {
        protection |= PROT_WRITE;
    }
    return protection;
}

std::size_t MappedGuestMemory::page_index(std::uint32_t address) const noexcept {
    return static_cast<std::size_t>(static_cast<std::uint64_t>(address) / page_size_);
}

void* MappedGuestMemory::host_address(std::uint32_t address) const noexcept {
    auto* const base = static_cast<std::uint8_t*>(reservation_);
    return base + static_cast<std::size_t>(address);
}

}  // namespace liba32android::memory
