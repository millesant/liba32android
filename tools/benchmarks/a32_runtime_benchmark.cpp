#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <span>
#include <vector>

#include "cpu/a32_cpu.h"
#include "memory/guest_memory.h"

using liba32android::cpu::A32Executor;
using liba32android::cpu::ExecutionRequest;
using liba32android::memory::LinearGuestMemory;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::array<std::uint8_t, 8> kComputeCode{{
    0x01, 0x00, 0x80, 0xE2,  // add r0,r0,#1
    0xFD, 0xFF, 0xFF, 0xEA,  // b 0
}};

constexpr std::array<std::uint8_t, 16> kFastmemCode{{
    0x00, 0x00, 0x91, 0xE5,  // ldr r0,[r1]
    0x01, 0x00, 0x80, 0xE2,  // add r0,r0,#1
    0x00, 0x00, 0x81, 0xE5,  // str r0,[r1]
    0xFB, 0xFF, 0xFF, 0xEA,  // b 0x1000
}};

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

template <typename F>
double run_samples(std::size_t samples, F&& fn) {
    std::vector<double> ns_per_instruction;
    ns_per_instruction.reserve(samples);
    for (std::size_t i = 0; i < samples; ++i) {
        const auto start = Clock::now();
        const auto executed = fn();
        const auto end = Clock::now();
        if (executed == 0) return 0.0;

        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start).count();
        ns_per_instruction.push_back(
            static_cast<double>(ns) / static_cast<double>(executed));
    }
    return median(std::move(ns_per_instruction));
}

double bench_compute(std::size_t instructions, std::size_t samples) {
    LinearGuestMemory memory{4096};
    if (!memory.write(0, kComputeCode)) return 0.0;

    A32Executor executor{memory};
    ExecutionRequest request{};
    request.instruction_count = 1000;
    (void)executor.execute(request);

    request.instruction_count = instructions;
    return run_samples(samples, [&] {
        request.regs = {};
        const auto result = executor.execute(request);
        if (result.exception_raised || result.memory_fault) {
            return std::size_t{0};
        }
        return result.instructions_executed;
    });
}

double bench_fastmem(std::size_t instructions, std::size_t samples) {
    MappedGuestMemory memory;
    const auto page = memory.page_size();
    if (!memory.map(
            0x1000U,
            page,
            MemoryPermission::Read | MemoryPermission::Write) ||
        !memory.map(
            0x2000U,
            page,
            MemoryPermission::Read | MemoryPermission::Write) ||
        !memory.write(0x1000U, kFastmemCode) ||
        !memory.protect(
            0x1000U,
            page,
            MemoryPermission::Read | MemoryPermission::Execute)) {
        return 0.0;
    }

    A32Executor executor{memory};
    ExecutionRequest request{};
    request.entry_pc = 0x1000U;
    request.regs[1] = 0x2000U;
    request.instruction_count = 1000;
    (void)executor.execute(request);

    request.instruction_count = instructions;
    return run_samples(samples, [&] {
        request.regs = {};
        request.regs[1] = 0x2000U;
        const auto result = executor.execute(request);
        if (result.exception_raised ||
            result.memory_fault ||
            !result.fastmem_enabled) {
            return std::size_t{0};
        }
        return result.instructions_executed;
    });
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t instructions =
        argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 5000000ULL;
    const std::size_t samples =
        argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 5ULL;

    if (instructions == 0U || samples == 0U) {
        std::cerr << "instruction budget and sample count must be non-zero\n";
        return 2;
    }

    const double compute = bench_compute(instructions, samples);
    const double fastmem = bench_fastmem(instructions, samples);
    if (compute <= 0.0 || fastmem <= 0.0) return 1;

    std::cout << std::fixed << std::setprecision(4)
              << "compute_ns_per_instruction=" << compute << '\n'
              << "compute_mips=" << (1000.0 / compute) << '\n'
              << "fastmem_ns_per_instruction=" << fastmem << '\n'
              << "fastmem_mips=" << (1000.0 / fastmem) << '\n';
    return 0;
}
