#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>

#include "cpu/a32_cpu.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::cpu::ExecutionRequest;
using liba32android::cpu::ExecutionResult;
using liba32android::cpu::InstructionSet;
using liba32android::memory::LinearGuestMemory;

constexpr std::size_t kMemorySize = 4096;

bool load_code(LinearGuestMemory& memory, std::span<const std::uint8_t> code) {
    return memory.write(0, code);
}

bool execution_ok(const ExecutionResult& result, std::size_t expected_instructions) {
    return !result.exception_raised && !result.memory_fault &&
           result.instructions_executed == expected_instructions;
}

bool read_u32(const LinearGuestMemory& memory, std::uint32_t address, std::uint32_t& value) {
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

ExecutionRequest thumb_request(std::size_t instruction_count) {
    ExecutionRequest request{};
    request.instruction_set = InstructionSet::Thumb;
    request.instruction_count = instruction_count;
    return request;
}

int run_register_state() {
    // add r2, r0, r1
    constexpr std::array<std::uint8_t, 4> code{0x01, 0x20, 0x80, 0xE0};
    LinearGuestMemory memory{kMemorySize};
    if (!load_code(memory, code)) {
        return 1;
    }

    ExecutionRequest request{};
    request.regs[0] = 20;
    request.regs[1] = 22;
    const auto result = liba32android::cpu::execute(memory, request);
    if (!execution_ok(result, 1) || result.regs[2] != 42) {
        std::cerr << "register-state test failed: r2=" << result.regs[2] << '\n';
        return 1;
    }
    return 0;
}

int run_branch() {
    // b 0x8; mov r0,#1; mov r0,#42
    constexpr std::array<std::uint8_t, 12> code{
        0x00, 0x00, 0x00, 0xEA,
        0x01, 0x00, 0xA0, 0xE3,
        0x2A, 0x00, 0xA0, 0xE3,
    };
    LinearGuestMemory memory{kMemorySize};
    if (!load_code(memory, code)) {
        return 1;
    }

    ExecutionRequest request{};
    request.instruction_count = 2;
    const auto result = liba32android::cpu::execute(memory, request);
    if (!execution_ok(result, 2) || result.regs[0] != 42) {
        std::cerr << "branch test failed: r0=" << result.regs[0] << '\n';
        return 1;
    }
    return 0;
}

int run_call() {
    // bl 0x8; LR receives the ARM return address 0x4.
    constexpr std::array<std::uint8_t, 12> code{
        0x00, 0x00, 0x00, 0xEB,
        0x01, 0x00, 0xA0, 0xE3,
        0x2A, 0x00, 0xA0, 0xE3,
    };
    LinearGuestMemory memory{kMemorySize};
    if (!load_code(memory, code)) {
        return 1;
    }

    ExecutionRequest request{};
    request.instruction_count = 2;
    const auto result = liba32android::cpu::execute(memory, request);
    if (!execution_ok(result, 2) || result.regs[0] != 42 || result.regs[14] != 4) {
        std::cerr << "call test failed: r0=" << result.regs[0] << " lr=" << result.regs[14] << '\n';
        return 1;
    }

    // bx lr must be able to return to an unmapped logical stop PC without
    // fetching from that address.
    constexpr std::array<std::uint8_t, 4> return_code{
        0x1E, 0xFF, 0x2F, 0xE1,
    };
    LinearGuestMemory return_memory{kMemorySize};
    if (!load_code(return_memory, return_code)) return 1;
    ExecutionRequest return_request{};
    return_request.regs[14] = static_cast<std::uint32_t>(kMemorySize);
    // Reaching the stop PC on the final permitted instruction must still
    // report a successful stop rather than budget exhaustion.
    return_request.instruction_count = 1;
    return_request.stop_pc = static_cast<std::uint32_t>(kMemorySize);
    const auto returned =
        liba32android::cpu::execute(return_memory, return_request);
    if (!execution_ok(returned, 1) || !returned.stop_pc_reached ||
        returned.regs[15] != kMemorySize) {
        std::cerr << "ARM stop-PC return failed: executed="
                  << returned.instructions_executed
                  << " pc=" << returned.regs[15]
                  << " stop=" << returned.stop_pc_reached << '\n';
        return 1;
    }

    // Initial stop must win before an instruction fetch.
    ExecutionRequest initial_stop{};
    initial_stop.entry_pc = static_cast<std::uint32_t>(kMemorySize);
    initial_stop.instruction_count = 1;
    initial_stop.stop_pc = static_cast<std::uint32_t>(kMemorySize);
    const auto initially_stopped =
        liba32android::cpu::execute(return_memory, initial_stop);
    if (!execution_ok(initially_stopped, 0) ||
        !initially_stopped.stop_pc_reached ||
        initially_stopped.code_read_callbacks != 0) {
        std::cerr << "initial stop-PC did not stop before fetch\n";
        return 1;
    }

    // An unreachable stop PC keeps the existing fixed-budget behavior.
    constexpr std::array<std::uint8_t, 4> loop_code{
        0xFE, 0xFF, 0xFF, 0xEA,
    };
    LinearGuestMemory loop_memory{kMemorySize};
    if (!load_code(loop_memory, loop_code)) return 1;
    ExecutionRequest budgeted{};
    budgeted.instruction_count = 2;
    budgeted.stop_pc = static_cast<std::uint32_t>(kMemorySize);
    const auto exhausted = liba32android::cpu::execute(loop_memory, budgeted);
    if (!execution_ok(exhausted, 2) || exhausted.stop_pc_reached) {
        std::cerr << "unreached stop-PC changed instruction-budget behavior\n";
        return 1;
    }
    return 0;
}

int run_memory_load_store() {
    // str r0,[r1]; ldr r2,[r1]
    constexpr std::array<std::uint8_t, 8> code{
        0x00, 0x00, 0x81, 0xE5,
        0x00, 0x20, 0x91, 0xE5,
    };
    LinearGuestMemory memory{kMemorySize};
    if (!load_code(memory, code)) {
        return 1;
    }

    ExecutionRequest request{};
    request.regs[0] = 0x12345678;
    request.regs[1] = 0x300;
    request.instruction_count = 2;
    const auto result = liba32android::cpu::execute(memory, request);

    std::uint32_t stored = 0;
    if (!read_u32(memory, 0x300, stored) || !execution_ok(result, 2) ||
        result.regs[2] != 0x12345678 || stored != 0x12345678) {
        std::cerr << "memory test failed: r2=0x" << std::hex << result.regs[2]
                  << " stored=0x" << stored << std::dec << '\n';
        return 1;
    }
    return 0;
}

int run_stack() {
    // str r0,[sp,#-4]!; ldr r1,[sp]
    constexpr std::array<std::uint8_t, 8> code{
        0x04, 0x00, 0x2D, 0xE5,
        0x00, 0x10, 0x9D, 0xE5,
    };
    LinearGuestMemory memory{kMemorySize};
    if (!load_code(memory, code)) {
        return 1;
    }

    ExecutionRequest request{};
    request.regs[0] = 0xCAFEBABE;
    request.regs[13] = 0x800;
    request.instruction_count = 2;
    const auto result = liba32android::cpu::execute(memory, request);

    std::uint32_t stored = 0;
    if (!read_u32(memory, 0x7FC, stored) || !execution_ok(result, 2) ||
        result.regs[13] != 0x7FC || result.regs[1] != 0xCAFEBABE || stored != 0xCAFEBABE) {
        std::cerr << "stack test failed: sp=0x" << std::hex << result.regs[13]
                  << " r1=0x" << result.regs[1] << " stored=0x" << stored << std::dec << '\n';
        return 1;
    }
    return 0;
}

int run_thumb_branch() {
    // b.n target; movs r0,#1; target: movs r0,#42; nop
    constexpr std::array<std::uint8_t, 8> code{
        0x00, 0xE0,
        0x01, 0x20,
        0x2A, 0x20,
        0x00, 0xBF,
    };
    LinearGuestMemory memory{kMemorySize};
    if (!load_code(memory, code)) {
        return 1;
    }

    const auto result = liba32android::cpu::execute(memory, thumb_request(2));
    if (!execution_ok(result, 2) || result.regs[0] != 42) {
        std::cerr << "Thumb branch test failed: r0=" << result.regs[0] << '\n';
        return 1;
    }
    return 0;
}

int run_thumb_call() {
    // bl target; the skipped mov is at the return address. Thumb LR keeps bit 0 set.
    constexpr std::array<std::uint8_t, 12> code{
        0x00, 0xF0, 0x02, 0xF8,
        0x01, 0x20,
        0x00, 0xBF,
        0x2A, 0x20,
        0x00, 0xBF,
    };
    LinearGuestMemory memory{kMemorySize};
    if (!load_code(memory, code)) {
        return 1;
    }

    const auto result = liba32android::cpu::execute(memory, thumb_request(2));
    if (!execution_ok(result, 2) || result.regs[0] != 42 || result.regs[14] != 5) {
        std::cerr << "Thumb call test failed: r0=" << result.regs[0]
                  << " lr=" << result.regs[14] << '\n';
        return 1;
    }

    // Thumb BX LR clears the interworking bit in PC; exact stop matching uses
    // the normalized target and must not fetch the unmapped address.
    constexpr std::array<std::uint8_t, 2> return_code{0x70, 0x47};
    LinearGuestMemory return_memory{kMemorySize};
    if (!load_code(return_memory, return_code)) return 1;
    auto return_request = thumb_request(1);
    return_request.regs[14] =
        static_cast<std::uint32_t>(kMemorySize) | 1U;
    return_request.stop_pc = static_cast<std::uint32_t>(kMemorySize);
    const auto returned =
        liba32android::cpu::execute(return_memory, return_request);
    if (!execution_ok(returned, 1) || !returned.stop_pc_reached ||
        returned.regs[15] != kMemorySize) {
        std::cerr << "Thumb stop-PC return failed: executed="
                  << returned.instructions_executed
                  << " pc=" << returned.regs[15]
                  << " stop=" << returned.stop_pc_reached << '\n';
        return 1;
    }
    return 0;
}

int run_thumb_memory_load_store() {
    // str r0,[r1]; ldr r2,[r1]; nop
    constexpr std::array<std::uint8_t, 6> code{
        0x08, 0x60,
        0x0A, 0x68,
        0x00, 0xBF,
    };
    LinearGuestMemory memory{kMemorySize};
    if (!load_code(memory, code)) {
        return 1;
    }

    auto request = thumb_request(2);
    request.regs[0] = 0x12345678;
    request.regs[1] = 0x300;
    const auto result = liba32android::cpu::execute(memory, request);

    std::uint32_t stored = 0;
    if (!read_u32(memory, 0x300, stored) || !execution_ok(result, 2) ||
        result.regs[2] != 0x12345678 || stored != 0x12345678) {
        std::cerr << "Thumb memory test failed: r2=0x" << std::hex << result.regs[2]
                  << " stored=0x" << stored << std::dec << '\n';
        return 1;
    }
    return 0;
}

int run_thumb_stack() {
    // push {r0}; ldr r1,[sp]; nop
    constexpr std::array<std::uint8_t, 6> code{
        0x01, 0xB4,
        0x00, 0x99,
        0x00, 0xBF,
    };
    LinearGuestMemory memory{kMemorySize};
    if (!load_code(memory, code)) {
        return 1;
    }

    auto request = thumb_request(2);
    request.regs[0] = 0xCAFEBABE;
    request.regs[13] = 0x800;
    const auto result = liba32android::cpu::execute(memory, request);

    std::uint32_t stored = 0;
    if (!read_u32(memory, 0x7FC, stored) || !execution_ok(result, 2) ||
        result.regs[13] != 0x7FC || result.regs[1] != 0xCAFEBABE || stored != 0xCAFEBABE) {
        std::cerr << "Thumb stack test failed: sp=0x" << std::hex << result.regs[13]
                  << " r1=0x" << result.regs[1] << " stored=0x" << stored << std::dec << '\n';
        return 1;
    }
    return 0;
}

int run_thumb_svc_exception() {
    // svc #0; nop
    constexpr std::array<std::uint8_t, 4> code{0x00, 0xDF, 0x00, 0xBF};
    LinearGuestMemory memory{kMemorySize};
    if (!load_code(memory, code)) {
        return 1;
    }

    const auto result = liba32android::cpu::execute(memory, thumb_request(1));
    if (!result.exception_raised || result.memory_fault ||
        result.instructions_executed != 1 ||
        !result.svc_immediate.has_value() ||
        *result.svc_immediate != 0 ||
        result.regs[15] != 2 ||
        (result.cpsr & 0x20U) == 0) {
        std::cerr << "Thumb SVC test failed: executed=" << result.instructions_executed
                  << " exception=" << result.exception_raised
                  << " memory_fault=" << result.memory_fault
                  << " pc=" << result.regs[15]
                  << " svc="
                  << (result.svc_immediate.has_value()
                          ? *result.svc_immediate
                          : 0xffffffffU)
                  << '\n';
        return 1;
    }
    return 0;
}

int run_svc_resume_state() {
    {
        // mov r0,#1; svc #0x123456; add r0,r0,#1
        constexpr std::array<std::uint8_t, 12> code{
            0x01, 0x00, 0xA0, 0xE3,
            0x56, 0x34, 0x12, 0xEF,
            0x01, 0x00, 0x80, 0xE2,
        };
        LinearGuestMemory memory{kMemorySize};
        if (!load_code(memory, code)) return 1;

        ExecutionRequest first{};
        first.instruction_count = 2;
        const auto trapped = liba32android::cpu::execute(memory, first);
        if (!trapped.exception_raised || trapped.memory_fault ||
            trapped.instructions_executed != 2 ||
            !trapped.svc_immediate.has_value() ||
            *trapped.svc_immediate != 0x123456U ||
            trapped.regs[0] != 1 ||
            trapped.regs[15] != 8) {
            std::cerr << "ARM SVC state test failed: r0=" << trapped.regs[0]
                      << " pc=" << trapped.regs[15] << '\n';
            return 1;
        }

        ExecutionRequest resume{};
        resume.regs = trapped.regs;
        resume.entry_pc = trapped.regs[15];
        resume.instruction_count = 1;
        resume.initial_cpsr = trapped.cpsr;
        const auto continued = liba32android::cpu::execute(memory, resume);
        if (!execution_ok(continued, 1) || continued.svc_immediate.has_value() ||
            continued.regs[0] != 2 || continued.regs[15] != 12) {
            std::cerr << "ARM SVC resume failed: r0=" << continued.regs[0]
                      << " pc=" << continued.regs[15] << '\n';
            return 1;
        }
    }

    {
        // movs r0,#1; svc #0x7a; adds r0,#1
        constexpr std::array<std::uint8_t, 6> code{
            0x01, 0x20,
            0x7A, 0xDF,
            0x01, 0x30,
        };
        LinearGuestMemory memory{kMemorySize};
        if (!load_code(memory, code)) return 1;

        auto first = thumb_request(2);
        const auto trapped = liba32android::cpu::execute(memory, first);
        if (!trapped.exception_raised || trapped.memory_fault ||
            trapped.instructions_executed != 2 ||
            !trapped.svc_immediate.has_value() ||
            *trapped.svc_immediate != 0x7aU ||
            trapped.regs[0] != 1 ||
            trapped.regs[15] != 4 ||
            (trapped.cpsr & 0x20U) == 0) {
            std::cerr << "Thumb SVC state test failed: r0=" << trapped.regs[0]
                      << " pc=" << trapped.regs[15] << '\n';
            return 1;
        }

        // Deliberately leave instruction_set at its default ARM value. The
        // returned CPSR T bit must be what restores Thumb execution.
        ExecutionRequest resume{};
        resume.regs = trapped.regs;
        resume.entry_pc = trapped.regs[15];
        resume.instruction_count = 1;
        resume.initial_cpsr = trapped.cpsr;
        const auto continued = liba32android::cpu::execute(memory, resume);
        if (!execution_ok(continued, 1) || continued.svc_immediate.has_value() ||
            continued.regs[0] != 2 || continued.regs[15] != 6 ||
            (continued.cpsr & 0x20U) == 0) {
            std::cerr << "Thumb SVC resume failed: r0=" << continued.regs[0]
                      << " pc=" << continued.regs[15]
                      << " cpsr=0x" << std::hex << continued.cpsr
                      << std::dec << '\n';
            return 1;
        }
    }

    return 0;
}

int run_instruction_fetch_fault() {
    LinearGuestMemory memory{kMemorySize};

    ExecutionRequest request{};
    request.entry_pc = static_cast<std::uint32_t>(kMemorySize - 2);
    const auto result = liba32android::cpu::execute(memory, request);

    if (!result.memory_fault || result.code_read_callbacks == 0) {
        std::cerr << "instruction-fetch fault test failed: memory_fault=" << result.memory_fault
                  << " code_reads=" << result.code_read_callbacks << '\n';
        return 1;
    }
    return 0;
}

int run_thumb_data_fault() {
    // ldr r0,[r1]; nop
    constexpr std::array<std::uint8_t, 4> code{0x08, 0x68, 0x00, 0xBF};
    LinearGuestMemory memory{kMemorySize};
    if (!load_code(memory, code)) {
        return 1;
    }

    auto request = thumb_request(1);
    request.regs[1] = static_cast<std::uint32_t>(kMemorySize - 2);
    const auto result = liba32android::cpu::execute(memory, request);

    if (!result.memory_fault || result.data_read_callbacks == 0) {
        std::cerr << "Thumb data-fault test failed: memory_fault=" << result.memory_fault
                  << " data_reads=" << result.data_read_callbacks << '\n';
        return 1;
    }
    return 0;
}

int run_persistent_session() {
    constexpr std::array<std::uint8_t, 4> initial_code{
        0x01, 0x00, 0xA0, 0xE3,  // mov r0,#1
    };
    constexpr std::array<std::uint8_t, 4> replacement_code{
        0x2A, 0x00, 0xA0, 0xE3,  // mov r0,#42
    };

    LinearGuestMemory memory{kMemorySize};
    if (!memory.write(0, initial_code)) {
        std::cerr << "persistent session could not stage initial code\\n";
        return 1;
    }

    liba32android::cpu::A32Executor executor{memory};
    ExecutionRequest request{};
    request.instruction_count = 1;

    const auto first = executor.execute(request);
    const auto second = executor.execute(request);
    if (!execution_ok(first, 1) || !execution_ok(second, 1) ||
        first.regs[0] != 1 || second.regs[0] != 1 ||
        first.jit_instance_reused || !second.jit_instance_reused ||
        executor.execution_count() != 2 || second.code_cache_clears != 0) {
        std::cerr << "persistent JIT reuse regression failed\\n";
        return 1;
    }

    if (!memory.write(0, replacement_code)) {
        std::cerr << "persistent session could not mutate code\\n";
        return 1;
    }
    const auto third = executor.execute(request);
    if (!execution_ok(third, 1) || third.regs[0] != 42 ||
        !third.jit_instance_reused || third.code_cache_clears != 1 ||
        third.code_read_callbacks == 0 || executor.execution_count() != 3 ||
        executor.code_cache_clear_count() != 1) {
        std::cerr << "persistent JIT code invalidation failed\\n";
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr
            << "usage: cpu_execution "
               "<registers|branch|call|memory|stack|thumb_branch|thumb_call|"
               "thumb_memory|thumb_stack|thumb_svc|svc_resume|fetch_fault|thumb_data_fault|session>\n";
        return 2;
    }

    const std::string_view mode{argv[1]};
    if (mode == "registers") {
        return run_register_state();
    }
    if (mode == "branch") {
        return run_branch();
    }
    if (mode == "call") {
        return run_call();
    }
    if (mode == "memory") {
        return run_memory_load_store();
    }
    if (mode == "stack") {
        return run_stack();
    }
    if (mode == "thumb_branch") {
        return run_thumb_branch();
    }
    if (mode == "thumb_call") {
        return run_thumb_call();
    }
    if (mode == "thumb_memory") {
        return run_thumb_memory_load_store();
    }
    if (mode == "thumb_stack") {
        return run_thumb_stack();
    }
    if (mode == "thumb_svc") {
        return run_thumb_svc_exception();
    }
    if (mode == "svc_resume") {
        return run_svc_resume_state();
    }
    if (mode == "fetch_fault") {
        return run_instruction_fetch_fault();
    }
    if (mode == "thumb_data_fault") {
        return run_thumb_data_fault();
    }
    if (mode == "session") {
        return run_persistent_session();
    }

    std::cerr << "unknown mode: " << mode << '\n';
    return 2;
}
