#include "compat/a32_jni.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace liba32android::compat {
namespace {

constexpr std::size_t kJavaVmBytes = 4U;
constexpr std::size_t kInvokeTableWords = 8U;
constexpr std::size_t kInvokeTableBytes = kInvokeTableWords * 4U;
constexpr std::size_t kJniEnvBytes = 4U;
constexpr std::size_t kNativeTableWords = 5U;
constexpr std::size_t kNativeTableBytes = kNativeTableWords * 4U;
constexpr std::size_t kGetEnvStubBytes = 8U;
constexpr std::size_t kMaxInstallRegionBytes = kInvokeTableBytes;

struct AddressRange {
    std::uint32_t address{};
    std::size_t size{};
};

[[nodiscard]] bool valid_range(
    const AddressRange& range) noexcept {
    if (range.address == 0U ||
        (range.address & 3U) != 0U ||
        range.size == 0U) {
        return false;
    }
    return static_cast<std::uint64_t>(range.address) +
               static_cast<std::uint64_t>(range.size) <=
           (std::uint64_t{1} << 32U);
}

[[nodiscard]] bool overlaps(
    const AddressRange& left,
    const AddressRange& right) noexcept {
    const std::uint64_t left_begin = left.address;
    const std::uint64_t left_end = left_begin + left.size;
    const std::uint64_t right_begin = right.address;
    const std::uint64_t right_end = right_begin + right.size;
    return left_begin < right_end && right_begin < left_end;
}

void write_u32(
    std::span<std::uint8_t> output,
    std::size_t offset,
    std::uint32_t value) noexcept {
    output[offset + 0U] = static_cast<std::uint8_t>(value);
    output[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    output[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    output[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

[[nodiscard]] std::array<std::uint8_t, 4> u32_bytes(
    std::uint32_t value) noexcept {
    return {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U),
    };
}

[[nodiscard]] std::uint32_t jint_bits(
    std::int32_t value) noexcept {
    return static_cast<std::uint32_t>(value);
}

struct InstallRegion {
    std::uint32_t address{};
    std::size_t size{};
    std::array<std::uint8_t, kMaxInstallRegionBytes> desired{};
    std::array<std::uint8_t, kMaxInstallRegionBytes> original{};
};

[[nodiscard]] A32JniOnLoadResult onload_failure(
    A32JniOnLoadError error) {
    A32JniOnLoadResult result;
    result.error = error;
    return result;
}

}  // namespace

bool A32JniVmService::layout_valid() const noexcept {
    const std::array<AddressRange, 5> ranges{{
        {layout_.java_vm_address, kJavaVmBytes},
        {layout_.invoke_table_address, kInvokeTableBytes},
        {layout_.jni_env_address, kJniEnvBytes},
        {layout_.native_table_address, kNativeTableBytes},
        {layout_.get_env_stub_address, kGetEnvStubBytes},
    }};

    for (const AddressRange& range : ranges) {
        if (!valid_range(range)) {
            return false;
        }
    }
    for (std::size_t left = 0U; left < ranges.size(); ++left) {
        for (std::size_t right = left + 1U;
             right < ranges.size();
             ++right) {
            if (overlaps(ranges[left], ranges[right])) {
                return false;
            }
        }
    }
    return true;
}

A32JniVmInstallResult A32JniVmService::install(
    memory::GuestMemory& memory) {
    installed_ = false;
    if (!layout_valid()) {
        return {.error = A32JniVmInstallError::InvalidLayout};
    }

    std::array<InstallRegion, 5> regions{{
        {.address = layout_.java_vm_address, .size = kJavaVmBytes},
        {.address = layout_.invoke_table_address,
         .size = kInvokeTableBytes},
        {.address = layout_.jni_env_address, .size = kJniEnvBytes},
        {.address = layout_.native_table_address,
         .size = kNativeTableBytes},
        {.address = layout_.get_env_stub_address,
         .size = kGetEnvStubBytes},
    }};

    write_u32(
        regions[0].desired,
        0U,
        layout_.invoke_table_address);
    write_u32(
        regions[1].desired,
        6U * 4U,
        layout_.get_env_stub_address);
    write_u32(
        regions[2].desired,
        0U,
        layout_.native_table_address);

    // ARM: svc #0xd7; bx lr
    regions[4].desired[0] =
        static_cast<std::uint8_t>(kA32JniGetEnvSvcImmediate);
    regions[4].desired[1] = 0x00U;
    regions[4].desired[2] = 0x00U;
    regions[4].desired[3] = 0xEFU;
    regions[4].desired[4] = 0x1EU;
    regions[4].desired[5] = 0xFFU;
    regions[4].desired[6] = 0x2FU;
    regions[4].desired[7] = 0xE1U;

    for (InstallRegion& region : regions) {
        if (!memory.read(
                region.address,
                std::span<std::uint8_t>{
                    region.original.data(),
                    region.size})) {
            return {.error = A32JniVmInstallError::SnapshotFailed};
        }
    }

    std::size_t written = 0U;
    for (; written < regions.size(); ++written) {
        const InstallRegion& region = regions[written];
        if (memory.write(
                region.address,
                std::span<const std::uint8_t>{
                    region.desired.data(),
                    region.size})) {
            continue;
        }

        bool rollback_ok = true;
        for (std::size_t index = 0U; index < written; ++index) {
            const InstallRegion& previous = regions[index];
            if (!memory.write(
                    previous.address,
                    std::span<const std::uint8_t>{
                        previous.original.data(),
                        previous.size})) {
                rollback_ok = false;
            }
        }
        return {
            .error = rollback_ok
                ? A32JniVmInstallError::WriteFailed
                : A32JniVmInstallError::RollbackFailed,
        };
    }

    installed_ = true;
    return {};
}

runtime::A32HostServiceDisposition A32JniVmService::handle(
    memory::GuestMemory& memory,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    if (svc_immediate != kA32JniGetEnvSvcImmediate) {
        return runtime::A32HostServiceDisposition::Unhandled;
    }
    if (!installed_ ||
        regs[0] != layout_.java_vm_address ||
        regs[1] == 0U) {
        return runtime::A32HostServiceDisposition::Failed;
    }

    const bool supported =
        is_a32_supported_jni_getenv_version(regs[2]);
    const auto env_bytes = u32_bytes(
        supported ? layout_.jni_env_address : 0U);
    if (!memory.write(regs[1], env_bytes)) {
        return runtime::A32HostServiceDisposition::Failed;
    }

    regs[0] = jint_bits(
        supported ? kA32JniOk : kA32JniEversion);
    return runtime::A32HostServiceDisposition::Handled;
}

A32JniOnLoadResult invoke_a32_jni_on_load(
    memory::GuestMemory& memory,
    const elf::Elf32DependencyGraph& graph,
    std::size_t object_index,
    std::uint32_t java_vm_address,
    runtime::A32HostServiceHandler& service_handler,
    const A32JniOnLoadOptions& options) {
    if (java_vm_address == 0U ||
        (java_vm_address & 3U) != 0U ||
        options.stack_top == 0U ||
        (options.stack_top & 7U) != 0U ||
        (options.return_pc & 3U) != 0U ||
        options.max_instructions == 0U ||
        options.max_service_calls == 0U) {
        return onload_failure(
            A32JniOnLoadError::InvalidOptions);
    }
    if (object_index >= graph.objects.size()) {
        return onload_failure(
            A32JniOnLoadError::InvalidObject);
    }

    const auto& object = graph.objects[object_index];
    const auto index = elf::build_elf32_symbol_index(
        memory,
        object.linker_metadata,
        options.symbols);
    if (!index) {
        auto result = onload_failure(
            A32JniOnLoadError::SymbolIndexFailed);
        result.index_error = index.error;
        return result;
    }

    const auto lookup = elf::lookup_elf32_symbol(
        memory,
        object.load.load_bias,
        object.linker_metadata,
        index.index,
        "JNI_OnLoad",
        options.symbols);
    if (!lookup) {
        auto result = onload_failure(
            A32JniOnLoadError::SymbolLookupFailed);
        result.lookup_error = lookup.error;
        result.string_error = lookup.string_error;
        return result;
    }

    constexpr std::uint8_t kSttFunc = 2U;
    const std::uint32_t function = lookup.symbol.guest_value;
    const bool thumb = (function & 1U) != 0U;
    const std::uint32_t entry_pc = function & ~1U;
    if (lookup.symbol.symbol.type != kSttFunc ||
        function == 0U ||
        function == std::numeric_limits<std::uint32_t>::max() ||
        entry_pc == options.return_pc ||
        (!thumb && (entry_pc & 3U) != 0U)) {
        return onload_failure(
            A32JniOnLoadError::InvalidFunctionAddress);
    }

    cpu::ExecutionRequest request{};
    request.instruction_set =
        thumb ? cpu::InstructionSet::Thumb
              : cpu::InstructionSet::Arm;
    request.entry_pc = entry_pc;
    request.regs[0] = java_vm_address;
    request.regs[1] = 0U;
    request.regs[13] = options.stack_top;
    request.regs[14] =
        options.return_pc | (thumb ? 1U : 0U);
    request.instruction_count = options.max_instructions;
    request.stop_pc = options.return_pc;

    auto execution = runtime::execute_a32_with_services(
        memory,
        request,
        service_handler,
        options.max_service_calls);

    A32JniOnLoadResult result;
    result.execution = execution;
    if (execution.service_suspended) {
        result.error = A32JniOnLoadError::ServiceSuspended;
        result.failing_svc_immediate =
            execution.suspended_svc_immediate;
        return result;
    }

    switch (execution.error) {
    case runtime::A32ServiceDispatchError::None:
        break;
    case runtime::A32ServiceDispatchError::MemoryFault:
        result.error = A32JniOnLoadError::MemoryFault;
        break;
    case runtime::A32ServiceDispatchError::CpuException:
        result.error = A32JniOnLoadError::CpuException;
        break;
    case runtime::A32ServiceDispatchError::ServiceLimitExceeded:
        result.error = A32JniOnLoadError::ServiceLimitExceeded;
        break;
    case runtime::A32ServiceDispatchError::ServiceUnhandled:
        result.error = A32JniOnLoadError::ServiceUnhandled;
        break;
    case runtime::A32ServiceDispatchError::ServiceFailed:
        result.error = A32JniOnLoadError::ServiceFailed;
        break;
    case runtime::A32ServiceDispatchError::InstructionLimitExceeded:
        result.error = A32JniOnLoadError::InstructionLimitExceeded;
        break;
    }
    if (result.error != A32JniOnLoadError::None) {
        result.failing_svc_immediate =
            execution.failing_svc_immediate;
        return result;
    }
    if (!execution.stop_pc_reached) {
        result.error =
            A32JniOnLoadError::InstructionLimitExceeded;
        return result;
    }

    result.returned_version = execution.regs[0];
    if (!is_a32_supported_jni_onload_version(
            result.returned_version)) {
        result.error =
            A32JniOnLoadError::UnsupportedVersion;
    }
    return result;
}

const char* to_string(
    A32JniVmInstallError error) noexcept {
    switch (error) {
    case A32JniVmInstallError::None:
        return "none";
    case A32JniVmInstallError::InvalidLayout:
        return "invalid_layout";
    case A32JniVmInstallError::SnapshotFailed:
        return "snapshot_failed";
    case A32JniVmInstallError::WriteFailed:
        return "write_failed";
    case A32JniVmInstallError::RollbackFailed:
        return "rollback_failed";
    }
    return "unknown";
}

const char* to_string(
    A32JniOnLoadError error) noexcept {
    switch (error) {
    case A32JniOnLoadError::None:
        return "none";
    case A32JniOnLoadError::InvalidOptions:
        return "invalid_options";
    case A32JniOnLoadError::InvalidObject:
        return "invalid_object";
    case A32JniOnLoadError::SymbolIndexFailed:
        return "symbol_index_failed";
    case A32JniOnLoadError::SymbolLookupFailed:
        return "symbol_lookup_failed";
    case A32JniOnLoadError::InvalidFunctionAddress:
        return "invalid_function_address";
    case A32JniOnLoadError::MemoryFault:
        return "memory_fault";
    case A32JniOnLoadError::CpuException:
        return "cpu_exception";
    case A32JniOnLoadError::ServiceLimitExceeded:
        return "service_limit_exceeded";
    case A32JniOnLoadError::ServiceUnhandled:
        return "service_unhandled";
    case A32JniOnLoadError::ServiceFailed:
        return "service_failed";
    case A32JniOnLoadError::ServiceSuspended:
        return "service_suspended";
    case A32JniOnLoadError::InstructionLimitExceeded:
        return "instruction_limit_exceeded";
    case A32JniOnLoadError::UnsupportedVersion:
        return "unsupported_version";
    }
    return "unknown";
}

}  // namespace liba32android::compat
