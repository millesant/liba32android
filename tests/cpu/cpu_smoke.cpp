#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>

#include "cpu/a32_cpu.h"
#include "memory/guest_memory.h"

namespace {

int run(std::span<const std::uint8_t> code, liba32android::cpu::InstructionSet instruction_set,
        std::string_view label) {
    liba32android::memory::LinearGuestMemory memory{4096};
    if (!memory.write(0, code)) {
        std::cerr << label << " smoke failed to load code\n";
        return 1;
    }

    liba32android::cpu::ExecutionRequest request{};
    request.instruction_set = instruction_set;
    request.instruction_count = 1;
    const auto result = liba32android::cpu::execute(memory, request);

    if (result.exception_raised || result.memory_fault || result.instructions_executed != 1 || result.regs[0] != 42) {
        std::cerr << label << " smoke failed: r0=" << result.regs[0]
                  << " executed=" << result.instructions_executed
                  << " exception=" << result.exception_raised
                  << " memory_fault=" << result.memory_fault << '\n';
        return 1;
    }

    std::cout << label << " guest returned 42\n";
    return 0;
}

int run_arm() {
    // ARM little-endian: mov r0, #42 (E3A0002A).
    constexpr std::array<std::uint8_t, 4> code{0x2A, 0x00, 0xA0, 0xE3};
    return run(code, liba32android::cpu::InstructionSet::Arm, "ARM");
}

int run_thumb() {
    // Thumb little-endian: movs r0, #42 (202A). The extra halfword pads the
    // wider instruction fetch used by the translator.
    constexpr std::array<std::uint8_t, 4> code{0x2A, 0x20, 0x00, 0x00};
    return run(code, liba32android::cpu::InstructionSet::Thumb, "Thumb");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: cpu_smoke <arm|thumb>\n";
        return 2;
    }

    const std::string_view mode{argv[1]};
    if (mode == "arm") {
        return run_arm();
    }
    if (mode == "thumb") {
        return run_thumb();
    }

    std::cerr << "unknown mode: " << mode << '\n';
    return 2;
}
