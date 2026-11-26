#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "compat/a32_android_log_write.h"
#include "cpu/a32_cpu.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"
#include "runtime/a32_service_registry.h"

namespace {

using liba32android::compat::A32AndroidLogFormatOptions;
using liba32android::compat::A32AndroidLogPrintService;
using liba32android::compat::A32AndroidLogSink;
using liba32android::compat::A32AndroidLogWriteOptions;
using liba32android::compat::A32AndroidLogWriteService;
using liba32android::cpu::ExecutionRequest;
using liba32android::memory::LinearGuestMemory;
using liba32android::runtime::A32HostServiceDisposition;
using liba32android::runtime::A32HostServiceRegistry;
using liba32android::runtime::A32HostServiceRegistryEntry;
using liba32android::runtime::execute_a32_with_services;

constexpr std::size_t kMemorySize = 4096;
constexpr std::uint32_t kServiceId = 0xA0U;
constexpr std::uint32_t kPrintServiceId = 0x133U;
constexpr std::uint32_t kVprintServiceId = 0x134U;
constexpr std::uint32_t kStopPc = static_cast<std::uint32_t>(kMemorySize);

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}


bool write_u32(
    LinearGuestMemory& memory,
    std::uint32_t address,
    std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U),
    }};
    return memory.write(address, bytes);
}

bool write_u64(
    LinearGuestMemory& memory,
    std::uint32_t address,
    std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(value >> (i * 8U));
    }
    return memory.write(address, bytes);
}

A32AndroidLogFormatOptions format_options(
    std::size_t max_output = 128U) {
    return A32AndroidLogFormatOptions{
        .max_tag_bytes = 16U,
        .max_format_bytes = 96U,
        .max_output_bytes = max_output,
        .max_string_argument_bytes = 32U,
        .max_arguments = 16U,
        .max_field_width = 32U,
        .max_precision = 16U,
    };
}

class RecordingSink final : public A32AndroidLogSink {
public:
    std::int32_t return_value{1};
    std::size_t calls{};
    std::int32_t priority{};
    std::optional<std::string> tag;
    std::string text;

    std::int32_t write(
        std::int32_t value,
        std::optional<std::string_view> tag_value,
        std::string_view text_value) override {
        ++calls;
        priority = value;
        tag = tag_value.has_value()
                  ? std::optional<std::string>{std::string{*tag_value}}
                  : std::nullopt;
        text.assign(text_value.data(), text_value.size());
        return return_value;
    }
};

int test_arm_stub_registry_and_service_path() {
    // svc #0xa0; bx lr
    constexpr std::array<std::uint8_t, 8> code{
        0xA0, 0x00, 0x00, 0xEF,
        0x1E, 0xFF, 0x2F, 0xE1,
    };
    constexpr std::array<std::uint8_t, 5> tag{
        'F', 'M', 'O', 'D', 0,
    };
    constexpr std::array<std::uint8_t, 6> text{
        'h', 'e', 'l', 'l', 'o', 0,
    };

    LinearGuestMemory memory{kMemorySize};
    if (!memory.write(0, code) ||
        !memory.write(0x100U, tag) ||
        !memory.write(0x120U, text)) {
        return fail("could not stage Android log write service fixture");
    }

    RecordingSink sink;
    sink.return_value = -1;
    A32AndroidLogWriteService service{
        kServiceId, sink, A32AndroidLogWriteOptions{8, 16}};
    const std::array<A32HostServiceRegistryEntry, 1> entries{{
        {kServiceId, &service},
    }};
    A32HostServiceRegistry registry{std::span{entries}};

    ExecutionRequest request{};
    request.regs[0] = 4U;
    request.regs[1] = 0x100U;
    request.regs[2] = 0x120U;
    request.regs[14] = kStopPc;
    request.instruction_count = 2;
    request.stop_pc = kStopPc;

    const auto result =
        execute_a32_with_services(memory, request, registry, 1);
    if (!result || !result.stop_pc_reached ||
        result.services_handled != 1 ||
        result.regs[0] != 0xFFFFFFFFU ||
        sink.calls != 1 || sink.priority != 4 ||
        sink.tag != std::optional<std::string>{"FMOD"} ||
        sink.text != "hello") {
        return fail("ARM log-write stub did not bridge through registry/service");
    }
    return 0;
}

