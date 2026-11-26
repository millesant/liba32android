#include <array>
#include <cstdint>
#include <iostream>
#include <span>

#include "cpu/a32_cpu.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"
#include "runtime/a32_service_registry.h"

namespace {

using liba32android::cpu::ExecutionRequest;
using liba32android::memory::LinearGuestMemory;
using liba32android::runtime::A32HostServiceDisposition;
using liba32android::runtime::A32HostServiceHandler;
using liba32android::runtime::A32HostServiceRegistry;
using liba32android::runtime::A32HostServiceRegistryEntry;
using liba32android::runtime::execute_a32_with_services;

constexpr std::size_t kMemorySize = 4096;
constexpr std::uint32_t kStopPc = static_cast<std::uint32_t>(kMemorySize);

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

class RecordingHandler final : public A32HostServiceHandler {
public:
    A32HostServiceDisposition disposition{A32HostServiceDisposition::Handled};
    std::uint32_t replacement_r0{0x1234U};
    std::size_t calls{};
    std::uint32_t last_immediate{};

    A32HostServiceDisposition handle(
        liba32android::memory::GuestMemory&,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override {
        ++calls;
        last_immediate = svc_immediate;
        regs[0] = replacement_r0;
        cpsr ^= 0x20000000U;
        return disposition;
    }
};

int test_exact_dispatch_through_runtime() {
    // svc #0x42; bx lr
    constexpr std::array<std::uint8_t, 8> code{
        0x42, 0x00, 0x00, 0xEF,
        0x1E, 0xFF, 0x2F, 0xE1,
    };

    LinearGuestMemory memory{kMemorySize};
    if (!memory.write(0, code)) {
        return fail("could not stage registry dispatch program");
    }

    RecordingHandler ignored;
    RecordingHandler selected;
    selected.replacement_r0 = 77;

    const std::array<A32HostServiceRegistryEntry, 2> entries{{
        {0x11U, &ignored},
        {0x42U, &selected},
    }};
    A32HostServiceRegistry registry{std::span{entries}};

    ExecutionRequest request{};
    request.regs[14] = kStopPc;
    request.instruction_count = 2;
    request.stop_pc = kStopPc;

    const auto result =
        execute_a32_with_services(memory, request, registry, 1);
    if (!result || !result.stop_pc_reached ||
        result.services_handled != 1 ||
        result.regs[0] != 77 ||
        selected.calls != 1 || selected.last_immediate != 0x42U ||
        ignored.calls != 0) {
        return fail("registry did not dispatch the exact SVC through runtime");
    }
    return 0;
}

int test_unhandled_and_child_result_forwarding() {
    LinearGuestMemory memory{kMemorySize};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr = 0x10U;

    RecordingHandler handler;
    const std::array<A32HostServiceRegistryEntry, 1> entries{{
        {0x22U, &handler},
    }};
    A32HostServiceRegistry registry{std::span{entries}};

    if (registry.handle(memory, 0x23U, regs, cpsr) !=
            A32HostServiceDisposition::Unhandled ||
        handler.calls != 0) {
        return fail("unknown SVC did not remain unhandled");
    }

    handler.disposition = A32HostServiceDisposition::Failed;
    if (registry.handle(memory, 0x22U, regs, cpsr) !=
            A32HostServiceDisposition::Failed ||
        handler.calls != 1 || handler.last_immediate != 0x22U ||
        regs[0] != handler.replacement_r0) {
        return fail("child handler result/state was not forwarded exactly");
    }
    return 0;
}

int test_invalid_matching_entries_fail_before_dispatch() {
    LinearGuestMemory memory{kMemorySize};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr = 0x10U;

    RecordingHandler first;
    RecordingHandler second;
    const std::array<A32HostServiceRegistryEntry, 2> duplicates{{
        {0x55U, &first},
        {0x55U, &second},
    }};
    A32HostServiceRegistry duplicate_registry{std::span{duplicates}};
    if (duplicate_registry.handle(memory, 0x55U, regs, cpsr) !=
            A32HostServiceDisposition::Failed ||
        first.calls != 0 || second.calls != 0) {
        return fail("duplicate matching SVC entries were not rejected first");
    }

    const std::array<A32HostServiceRegistryEntry, 1> null_match{{
        {0x66U, nullptr},
    }};
    A32HostServiceRegistry null_registry{std::span{null_match}};
    if (null_registry.handle(memory, 0x66U, regs, cpsr) !=
        A32HostServiceDisposition::Failed) {
        return fail("null matching handler was not rejected");
    }

    RecordingHandler valid;
    const std::array<A32HostServiceRegistryEntry, 2> unrelated_null{{
        {0x77U, nullptr},
        {0x78U, &valid},
    }};
    A32HostServiceRegistry unrelated_registry{std::span{unrelated_null}};
    if (unrelated_registry.handle(memory, 0x78U, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        valid.calls != 1) {
        return fail("unrelated invalid entry poisoned exact lookup");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_exact_dispatch_through_runtime(); status != 0) {
        return status;
    }
    if (const int status = test_unhandled_and_child_result_forwarding();
        status != 0) {
        return status;
    }
    if (const int status = test_invalid_matching_entries_fail_before_dispatch();
        status != 0) {
        return status;
    }
    return 0;
}
