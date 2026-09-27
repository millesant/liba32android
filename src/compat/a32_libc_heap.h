#pragma once

#define LIBA32ANDROID_A32_LIBC_MALLOC_SVC 0xAE
#define LIBA32ANDROID_A32_LIBC_CALLOC_SVC 0xAF
#define LIBA32ANDROID_A32_LIBC_REALLOC_SVC 0xB0
#define LIBA32ANDROID_A32_LIBC_FREE_SVC 0xB1

#ifdef __cplusplus

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "compat/a32_libc_integer.h"
#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

inline constexpr std::uint32_t kA32LibcMallocSvcImmediate =
    LIBA32ANDROID_A32_LIBC_MALLOC_SVC;
inline constexpr std::uint32_t kA32LibcCallocSvcImmediate =
    LIBA32ANDROID_A32_LIBC_CALLOC_SVC;
inline constexpr std::uint32_t kA32LibcReallocSvcImmediate =
    LIBA32ANDROID_A32_LIBC_REALLOC_SVC;
inline constexpr std::uint32_t kA32LibcFreeSvcImmediate =
    LIBA32ANDROID_A32_LIBC_FREE_SVC;
inline constexpr std::int32_t kA32AndroidEnomem = 12;
inline constexpr std::uint32_t kA32AndroidMallocAlignment = 16;

struct A32LibcHeapBlock {
    std::uint32_t address{};
    std::uint32_t reserved_size{};
    std::uint32_t requested_size{};
};

struct A32LibcHeapOptions {
    std::uint32_t begin{};
    std::uint64_t end_exclusive{};
};

// Bounded single-context guest heap over a caller-provided, already mapped
// writable guest-memory arena. Metadata storage and errno state are borrowed
// for the entire heap lifetime. The heap owns no guest mapping and exposes no
// host pointer as a guest pointer.
class A32LibcGuestHeap final
    : public runtime::A32HostServiceHandler {
public:
    A32LibcGuestHeap(
        A32LibcErrnoSink& errno_sink,
        A32LibcHeapOptions options,
        std::span<A32LibcHeapBlock> metadata) noexcept;

    A32LibcGuestHeap(const A32LibcGuestHeap&) = delete;
    A32LibcGuestHeap& operator=(const A32LibcGuestHeap&) = delete;
    A32LibcGuestHeap(A32LibcGuestHeap&&) = delete;
    A32LibcGuestHeap& operator=(A32LibcGuestHeap&&) = delete;

    [[nodiscard]] runtime::A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;

    [[nodiscard]] A32LibcHeapOptions options() const noexcept {
        return options_;
    }

    [[nodiscard]] std::size_t allocation_count() const noexcept {
        return allocation_count_;
    }

private:
    [[nodiscard]] bool configuration_valid() const noexcept;
    [[nodiscard]] bool publish_enomem(
        memory::GuestMemory& memory,
        std::array<std::uint32_t, 16>& regs) noexcept;
    [[nodiscard]] bool normalized_reserved_size(
        std::uint32_t requested_size,
        std::uint32_t& reserved_size) const noexcept;
    [[nodiscard]] bool allocate_block(
        std::uint32_t requested_size,
        std::uint32_t& address) noexcept;
    [[nodiscard]] bool free_block(std::uint32_t address) noexcept;
    [[nodiscard]] std::size_t find_block(
        std::uint32_t address) const noexcept;
    [[nodiscard]] bool zero_guest_range(
        memory::GuestMemory& memory,
        std::uint32_t address,
        std::uint32_t size) const;
    [[nodiscard]] bool copy_guest_range(
        memory::GuestMemory& memory,
        std::uint32_t destination,
        std::uint32_t source,
        std::uint32_t size) const;

    A32LibcErrnoSink& errno_sink_;
    A32LibcHeapOptions options_;
    std::span<A32LibcHeapBlock> metadata_;
    std::size_t allocation_count_{};
};

}  // namespace liba32android::compat

#endif
