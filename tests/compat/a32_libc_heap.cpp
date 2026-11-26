#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <span>

#include "compat/a32_libc_heap.h"
#include "compat/a32_libc_integer.h"
#include "cpu/a32_cpu.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"
#include "runtime/a32_service_registry.h"

namespace {

using liba32android::compat::A32LibcGuestErrnoState;
using liba32android::compat::A32LibcGuestHeap;
using liba32android::compat::A32LibcHeapBlock;
using liba32android::compat::A32LibcHeapOptions;
using liba32android::compat::kA32AndroidEnomem;
using liba32android::compat::kA32AndroidMallocAlignment;
using liba32android::compat::kA32LibcCallocSvcImmediate;
using liba32android::compat::kA32LibcFreeSvcImmediate;
using liba32android::compat::kA32LibcMallocSvcImmediate;
using liba32android::compat::kA32LibcReallocSvcImmediate;
using liba32android::cpu::ExecutionRequest;
using liba32android::memory::LinearGuestMemory;
using liba32android::runtime::A32HostServiceDisposition;
using liba32android::runtime::A32HostServiceRegistry;
using liba32android::runtime::A32HostServiceRegistryEntry;
using liba32android::runtime::execute_a32_with_services;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

std::uint32_t read_u32_le(
    const LinearGuestMemory& memory,
    std::uint32_t address) {
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) {
        return 0xffffffffU;
    }
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

int test_malloc_free_alignment_and_reuse() {
    LinearGuestMemory memory{0x1000, 0x1000U};
    A32LibcGuestErrnoState errno_state{0x1000U};
    std::array<A32LibcHeapBlock, 8> metadata{};
    A32LibcGuestHeap heap{
        errno_state,
        A32LibcHeapOptions{0x1100U, 0x1800U},
        std::span{metadata},
    };

    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    regs[0] = 1U;
    if (heap.handle(memory, kA32LibcMallocSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] == 0U ||
        (regs[0] % kA32AndroidMallocAlignment) != 0U) {
        return fail("malloc did not return aligned logical guest pointer");
    }
    const std::uint32_t first = regs[0];

    regs = {};
    regs[0] = 0U;
    if (heap.handle(memory, kA32LibcMallocSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] == 0U ||
        regs[0] == first) {
        return fail("malloc(0) did not produce a distinct minimum allocation");
    }

    regs = {};
    regs[0] = first;
    if (heap.handle(memory, kA32LibcFreeSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Handled) {
        return fail("free of live allocation failed");
    }

    regs = {};
    regs[0] = 8U;
    if (heap.handle(memory, kA32LibcMallocSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != first) {
        return fail("first-fit heap did not reuse freed leading span");
    }

    regs = {};
    regs[0] = 0U;
    if (heap.handle(memory, kA32LibcFreeSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Handled) {
        return fail("free(nullptr) was not a no-op");
    }
    return 0;
}

int test_calloc_and_enomem() {
    LinearGuestMemory memory{0x1000, 0x1000U};
    A32LibcGuestErrnoState errno_state{0x1000U};
    std::array<A32LibcHeapBlock, 4> metadata{};
    A32LibcGuestHeap heap{
        errno_state,
        A32LibcHeapOptions{0x1100U, 0x1200U},
        std::span{metadata},
    };

    constexpr std::array<std::uint8_t, 32> dirty{
        0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
        0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
        0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
        0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
    };
    if (!memory.write(0x1100U, dirty)) {
        return fail("could not dirty calloc arena");
    }

    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    regs[0] = 3U;
    regs[1] = 5U;
    if (heap.handle(memory, kA32LibcCallocSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0x1100U) {
        return fail("calloc allocation failed");
    }
    std::array<std::uint8_t, 15> zeros{};
    std::array<std::uint8_t, 15> observed{};
    if (!memory.read(0x1100U, observed) || observed != zeros) {
        return fail("calloc did not clear requested bytes");
    }

    regs = {};
    regs[0] = 0xffffffffU;
    regs[1] = 2U;
    if (heap.handle(memory, kA32LibcCallocSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        read_u32_le(memory, 0x1000U) !=
            static_cast<std::uint32_t>(kA32AndroidEnomem)) {
        return fail("calloc multiplication overflow did not publish ENOMEM");
    }
    return 0;
}

int test_realloc_semantics() {
    LinearGuestMemory memory{0x1000, 0x1000U};
    A32LibcGuestErrnoState errno_state{0x1000U};
    std::array<A32LibcHeapBlock, 8> metadata{};
    A32LibcGuestHeap heap{
        errno_state,
        A32LibcHeapOptions{0x1100U, 0x1300U},
        std::span{metadata},
    };

    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    regs[0] = 8U;
    if (heap.handle(memory, kA32LibcMallocSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Handled) {
        return fail("could not create realloc source");
    }
    const std::uint32_t old = regs[0];
    constexpr std::array<std::uint8_t, 8> payload{
        1,2,3,4,5,6,7,8,
    };
    if (!memory.write(old, payload)) {
        return fail("could not stage realloc payload");
    }

    regs = {};
    regs[0] = old;
    regs[1] = 40U;
    if (heap.handle(memory, kA32LibcReallocSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] == 0U ||
        regs[0] == old) {
        return fail("growing realloc did not allocate a new block");
    }
    const std::uint32_t grown = regs[0];
    std::array<std::uint8_t, 8> copied{};
    if (!memory.read(grown, copied) || copied != payload) {
        return fail("growing realloc did not preserve old payload");
    }

    regs = {};
    regs[0] = grown;
    regs[1] = 4U;
    if (heap.handle(memory, kA32LibcReallocSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != grown) {
        return fail("shrinking realloc did not preserve pointer");
    }

    regs = {};
    regs[0] = grown;
    regs[1] = 0U;
    if (heap.handle(memory, kA32LibcReallocSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("realloc(ptr,0) did not free and return null");
    }

    regs = {};
    regs[0] = 0U;
    regs[1] = 0U;
    if (heap.handle(memory, kA32LibcReallocSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] == 0U) {
        return fail("realloc(nullptr,0) did not follow malloc(0) path");
    }
    return 0;
}

int test_failure_preserves_old_allocation() {
    LinearGuestMemory memory{0x1000, 0x1000U};
    A32LibcGuestErrnoState errno_state{0x1000U};
    std::array<A32LibcHeapBlock, 2> metadata{};
    A32LibcGuestHeap heap{
        errno_state,
        A32LibcHeapOptions{0x1100U, 0x1120U},
        std::span{metadata},
    };
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    regs[0] = 8U;
    if (heap.handle(memory, kA32LibcMallocSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Handled) {
        return fail("could not allocate first tiny-heap block");
    }
    const std::uint32_t first = regs[0];
    constexpr std::array<std::uint8_t, 4> payload{{9,8,7,6}};
    if (!memory.write(first, payload)) {
        return fail("could not stage tiny-heap payload");
    }

    regs = {};
    regs[0] = 8U;
    if (heap.handle(memory, kA32LibcMallocSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Handled) {
        return fail("could not allocate second tiny-heap block");
    }

    regs = {};
    regs[0] = first;
    regs[1] = 32U;
    if (heap.handle(memory, kA32LibcReallocSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        read_u32_le(memory, 0x1000U) !=
            static_cast<std::uint32_t>(kA32AndroidEnomem)) {
        return fail("failed realloc did not return null/ENOMEM");
    }

    std::array<std::uint8_t, 4> after{};
    if (!memory.read(first, after) || after != payload) {
        return fail("failed realloc did not preserve old allocation");
    }

    regs = {};
    regs[0] = first;
    if (heap.handle(memory, kA32LibcFreeSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Handled) {
        return fail("failed realloc released old allocation");
    }
    return 0;
}

int test_invalid_pointer_fail_stop() {
    LinearGuestMemory memory{0x1000, 0x1000U};
    A32LibcGuestErrnoState errno_state{0x1000U};
    std::array<A32LibcHeapBlock, 4> metadata{};
    A32LibcGuestHeap heap{
        errno_state,
        A32LibcHeapOptions{0x1100U, 0x1200U},
        std::span{metadata},
    };
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    regs[0] = 0x1110U;
    if (heap.handle(memory, kA32LibcFreeSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Failed) {
        return fail("free of unknown non-null pointer did not fail-stop");
    }

    regs = {};
    regs[0] = 0x1110U;
    regs[1] = 8U;
    if (heap.handle(memory, kA32LibcReallocSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Failed) {
        return fail("realloc of unknown pointer did not fail-stop");
    }

    regs = {};
    if (heap.handle(memory, 0xB2U, regs, cpsr) !=
        A32HostServiceDisposition::Unhandled) {
        return fail("unknown heap service ID was not unhandled");
    }
    return 0;
}

int test_arm_registry_integration() {
    constexpr std::array<std::uint8_t, 8> code{
        0xAE, 0x00, 0x00, 0xEF,
        0x1E, 0xFF, 0x2F, 0xE1,
    };

    LinearGuestMemory memory{4096};
    if (!memory.write(0U, code)) {
        return fail("could not stage malloc ARM fixture");
    }

    A32LibcGuestErrnoState errno_state{0x100U};
    std::array<A32LibcHeapBlock, 4> metadata{};
    A32LibcGuestHeap heap{
        errno_state,
        A32LibcHeapOptions{0x200U, 0x600U},
        std::span{metadata},
    };
    const std::array<A32HostServiceRegistryEntry, 1> entries{{
        {kA32LibcMallocSvcImmediate, &heap},
    }};
    A32HostServiceRegistry registry{std::span{entries}};

    ExecutionRequest request{};
    request.regs[0] = 24U;
    request.regs[14] = 4096U;
    request.instruction_count = 2;
    request.stop_pc = 4096U;

    const auto result =
        execute_a32_with_services(memory, request, registry, 1);
    if (!result || !result.stop_pc_reached ||
        result.services_handled != 1 ||
        result.regs[0] < 0x200U ||
        (result.regs[0] % kA32AndroidMallocAlignment) != 0U) {
        return fail("ARM malloc SVC did not compose through registry/dispatcher");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_malloc_free_alignment_and_reuse(); status != 0) {
        return status;
    }
    if (const int status = test_calloc_and_enomem(); status != 0) {
        return status;
    }
    if (const int status = test_realloc_semantics(); status != 0) {
        return status;
    }
    if (const int status = test_failure_preserves_old_allocation(); status != 0) {
        return status;
    }
    if (const int status = test_invalid_pointer_fail_stop(); status != 0) {
        return status;
    }
    if (const int status = test_arm_registry_integration(); status != 0) {
        return status;
    }
    return 0;
}
