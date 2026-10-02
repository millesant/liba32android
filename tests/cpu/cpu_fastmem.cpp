#include <array>
#include <cstdint>
#include <iostream>
#include <span>

#include "cpu/a32_cpu.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::cpu::ExecutionRequest;
using liba32android::memory::LinearGuestMemory;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;

constexpr std::array<std::uint8_t, 8> kLoadStoreCode{
    0x00, 0x00, 0x81, 0xE5,  // str r0,[r1]
    0x00, 0x20, 0x91, 0xE5,  // ldr r2,[r1]
};

constexpr std::array<std::uint8_t, 4> kFaultingLoadCode{
    0x00, 0x00, 0x91, 0xE5,  // ldr r0,[r1]
};

bool read_u32(const liba32android::memory::GuestMemory& memory, std::uint32_t address,
              std::uint32_t& value) {
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) {
        return false;
    }
    value = static_cast<std::uint32_t>(bytes[0]) |
            static_cast<std::uint32_t>(bytes[1]) << 8 |
            static_cast<std::uint32_t>(bytes[2]) << 16 |
            static_cast<std::uint32_t>(bytes[3]) << 24;
    return true;
}

int run_linear_baseline() {
    LinearGuestMemory memory{8192};
    if (!memory.write(0, kLoadStoreCode)) {
        return 1;
    }

    ExecutionRequest request{};
    request.regs[0] = 0x12345678;
    request.regs[1] = 0x1000;
    request.instruction_count = 2;

    const auto result = liba32android::cpu::execute(memory, request);
    std::uint32_t stored = 0;
    if (result.fastmem_enabled || result.memory_fault || result.exception_raised ||
        result.regs[2] != 0x12345678 || !read_u32(memory, 0x1000, stored) ||
        stored != 0x12345678) {
        std::cerr << "callback baseline failed\n";
        return 1;
    }
    if (result.data_read_callbacks == 0 || result.data_write_callbacks == 0) {
        std::cerr << "callback baseline did not use data callbacks\n";
        return 1;
    }
    return 0;
}

int prepare_mapped_code(MappedGuestMemory& memory, std::span<const std::uint8_t> code) {
    const std::size_t page = memory.page_size();
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    const auto rx = MemoryPermission::Read | MemoryPermission::Execute;
    if (!memory.map(0, page, rw) || !memory.write(0, code) || !memory.protect(0, page, rx)) {
        std::cerr << "failed to prepare mapped code page\n";
        return 1;
    }
    return 0;
}

int run_fastmem_load_store() {
    MappedGuestMemory memory;
    if (prepare_mapped_code(memory, kLoadStoreCode) != 0) {
        return 1;
    }

    const std::uint32_t data = static_cast<std::uint32_t>(memory.page_size());
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    if (!memory.map(data, memory.page_size(), rw)) {
        std::cerr << "failed to map fastmem data page\n";
        return 1;
    }

    ExecutionRequest request{};
    request.regs[0] = 0x12345678;
    request.regs[1] = data;
    request.instruction_count = 2;

    const auto result = liba32android::cpu::execute(memory, request);
    std::uint32_t stored = 0;
    if (!result.fastmem_enabled || result.memory_fault || result.exception_raised ||
        result.regs[2] != 0x12345678 || !read_u32(memory, data, stored) ||
        stored != 0x12345678) {
        std::cerr << "fastmem load/store failed: enabled=" << result.fastmem_enabled
                  << " fault=" << result.memory_fault << " exception=" << result.exception_raised
                  << " r2=0x" << std::hex << result.regs[2] << " stored=0x" << stored
                  << std::dec << '\n';
        return 1;
    }

    // A mapped fastmem access should not need the data callbacks. Instruction
    // translation still uses read_code and is counted separately.
    if (result.data_read_callbacks != 0 || result.data_write_callbacks != 0) {
        std::cerr << "mapped access unexpectedly used data callbacks: reads="
                  << result.data_read_callbacks << " writes=" << result.data_write_callbacks << '\n';
        return 1;
    }
    return 0;
}

int run_fastmem_fault_fallback() {
    MappedGuestMemory memory;
    if (prepare_mapped_code(memory, kFaultingLoadCode) != 0) {
        return 1;
    }

    const std::uint32_t unmapped = static_cast<std::uint32_t>(memory.page_size());
    ExecutionRequest request{};
    request.regs[1] = unmapped;
    request.instruction_count = 1;

    const auto result = liba32android::cpu::execute(memory, request);
    if (!result.fastmem_enabled || !result.memory_fault || result.data_read_callbacks == 0) {
        std::cerr << "fastmem fault did not fall back to callbacks: enabled=" << result.fastmem_enabled
                  << " fault=" << result.memory_fault
                  << " callbacks=" << result.data_read_callbacks << '\n';
        return 1;
    }
    return 0;
}

int run_fastmem_self_modifying_code() {
    MappedGuestMemory memory;
    const std::size_t page = memory.page_size();
    const auto rwx = MemoryPermission::Read |
                     MemoryPermission::Write |
                     MemoryPermission::Execute;
    if (!memory.map(0, page, rwx)) {
        std::cerr << "failed to map RWX self-modifying page\\n";
        return 1;
    }

    constexpr std::uint32_t target = 0x100U;
    constexpr std::array<std::uint8_t, 8> writer{
        0x00, 0x20, 0x81, 0xE5,  // str r2,[r1]
        0x11, 0xFF, 0x2F, 0xE1,  // bx r1
    };
    constexpr std::array<std::uint8_t, 4> initial_target{
        0x01, 0x00, 0xA0, 0xE3,  // mov r0,#1
    };
    if (!memory.write(0, writer) || !memory.write(target, initial_target)) {
        std::cerr << "failed to stage self-modifying code\\n";
        return 1;
    }

    liba32android::cpu::A32Executor executor{memory};
    ExecutionRequest warm{};
    warm.entry_pc = target;
    warm.instruction_count = 1;
    const auto first = executor.execute(warm);
    if (first.memory_fault || first.exception_raised || first.regs[0] != 1) {
        std::cerr << "failed to execute initial target\\n";
        return 1;
    }

    ExecutionRequest mutate{};
    mutate.regs[1] = target;
    mutate.regs[2] = 0xE3A0002AU;  // mov r0,#42
    mutate.instruction_count = 3;
    const auto result = executor.execute(mutate);
    std::uint32_t stored = 0;
    if (result.memory_fault || result.exception_raised ||
        result.instructions_executed != 3 || result.regs[0] != 42 ||
        !result.jit_instance_reused || result.data_write_callbacks != 0 ||
        result.code_cache_clears == 0 ||
        !read_u32(memory, target, stored) || stored != 0xE3A0002AU) {
        std::cerr << "fastmem self-modifying code invalidation failed\\n";
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (run_linear_baseline() != 0) {
        return 1;
    }
    if (run_fastmem_load_store() != 0) {
        return 1;
    }
    if (run_fastmem_fault_fallback() != 0) {
        return 1;
    }
    if (run_fastmem_self_modifying_code() != 0) {
        return 1;
    }
    return 0;
}