int test_null_tag_and_exact_string_limits() {
    constexpr std::array<std::uint8_t, 2> empty_text{0, 0};

    LinearGuestMemory memory{kMemorySize};
    if (!memory.write(0x100U, empty_text)) {
        return fail("could not stage empty Android log text");
    }

    RecordingSink sink;
    A32AndroidLogWriteService service{
        kServiceId, sink, A32AndroidLogWriteOptions{0, 0}};

    std::array<std::uint32_t, 16> regs{};
    regs[0] = 0xFFFFFFFFU;
    regs[1] = 0;
    regs[2] = 0x100U;
    std::uint32_t cpsr = 0x10U;

    if (service.handle(memory, kServiceId, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        sink.calls != 1 || sink.priority != -1 ||
        sink.tag.has_value() || !sink.text.empty() ||
        regs[0] != 1U) {
        return fail("null tag or zero-length bounded text handling failed");
    }

    constexpr std::array<std::uint8_t, 4> exact_tag{
        'a', 'b', 'c', 0,
    };
    constexpr std::array<std::uint8_t, 2> exact_text{
        'x', 0,
    };
    if (!memory.write(0x110U, exact_tag) ||
        !memory.write(0x120U, exact_text)) {
        return fail("could not stage exact-limit Android log strings");
    }

    RecordingSink exact_sink;
    A32AndroidLogWriteService exact_service{
        kServiceId, exact_sink, A32AndroidLogWriteOptions{3, 1}};
    regs[0] = 5U;
    regs[1] = 0x110U;
    regs[2] = 0x120U;
    if (exact_service.handle(memory, kServiceId, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        exact_sink.tag != std::optional<std::string>{"abc"} ||
        exact_sink.text != "x") {
        return fail("exact payload ceilings rejected valid strings");
    }
    return 0;
}

int test_mismatch_and_guest_string_failures() {
    LinearGuestMemory memory{512};
    RecordingSink sink;
    A32AndroidLogWriteService service{
        kServiceId, sink, A32AndroidLogWriteOptions{3, 3}};

    std::array<std::uint32_t, 16> regs{};
    regs[0] = 4U;
    regs[1] = 0;
    regs[2] = 0x100U;
    std::uint32_t cpsr = 0x10U;

    if (service.handle(memory, 0xA1U, regs, cpsr) !=
            A32HostServiceDisposition::Unhandled ||
        sink.calls != 0) {
        return fail("service accepted the wrong SVC immediate");
    }

    regs[2] = 0;
    if (service.handle(memory, kServiceId, regs, cpsr) !=
            A32HostServiceDisposition::Failed ||
        sink.calls != 0) {
        return fail("null text pointer did not fail before sink invocation");
    }

    constexpr std::array<std::uint8_t, 4> no_nul{
        'a', 'b', 'c', 'd',
    };
    if (!memory.write(0x100U, no_nul)) {
        return fail("could not stage unterminated Android log text");
    }
    regs[2] = 0x100U;
    if (service.handle(memory, kServiceId, regs, cpsr) !=
            A32HostServiceDisposition::Failed ||
        sink.calls != 0) {
        return fail("over-limit unterminated text reached the sink");
    }

    regs[1] = 0x1000U;
    regs[2] = 0x100U;
    if (service.handle(memory, kServiceId, regs, cpsr) !=
            A32HostServiceDisposition::Failed ||
        sink.calls != 0) {
        return fail("unreadable tag pointer reached the sink");
    }
    return 0;
}


int test_print_varargs_register_stack_and_alignment() {
    LinearGuestMemory memory{kMemorySize};
    constexpr std::array<std::uint8_t, 4> tag{{'V','L','C',0}};
    constexpr std::array<std::uint8_t, 28> format{{
        's','=','%','s',' ','d','=','%','d',' ','x','=','%','#','x',' ',
        'f','=','%','.','1','f',0,0,0,0,0,0,
    }};
    constexpr std::array<std::uint8_t, 6> text{{'g','u','e','s','t',0}};
    if (!memory.write(0x100U, tag) ||
        !memory.write(0x120U, format) ||
        !memory.write(0x180U, text) ||
        !write_u32(memory, 0x200U, std::bit_cast<std::uint32_t>(-7)) ||
        !write_u32(memory, 0x204U, 42U) ||
        !write_u64(memory, 0x208U, std::bit_cast<std::uint64_t>(1.5))) {
        return fail("could not stage __android_log_print fixture");
    }

    RecordingSink sink;
    A32AndroidLogPrintService service{
        kPrintServiceId,
        kVprintServiceId,
        sink,
        format_options(),
    };
    std::array<std::uint32_t, 16> regs{};
    regs[0] = 4U;
    regs[1] = 0x100U;
    regs[2] = 0x120U;
    regs[3] = 0x180U;
    regs[13] = 0x200U;
    std::uint32_t cpsr{};

    if (service.handle(memory, kPrintServiceId, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 1U ||
        sink.calls != 1U ||
        sink.priority != 4 ||
        sink.tag != std::optional<std::string>{"VLC"} ||
        sink.text != "s=guest d=-7 x=0x2a f=1.5") {
        return fail("__android_log_print ARM32 varargs decoding failed");
    }
    return 0;
}

int test_vprint_va_list_memory_and_dynamic_fields() {
    LinearGuestMemory memory{kMemorySize};
    constexpr std::array<std::uint8_t, 4> tag{{'V','L','C',0}};
    constexpr std::array<std::uint8_t, 18> format{{
        '%','*','.','*','s','/','%','l','l','d','/','%','p',0,0,0,0,0,
    }};
    constexpr std::array<std::uint8_t, 7> text{{'a','b','c','d','e','f',0}};
    if (!memory.write(0x100U, tag) ||
        !memory.write(0x120U, format) ||
        !memory.write(0x180U, text) ||
        !write_u32(memory, 0x300U, 8U) ||
        !write_u32(memory, 0x304U, 3U) ||
        !write_u32(memory, 0x308U, 0x180U) ||
        !write_u64(
            memory,
            0x310U,
            std::bit_cast<std::uint64_t>(std::int64_t{-2})) ||
        !write_u32(memory, 0x318U, 0x1234U)) {
        return fail("could not stage __android_log_vprint fixture");
    }

    RecordingSink sink;
    A32AndroidLogPrintService service{
        kPrintServiceId,
        kVprintServiceId,
        sink,
        format_options(),
    };
    std::array<std::uint32_t, 16> regs{};
    regs[0] = 5U;
    regs[1] = 0x100U;
    regs[2] = 0x120U;
    regs[3] = 0x300U;
    std::uint32_t cpsr{};

    if (service.handle(memory, kVprintServiceId, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 1U ||
        sink.calls != 1U ||
        sink.priority != 5 ||
        sink.text != "     abc/-2/0x1234") {
        return fail("__android_log_vprint ARM32 va_list decoding failed");
    }
    return 0;
}

int test_format_rejection_and_output_bounds() {
    LinearGuestMemory memory{kMemorySize};
    constexpr std::array<std::uint8_t, 4> tag{{'V','L','C',0}};
    constexpr std::array<std::uint8_t, 3> unsafe_format{{'%','n',0}};
    constexpr std::array<std::uint8_t, 7> long_format{{
        'a','b','c','d','e','f',0,
    }};
    if (!memory.write(0x100U, tag) ||
        !memory.write(0x120U, unsafe_format) ||
        !memory.write(0x140U, long_format)) {
        return fail("could not stage bounded log format failures");
    }

    RecordingSink sink;
    A32AndroidLogPrintService service{
        kPrintServiceId,
        kVprintServiceId,
        sink,
        format_options(5U),
    };
    std::array<std::uint32_t, 16> regs{};
    regs[0] = 4U;
    regs[1] = 0x100U;
    regs[2] = 0x120U;
    regs[3] = 0x200U;
    regs[13] = 0x300U;
    std::uint32_t cpsr{};

    if (service.handle(memory, kPrintServiceId, regs, cpsr) !=
            A32HostServiceDisposition::Failed ||
        sink.calls != 0U) {
        return fail("%n was not rejected before sink invocation");
    }

    regs[2] = 0x140U;
    if (service.handle(memory, kPrintServiceId, regs, cpsr) !=
            A32HostServiceDisposition::Failed ||
        sink.calls != 0U) {
        return fail("formatted output ceiling was not enforced");
    }

    if (service.handle(memory, 0x135U, regs, cpsr) !=
            A32HostServiceDisposition::Unhandled) {
        return fail("log print service accepted unknown SVC");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_arm_stub_registry_and_service_path();
        status != 0) {
        return status;
    }
    if (const int status = test_null_tag_and_exact_string_limits();
        status != 0) {
        return status;
    }
    if (const int status = test_mismatch_and_guest_string_failures();
        status != 0) {
        return status;
    }
    if (const int status = test_print_varargs_register_stack_and_alignment();
        status != 0) {
        return status;
    }
    if (const int status = test_vprint_va_list_memory_and_dynamic_fields();
        status != 0) {
        return status;
    }
    return test_format_rejection_and_output_bounds();
}
