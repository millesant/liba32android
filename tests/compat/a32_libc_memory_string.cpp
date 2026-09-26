#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <span>

#include "compat/a32_libc_memory_string.h"
#include "cpu/a32_cpu.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"
#include "runtime/a32_service_registry.h"

namespace {

using liba32android::compat::A32LibcMemoryStringOptions;
using liba32android::compat::A32LibcMemoryStringService;
using liba32android::compat::kA32LibcMemchrSvcImmediate;
using liba32android::compat::kA32LibcMemcmpSvcImmediate;
using liba32android::compat::kA32LibcMemcpySvcImmediate;
using liba32android::compat::kA32LibcMemsetSvcImmediate;
using liba32android::compat::kA32LibcStrcmpSvcImmediate;
using liba32android::compat::kA32LibcStrlenSvcImmediate;
using liba32android::compat::kA32LibcStrncmpSvcImmediate;
using liba32android::cpu::ExecutionRequest;
using liba32android::memory::LinearGuestMemory;
using liba32android::runtime::A32HostServiceDisposition;
using liba32android::runtime::A32HostServiceRegistry;
using liba32android::runtime::A32HostServiceRegistryEntry;
using liba32android::runtime::execute_a32_with_services;

constexpr std::size_t kMemorySize = 4096;
constexpr std::uint32_t kStopPc = static_cast<std::uint32_t>(kMemorySize);

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

std::int32_t signed_r0(std::uint32_t value) {
    return std::bit_cast<std::int32_t>(value);
}

int test_memory_primitives() {
    LinearGuestMemory memory{kMemorySize};
    constexpr std::array<std::uint8_t, 4> source{{1, 2, 3, 4}};
    constexpr std::array<std::uint8_t, 4> rhs{{1, 2, 4, 4}};
    if (!memory.write(0x100U, source) ||
        !memory.write(0x180U, rhs)) {
        return fail("could not stage memory primitive inputs");
    }

    A32LibcMemoryStringService service{
        A32LibcMemoryStringOptions{16, 16}};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr = 0x10U;

    regs[0] = 0x120U;
    regs[1] = 0x100U;
    regs[2] = 4U;
    if (service.handle(memory, kA32LibcMemcpySvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0x120U) {
        return fail("memcpy service did not return destination");
    }
    std::array<std::uint8_t, 4> copied{};
    if (!memory.read(0x120U, copied) || copied != source) {
        return fail("memcpy service did not copy source bytes");
    }

    regs = {};
    regs[0] = 0x140U;
    regs[1] = 0x1ffU;
    regs[2] = 3U;
    if (service.handle(memory, kA32LibcMemsetSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0x140U) {
        return fail("memset service failed");
    }
    std::array<std::uint8_t, 3> filled{};
    if (!memory.read(0x140U, filled) ||
        filled != std::array<std::uint8_t, 3>{{0xff, 0xff, 0xff}}) {
        return fail("memset service did not use low byte");
    }

    regs = {};
    regs[0] = 0x100U;
    regs[1] = 0x180U;
    regs[2] = 4U;
    if (service.handle(memory, kA32LibcMemcmpSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) >= 0) {
        return fail("memcmp service did not return negative sign");
    }

    regs = {};
    regs[0] = 0x180U;
    regs[1] = 0x100U;
    regs[2] = 4U;
    if (service.handle(memory, kA32LibcMemcmpSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) <= 0) {
        return fail("memcmp service did not return positive sign");
    }

    regs = {};
    regs[0] = 0x100U;
    regs[1] = 3U;
    regs[2] = 4U;
    if (service.handle(memory, kA32LibcMemchrSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0x102U) {
        return fail("memchr service did not return logical guest pointer");
    }

    regs = {};
    regs[0] = 0x100U;
    regs[1] = 9U;
    regs[2] = 4U;
    if (service.handle(memory, kA32LibcMemchrSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("memchr service miss did not return null");
    }
    return 0;
}

int test_string_primitives() {
    LinearGuestMemory memory{kMemorySize};
    constexpr std::array<std::uint8_t, 6> alpha{
        'a','l','p','h','a',0,
    };
    constexpr std::array<std::uint8_t, 6> alphb{
        'a','l','p','h','b',0,
    };
    if (!memory.write(0x200U, alpha) ||
        !memory.write(0x220U, alphb)) {
        return fail("could not stage string primitive inputs");
    }

    A32LibcMemoryStringService service{
        A32LibcMemoryStringOptions{16, 5}};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr = 0x10U;

    regs[0] = 0x200U;
    if (service.handle(memory, kA32LibcStrlenSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 5U) {
        return fail("strlen service rejected exact-limit payload");
    }

    regs = {};
    regs[0] = 0x200U;
    regs[1] = 0x220U;
    if (service.handle(memory, kA32LibcStrcmpSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) >= 0) {
        return fail("strcmp service did not return negative sign");
    }

    regs = {};
    regs[0] = 0x200U;
    regs[1] = 0x220U;
    regs[2] = 4U;
    if (service.handle(memory, kA32LibcStrncmpSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != 0) {
        return fail("strncmp equal prefix failed");
    }

    regs = {};
    regs[0] = 0x200U;
    regs[1] = 0x220U;
    regs[2] = 5U;
    if (service.handle(memory, kA32LibcStrncmpSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) >= 0) {
        return fail("strncmp differing byte sign failed");
    }

    regs = {};
    regs[0] = 0xffffffffU;
    regs[1] = 0xffffffffU;
    regs[2] = 0U;
    if (service.handle(memory, kA32LibcStrncmpSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != 0) {
        return fail("zero-count strncmp accessed guest pointers");
    }
    return 0;
}

int test_bounds_and_failures() {
    LinearGuestMemory memory{512};
    constexpr std::array<std::uint8_t, 4> destination{{9, 9, 9, 9}};
    constexpr std::array<std::uint8_t, 5> too_long{
        'a','b','c','d',0,
    };
    if (!memory.write(0x100U, destination) ||
        !memory.write(0x120U, too_long)) {
        return fail("could not stage bounds inputs");
    }

    A32LibcMemoryStringService service{
        A32LibcMemoryStringOptions{4, 3}};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr = 0x10U;

    regs[0] = 0x100U;
    regs[1] = 0x1feU;
    regs[2] = 4U;
    if (service.handle(memory, kA32LibcMemcpySvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Failed) {
        return fail("unreadable memcpy source did not fail");
    }
    std::array<std::uint8_t, 4> after{};
    if (!memory.read(0x100U, after) || after != destination) {
        return fail("memcpy source failure mutated destination");
    }

    regs = {};
    regs[0] = 0x100U;
    regs[1] = 0x120U;
    regs[2] = 5U;
    if (service.handle(memory, kA32LibcMemcpySvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Failed) {
        return fail("transfer ceiling was not enforced");
    }

    regs = {};
    regs[0] = 0x120U;
    if (service.handle(memory, kA32LibcStrlenSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Failed) {
        return fail("unterminated string within ceiling did not fail");
    }

    regs = {};
    regs[0] = 0xfffffffeU;
    regs[1] = 1U;
    regs[2] = 3U;
    if (service.handle(memory, kA32LibcMemchrSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Failed) {
        return fail("logical guest range wrap did not fail");
    }

    regs = {};
    if (service.handle(memory, 0xAFU, regs, cpsr) !=
        A32HostServiceDisposition::Unhandled) {
        return fail("unknown libc service ID was not unhandled");
    }
    return 0;
}

int test_arm_registry_integration() {
    constexpr std::array<std::uint8_t, 8> code{
        0xA1, 0x00, 0x00, 0xEF,
        0x1E, 0xFF, 0x2F, 0xE1,
    };
    constexpr std::array<std::uint8_t, 4> source{{7, 8, 9, 10}};

    LinearGuestMemory memory{kMemorySize};
    if (!memory.write(0, code) ||
        !memory.write(0x100U, source)) {
        return fail("could not stage libc service ARM fixture");
    }

    A32LibcMemoryStringService service{
        A32LibcMemoryStringOptions{16, 16}};
    const std::array<A32HostServiceRegistryEntry, 1> entries{{
        {kA32LibcMemcpySvcImmediate, &service},
    }};
    A32HostServiceRegistry registry{std::span{entries}};

    ExecutionRequest request{};
    request.regs[0] = 0x120U;
    request.regs[1] = 0x100U;
    request.regs[2] = 4U;
    request.regs[14] = kStopPc;
    request.instruction_count = 2;
    request.stop_pc = kStopPc;

    const auto result =
        execute_a32_with_services(memory, request, registry, 1);
    std::array<std::uint8_t, 4> copied{};
    if (!result || !result.stop_pc_reached ||
        result.services_handled != 1 ||
        result.regs[0] != 0x120U ||
        !memory.read(0x120U, copied) ||
        copied != source) {
        return fail("ARM memcpy SVC did not compose through registry/dispatcher");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_memory_primitives(); status != 0) {
        return status;
    }
    if (const int status = test_string_primitives(); status != 0) {
        return status;
    }
    if (const int status = test_bounds_and_failures(); status != 0) {
        return status;
    }
    if (const int status = test_arm_registry_integration(); status != 0) {
        return status;
    }
    return 0;
}
