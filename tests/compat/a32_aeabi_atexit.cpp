#include <array>
#include <cstdint>
#include <iostream>
#include <span>

#include "compat/a32_aeabi_atexit.h"
#include "cpu/a32_cpu.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"
#include "runtime/a32_service_registry.h"

namespace {

using liba32android::compat::A32AeabiAtexitRecord;
using liba32android::compat::A32AeabiAtexitService;
using liba32android::compat::kA32AeabiAtexitSvcImmediate;
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

int test_exact_registration_and_capacity() {
    LinearGuestMemory memory{64};
    std::array<A32AeabiAtexitRecord, 2> records{};
    A32AeabiAtexitService service{std::span{records}};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    regs[0] = 0x11111111U;
    regs[1] = 0x22222221U;
    regs[2] = 0x33333333U;
    if (service.handle(
            memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        service.record_count() != 1U ||
        service.records()[0].object != 0x11111111U ||
        service.records()[0].destructor != 0x22222221U ||
        service.records()[0].dso_handle != 0x33333333U) {
        return fail("__aeabi_atexit did not preserve exact guest registration");
    }

    regs = {};
    regs[0] = 0x44444444U;
    regs[1] = 0x55555554U;
    regs[2] = 0x66666666U;
    if (service.handle(
            memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        service.record_count() != 2U) {
        return fail("second __aeabi_atexit registration failed");
    }

    regs = {};
    regs[0] = 0x77777777U;
    regs[1] = 0x88888888U;
    regs[2] = 0x99999999U;
    if (service.handle(
            memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0xffffffffU ||
        service.record_count() != 2U ||
        service.records()[1].object != 0x44444444U) {
        return fail("__aeabi_atexit capacity failure mutated registrations");
    }

    regs = {};
    if (service.handle(memory, 0xD3U, regs, cpsr) !=
        A32HostServiceDisposition::Unhandled) {
        return fail("unknown __aeabi_atexit service ID was not unhandled");
    }
    return 0;
}

int test_arm_registry_integration() {
    constexpr std::array<std::uint8_t, 8> code{
        0xD2, 0x00, 0x00, 0xEF,
        0x1E, 0xFF, 0x2F, 0xE1,
    };

    LinearGuestMemory memory{4096};
    if (!memory.write(0U, code)) {
        return fail("could not stage __aeabi_atexit ARM fixture");
    }

    std::array<A32AeabiAtexitRecord, 2> records{};
    A32AeabiAtexitService service{std::span{records}};
    const std::array<A32HostServiceRegistryEntry, 1> entries{{
        {kA32AeabiAtexitSvcImmediate, &service},
    }};
    A32HostServiceRegistry registry{std::span{entries}};

    ExecutionRequest request{};
    request.regs[0] = 0x11110000U;
    request.regs[1] = 0x22220001U;
    request.regs[2] = 0x33330000U;
    request.regs[14] = 4096U;
    request.instruction_count = 2U;
    request.stop_pc = 4096U;

    const auto result =
        execute_a32_with_services(memory, request, registry, 1U);
    if (!result ||
        !result.stop_pc_reached ||
        result.services_handled != 1U ||
        result.regs[0] != 0U ||
        service.record_count() != 1U ||
        service.records()[0].object != 0x11110000U ||
        service.records()[0].destructor != 0x22220001U ||
        service.records()[0].dso_handle != 0x33330000U) {
        return fail("real ARM __aeabi_atexit SVC registration failed");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_exact_registration_and_capacity();
        status != 0) {
        return status;
    }
    return test_arm_registry_integration();
}
