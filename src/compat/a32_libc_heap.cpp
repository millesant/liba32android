#include "compat/a32_libc_heap.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "memory/guest_memory.h"

namespace liba32android::compat {
namespace {

using runtime::A32HostServiceDisposition;

constexpr std::size_t kTransferChunkBytes = 4096;

[[nodiscard]] std::uint64_t align_up_16(std::uint64_t value) noexcept {
    constexpr std::uint64_t mask =
        static_cast<std::uint64_t>(kA32AndroidMallocAlignment - 1U);
    return (value + mask) & ~mask;
}

}  // namespace

A32LibcGuestHeap::A32LibcGuestHeap(
    A32LibcErrnoSink& errno_sink,
    A32LibcHeapOptions options,
    std::span<A32LibcHeapBlock> metadata) noexcept
    : errno_sink_(errno_sink),
      options_(options),
      metadata_(metadata) {
    for (A32LibcHeapBlock& block : metadata_) {
        block = {};
    }
}

bool A32LibcGuestHeap::configuration_valid() const noexcept {
    constexpr std::uint64_t address_space_size =
        std::uint64_t{1} << 32;
    return options_.begin != 0U &&
           static_cast<std::uint64_t>(options_.begin) <
               options_.end_exclusive &&
           options_.end_exclusive <= address_space_size &&
           !metadata_.empty();
}

bool A32LibcGuestHeap::publish_enomem(
    memory::GuestMemory& memory,
    std::array<std::uint32_t, 16>& regs) noexcept {
    if (!errno_sink_.set_errno(memory, kA32AndroidEnomem)) {
        return false;
    }
    regs[0] = 0U;
    return true;
}

bool A32LibcGuestHeap::normalized_reserved_size(
    std::uint32_t requested_size,
    std::uint32_t& reserved_size) const noexcept {
    const std::uint64_t payload =
        requested_size == 0U ? 1U : requested_size;
    const std::uint64_t aligned = align_up_16(payload);
    if (aligned == 0U ||
        aligned > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    reserved_size = static_cast<std::uint32_t>(aligned);
    return true;
}

bool A32LibcGuestHeap::allocate_block(
    std::uint32_t requested_size,
    std::uint32_t& address) noexcept {
    if (allocation_count_ >= metadata_.size()) {
        return false;
    }

    std::uint32_t reserved_size{};
    if (!normalized_reserved_size(requested_size, reserved_size)) {
        return false;
    }

    std::uint64_t candidate =
        align_up_16(static_cast<std::uint64_t>(options_.begin));
    std::size_t insertion = 0;

    for (; insertion < allocation_count_; ++insertion) {
        const A32LibcHeapBlock& block = metadata_[insertion];
        const std::uint64_t block_address = block.address;
        if (candidate <= block_address &&
            reserved_size <= block_address - candidate) {
            break;
        }

        const std::uint64_t block_end =
            block_address + block.reserved_size;
        if (block_end > candidate) {
            candidate = align_up_16(block_end);
        }
    }

    if (candidate == 0U ||
        candidate >= options_.end_exclusive ||
        reserved_size > options_.end_exclusive - candidate ||
        candidate >
            std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    for (std::size_t index = allocation_count_;
         index > insertion;
         --index) {
        metadata_[index] = metadata_[index - 1U];
    }

    metadata_[insertion] = A32LibcHeapBlock{
        .address = static_cast<std::uint32_t>(candidate),
        .reserved_size = reserved_size,
        .requested_size = requested_size,
    };
    ++allocation_count_;
    address = static_cast<std::uint32_t>(candidate);
    return true;
}

std::size_t A32LibcGuestHeap::find_block(
    std::uint32_t address) const noexcept {
    for (std::size_t index = 0; index < allocation_count_; ++index) {
        if (metadata_[index].address == address) {
            return index;
        }
    }
    return metadata_.size();
}

bool A32LibcGuestHeap::free_block(std::uint32_t address) noexcept {
    const std::size_t index = find_block(address);
    if (index >= allocation_count_) {
        return false;
    }

    for (std::size_t next = index + 1U;
         next < allocation_count_;
         ++next) {
        metadata_[next - 1U] = metadata_[next];
    }
    --allocation_count_;
    metadata_[allocation_count_] = {};
    return true;
}

bool A32LibcGuestHeap::zero_guest_range(
    memory::GuestMemory& memory,
    std::uint32_t address,
    std::uint32_t size) const {
    constexpr std::array<std::uint8_t, kTransferChunkBytes> zeros{};
    std::uint32_t offset = 0U;
    while (offset < size) {
        const std::uint32_t remaining = size - offset;
        const std::size_t chunk = std::min<std::size_t>(
            zeros.size(), remaining);
        if (!memory.write(
                address + offset,
                std::span<const std::uint8_t>{zeros.data(), chunk})) {
            return false;
        }
        offset += static_cast<std::uint32_t>(chunk);
    }
    return true;
}

bool A32LibcGuestHeap::copy_guest_range(
    memory::GuestMemory& memory,
    std::uint32_t destination,
    std::uint32_t source,
    std::uint32_t size) const {
    std::array<std::uint8_t, kTransferChunkBytes> bytes{};
    std::uint32_t offset = 0U;
    while (offset < size) {
        const std::uint32_t remaining = size - offset;
        const std::size_t chunk = std::min<std::size_t>(
            bytes.size(), remaining);
        std::span<std::uint8_t> output{bytes.data(), chunk};
        if (!memory.read(source + offset, output) ||
            !memory.write(
                destination + offset,
                std::span<const std::uint8_t>{bytes.data(), chunk})) {
            return false;
        }
        offset += static_cast<std::uint32_t>(chunk);
    }
    return true;
}

runtime::A32HostServiceDisposition A32LibcGuestHeap::handle(
    memory::GuestMemory& memory,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    if (svc_immediate != kA32LibcMallocSvcImmediate &&
        svc_immediate != kA32LibcCallocSvcImmediate &&
        svc_immediate != kA32LibcReallocSvcImmediate &&
        svc_immediate != kA32LibcFreeSvcImmediate) {
        return A32HostServiceDisposition::Unhandled;
    }
    if (!configuration_valid()) {
        return A32HostServiceDisposition::Failed;
    }

    if (svc_immediate == kA32LibcMallocSvcImmediate) {
        std::uint32_t address{};
        if (!allocate_block(regs[0], address)) {
            return publish_enomem(memory, regs)
                ? A32HostServiceDisposition::Handled
                : A32HostServiceDisposition::Failed;
        }
        regs[0] = address;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32LibcCallocSvcImmediate) {
        const std::uint64_t product =
            static_cast<std::uint64_t>(regs[0]) *
            static_cast<std::uint64_t>(regs[1]);
        if (product > std::numeric_limits<std::uint32_t>::max()) {
            return publish_enomem(memory, regs)
                ? A32HostServiceDisposition::Handled
                : A32HostServiceDisposition::Failed;
        }

        const auto requested = static_cast<std::uint32_t>(product);
        std::uint32_t address{};
        if (!allocate_block(requested, address)) {
            return publish_enomem(memory, regs)
                ? A32HostServiceDisposition::Handled
                : A32HostServiceDisposition::Failed;
        }
        if (requested != 0U &&
            !zero_guest_range(memory, address, requested)) {
            static_cast<void>(free_block(address));
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = address;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32LibcFreeSvcImmediate) {
        const std::uint32_t address = regs[0];
        if (address == 0U) {
            return A32HostServiceDisposition::Handled;
        }
        return free_block(address)
            ? A32HostServiceDisposition::Handled
            : A32HostServiceDisposition::Failed;
    }

    const std::uint32_t old_address = regs[0];
    const std::uint32_t requested = regs[1];

    if (old_address == 0U) {
        std::uint32_t address{};
        if (!allocate_block(requested, address)) {
            return publish_enomem(memory, regs)
                ? A32HostServiceDisposition::Handled
                : A32HostServiceDisposition::Failed;
        }
        regs[0] = address;
        return A32HostServiceDisposition::Handled;
    }

    const std::size_t old_index = find_block(old_address);
    if (old_index >= allocation_count_) {
        return A32HostServiceDisposition::Failed;
    }

    if (requested == 0U) {
        static_cast<void>(free_block(old_address));
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    std::uint32_t new_reserved{};
    if (!normalized_reserved_size(requested, new_reserved)) {
        return publish_enomem(memory, regs)
            ? A32HostServiceDisposition::Handled
            : A32HostServiceDisposition::Failed;
    }

    const A32LibcHeapBlock old = metadata_[old_index];
    if (new_reserved <= old.reserved_size) {
        metadata_[old_index].requested_size = requested;
        regs[0] = old_address;
        return A32HostServiceDisposition::Handled;
    }

    std::uint32_t new_address{};
    if (!allocate_block(requested, new_address)) {
        return publish_enomem(memory, regs)
            ? A32HostServiceDisposition::Handled
            : A32HostServiceDisposition::Failed;
    }

    const std::uint32_t copy_size =
        std::min(old.requested_size, requested);
    if (copy_size != 0U &&
        !copy_guest_range(
            memory, new_address, old_address, copy_size)) {
        static_cast<void>(free_block(new_address));
        return A32HostServiceDisposition::Failed;
    }

    if (!free_block(old_address)) {
        static_cast<void>(free_block(new_address));
        return A32HostServiceDisposition::Failed;
    }

    regs[0] = new_address;
    return A32HostServiceDisposition::Handled;
}

}  // namespace liba32android::compat
