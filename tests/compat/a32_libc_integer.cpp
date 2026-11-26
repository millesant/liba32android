#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>

#include "compat/a32_libc_integer.h"
#include "cpu/a32_cpu.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"
#include "runtime/a32_service_registry.h"

namespace {

using liba32android::compat::A32LibcErrnoSink;
using liba32android::compat::A32LibcGuestErrnoState;
using liba32android::compat::A32LibcIntegerOptions;
using liba32android::compat::A32LibcIntegerService;
using liba32android::compat::kA32AndroidEinval;
using liba32android::compat::kA32AndroidErange;
using liba32android::compat::kA32LibcAtoiSvcImmediate;
using liba32android::compat::kA32LibcErrnoSvcImmediate;
using liba32android::compat::kA32LibcStrtolSvcImmediate;
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

std::int32_t signed_r0(std::uint32_t value) {
    return std::bit_cast<std::int32_t>(value);
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

class RecordingErrnoSink final : public A32LibcErrnoSink {
public:
    std::size_t calls{};
    std::int32_t value{};

    bool set_errno(
        liba32android::memory::GuestMemory&,
        std::int32_t next) noexcept override {
        ++calls;
        value = next;
        return true;
    }
};

int test_atoi_and_overflow() {
    LinearGuestMemory memory{1024};
    constexpr std::array<std::uint8_t, 7> value{
        ' ', ' ', '-', '4', '2', 'x', 0,
    };
    constexpr std::array<std::uint8_t, 13> overflow{
        '9','9','9','9','9','9','9','9','9','9','9','x',0,
    };
    if (!memory.write(0x100U, value) ||
        !memory.write(0x140U, overflow)) {
        return fail("could not stage atoi inputs");
    }

    RecordingErrnoSink errno_sink;
    A32LibcIntegerService service{
        errno_sink, A32LibcIntegerOptions{32}};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    regs[0] = 0x100U;
    if (service.handle(memory, kA32LibcAtoiSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != -42 ||
        errno_sink.calls != 0) {
        return fail("atoi signed decimal conversion failed");
    }

    regs = {};
    regs[0] = 0x140U;
    if (service.handle(memory, kA32LibcAtoiSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != std::numeric_limits<std::int32_t>::max() ||
        errno_sink.calls != 1 ||
        errno_sink.value != kA32AndroidErange) {
        return fail("atoi overflow did not clamp/set ERANGE");
    }
    return 0;
}

int test_strtol_bases_and_endptr() {
    LinearGuestMemory memory{1024};
    constexpr std::array<std::uint8_t, 9> hex{
        ' ', ' ', '-', '0', 'x', '1', '0', 'z', 0,
    };
    constexpr std::array<std::uint8_t, 7> binary{
        '0','b','1','0','1','2',0,
    };
    constexpr std::array<std::uint8_t, 5> octal{
        '0','7','7','9',0,
    };
    constexpr std::array<std::uint8_t, 4> nodigits{
        ' ', '+', 'x', 0,
    };
    if (!memory.write(0x100U, hex) ||
        !memory.write(0x140U, binary) ||
        !memory.write(0x180U, octal) ||
        !memory.write(0x1c0U, nodigits)) {
        return fail("could not stage strtol inputs");
    }

    RecordingErrnoSink errno_sink;
    A32LibcIntegerService service{
        errno_sink, A32LibcIntegerOptions{32}};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    regs[0] = 0x100U;
    regs[1] = 0x200U;
    regs[2] = 0U;
    if (service.handle(memory, kA32LibcStrtolSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != -16 ||
        read_u32_le(memory, 0x200U) != 0x107U) {
        return fail("strtol hexadecimal prefix/endptr failed");
    }

    regs = {};
    regs[0] = 0x140U;
    regs[1] = 0x204U;
    regs[2] = 0U;
    if (service.handle(memory, kA32LibcStrtolSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != 5 ||
        read_u32_le(memory, 0x204U) != 0x145U) {
        return fail("strtol binary prefix/endptr failed");
    }

    regs = {};
    regs[0] = 0x180U;
    regs[1] = 0x208U;
    regs[2] = 0U;
    if (service.handle(memory, kA32LibcStrtolSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != 63 ||
        read_u32_le(memory, 0x208U) != 0x183U) {
        return fail("strtol octal auto-base failed");
    }

    regs = {};
    regs[0] = 0x1c0U;
    regs[1] = 0x20cU;
    regs[2] = 10U;
    if (service.handle(memory, kA32LibcStrtolSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != 0 ||
        read_u32_le(memory, 0x20cU) != 0x1c0U) {
        return fail("strtol no-digit endptr did not return original input");
    }

    if (errno_sink.calls != 0) {
        return fail("successful/no-digit strtol unexpectedly changed errno");
    }
    return 0;
}

int test_invalid_base_overflow_and_failures() {
    LinearGuestMemory memory{512};
    constexpr std::array<std::uint8_t, 13> underflow{
        '-','2','1','4','7','4','8','3','6','4','9','!',0,
    };
    constexpr std::array<std::uint8_t, 5> no_stop{
        '1','2','3','4','5',
    };
    if (!memory.write(0x100U, underflow) ||
        !memory.write(0x140U, no_stop)) {
        return fail("could not stage integer failure inputs");
    }

    RecordingErrnoSink errno_sink;
    A32LibcIntegerService service{
        errno_sink, A32LibcIntegerOptions{32}};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    regs[0] = 0xffffffffU;
    regs[1] = 0x180U;
    regs[2] = 1U;
    if (service.handle(memory, kA32LibcStrtolSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != 0 ||
        read_u32_le(memory, 0x180U) != 0xffffffffU ||
        errno_sink.calls != 1 ||
        errno_sink.value != kA32AndroidEinval) {
        return fail("invalid strtol base did not avoid input read/set EINVAL");
    }

    regs = {};
    regs[0] = 0x100U;
    regs[1] = 0x184U;
    regs[2] = 10U;
    if (service.handle(memory, kA32LibcStrtolSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != std::numeric_limits<std::int32_t>::min() ||
        read_u32_le(memory, 0x184U) != 0x10bU ||
        errno_sink.calls != 2 ||
        errno_sink.value != kA32AndroidErange) {
        return fail("strtol underflow/endptr did not clamp/set ERANGE");
    }

    A32LibcIntegerService bounded{
        errno_sink, A32LibcIntegerOptions{4}};
    regs = {};
    regs[0] = 0x140U;
    if (bounded.handle(memory, kA32LibcAtoiSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Failed) {
        return fail("parse-byte ceiling was not enforced");
    }

    regs = {};
    regs[0] = 0x100U;
    regs[1] = 0x1feU;
    regs[2] = 10U;
    if (service.handle(memory, kA32LibcStrtolSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Failed) {
        return fail("unwritable strtol endptr did not fail");
    }

    regs = {};
    if (service.handle(memory, 0xADU, regs, cpsr) !=
        A32HostServiceDisposition::Unhandled) {
        return fail("unknown integer service ID was not unhandled");
    }
    return 0;
}

int test_guest_errno_state() {
    LinearGuestMemory memory{1024};
    A32LibcGuestErrnoState state{0x300U};

    if (!state.set_errno(memory, kA32AndroidErange) ||
        read_u32_le(memory, 0x300U) !=
            static_cast<std::uint32_t>(kA32AndroidErange)) {
        return fail("guest errno state did not publish value");
    }

    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    if (state.handle(memory, kA32LibcErrnoSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0x300U) {
        return fail("__errno service did not return configured guest slot");
    }

    if (state.handle(memory, 0xAEU, regs, cpsr) !=
        A32HostServiceDisposition::Unhandled) {
        return fail("guest errno state did not leave unknown SVC unhandled");
    }

    A32LibcGuestErrnoState invalid{0U};
    regs = {};
    if (invalid.set_errno(memory, kA32AndroidErange) ||
        invalid.handle(memory, kA32LibcErrnoSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Failed) {
        return fail("null guest errno slot was not rejected");
    }
    return 0;
}

int test_arm_registry_integration() {
    constexpr std::array<std::uint8_t, 8> code{
        0xAB, 0x00, 0x00, 0xEF,
        0x1E, 0xFF, 0x2F, 0xE1,
    };
    constexpr std::array<std::uint8_t, 4> input{
        '1','2','3',0,
    };

    LinearGuestMemory memory{4096};
    if (!memory.write(0, code) ||
        !memory.write(0x100U, input)) {
        return fail("could not stage atoi ARM fixture");
    }

    RecordingErrnoSink errno_sink;
    A32LibcIntegerService service{
        errno_sink, A32LibcIntegerOptions{16}};
    const std::array<A32HostServiceRegistryEntry, 1> entries{{
        {kA32LibcAtoiSvcImmediate, &service},
    }};
    A32HostServiceRegistry registry{std::span{entries}};

    ExecutionRequest request{};
    request.regs[0] = 0x100U;
    request.regs[14] = 4096U;
    request.instruction_count = 2;
    request.stop_pc = 4096U;

    const auto result =
        execute_a32_with_services(memory, request, registry, 1);
    if (!result || !result.stop_pc_reached ||
        result.services_handled != 1 ||
        signed_r0(result.regs[0]) != 123 ||
        errno_sink.calls != 0) {
        return fail("ARM atoi SVC did not compose through registry/dispatcher");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_atoi_and_overflow(); status != 0) {
        return status;
    }
    if (const int status = test_strtol_bases_and_endptr(); status != 0) {
        return status;
    }
    if (const int status = test_invalid_base_overflow_and_failures(); status != 0) {
        return status;
    }
    if (const int status = test_guest_errno_state(); status != 0) {
        return status;
    }
    if (const int status = test_arm_registry_integration(); status != 0) {
        return status;
    }
    return 0;
}
