#include "compat/a32_jni.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace liba32android::compat {
namespace {

constexpr std::size_t kJavaVmBytes = 4U;
constexpr std::size_t kInvokeTableWords = 8U;
constexpr std::size_t kInvokeTableBytes = kInvokeTableWords * 4U;
constexpr std::size_t kJniEnvBytes = 4U;
constexpr std::size_t kNativeTableWords = 216U;
constexpr std::size_t kNativeTableBytes = kNativeTableWords * 4U;
constexpr std::size_t kFindClassSlot = 6U;
constexpr std::size_t kRegisterNativesSlot = 215U;
constexpr std::size_t kJniNativeMethodBytes = 12U;
constexpr std::size_t kServiceStubBytes = 8U;
constexpr std::size_t kMaxInstallRegionBytes = kNativeTableBytes;

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

[[nodiscard]] bool read_guest_u32(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::uint32_t& value) {
    if (static_cast<std::uint64_t>(address) + 4U >
        (std::uint64_t{1} << 32U)) {
        return false;
    }
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) {
        return false;
    }
    value = static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) |
            (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return true;
}

[[nodiscard]] bool read_guest_c_string(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::size_t max_payload_bytes,
    std::string& output) {
    output.clear();
    if (address == 0U ||
        max_payload_bytes == 0U ||
        max_payload_bytes > kA32JniHardMaxStringBytes) {
        return false;
    }

    output.reserve(std::min<std::size_t>(max_payload_bytes, 64U));
    for (std::size_t offset = 0U;
         offset <= max_payload_bytes;
         ++offset) {
        const std::uint64_t guest =
            static_cast<std::uint64_t>(address) + offset;
        if (guest > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        std::uint8_t byte{};
        if (!memory.read(
                static_cast<std::uint32_t>(guest),
                std::span<std::uint8_t>{&byte, 1U})) {
            return false;
        }
        if (byte == 0U) {
            return true;
        }
        if (offset == max_payload_bytes) {
            return false;
        }
        output.push_back(static_cast<char>(byte));
    }
    return false;
}

void write_service_stub(
    std::span<std::uint8_t> output,
    std::uint32_t svc_immediate) noexcept {
    output[0] = static_cast<std::uint8_t>(svc_immediate);
    output[1] = static_cast<std::uint8_t>(svc_immediate >> 8U);
    output[2] = static_cast<std::uint8_t>(svc_immediate >> 16U);
    output[3] = 0xEFU;
    output[4] = 0x1EU;
    output[5] = 0xFFU;
    output[6] = 0x2FU;
    output[7] = 0xE1U;
}

[[nodiscard]] bool is_zero_argument_signature(
    std::string_view signature) noexcept {
    return signature.size() >= 3U &&
           signature[0] == '(' &&
           signature[1] == ')';
}

struct InstallRegion {
    std::uint32_t address{};
    std::size_t size{};
    std::array<std::uint8_t, kMaxInstallRegionBytes> desired{};
    std::array<std::uint8_t, kMaxInstallRegionBytes> original{};
};

class JniExecutionContextScope final {
public:
    JniExecutionContextScope(
        elf::Elf32LifecycleExecutionContext* context,
        std::size_t object_index) noexcept
        : context_(context) {
        if (context_ != nullptr) {
            previous_ = context_->object_index;
            context_->object_index = object_index;
        }
    }

    ~JniExecutionContextScope() {
        if (context_ != nullptr) {
            context_->object_index = previous_;
        }
    }

    JniExecutionContextScope(
        const JniExecutionContextScope&) = delete;
    JniExecutionContextScope& operator=(
        const JniExecutionContextScope&) = delete;

private:
    elf::Elf32LifecycleExecutionContext* context_{};
    std::optional<std::size_t> previous_;
};

[[nodiscard]] A32JniOnLoadResult onload_failure(
    A32JniOnLoadError error) {
    A32JniOnLoadResult result;
    result.error = error;
    return result;
}

[[nodiscard]] A32JniNativeInvokeResult native_invoke_failure(
    A32JniNativeInvokeError error) {
    A32JniNativeInvokeResult result;
    result.error = error;
    return result;
}

}  // namespace

bool A32JniClassRegistry::valid() const noexcept {
    return limits_.max_classes > 0U &&
           limits_.max_classes <= kA32JniHardMaxClasses &&
           limits_.max_registered_methods > 0U &&
           limits_.max_registered_methods <=
               kA32JniHardMaxRegisteredMethods &&
           limits_.max_methods_per_registration > 0U &&
           limits_.max_methods_per_registration <=
               kA32JniHardMaxMethodsPerRegistration &&
           limits_.max_methods_per_registration <=
               limits_.max_registered_methods &&
           limits_.max_class_name_bytes > 0U &&
           limits_.max_class_name_bytes <=
               kA32JniHardMaxStringBytes &&
           limits_.max_method_name_bytes > 0U &&
           limits_.max_method_name_bytes <=
               kA32JniHardMaxStringBytes &&
           limits_.max_signature_bytes > 0U &&
           limits_.max_signature_bytes <=
               kA32JniHardMaxStringBytes;
}

A32JniRegistryError A32JniClassRegistry::add_class(
    std::uint32_t handle,
    std::string_view name) {
    if (!valid()) {
        return A32JniRegistryError::InvalidLimits;
    }
    if (handle == 0U) {
        return A32JniRegistryError::InvalidClassHandle;
    }
    if (name.empty() ||
        name.size() > limits_.max_class_name_bytes) {
        return A32JniRegistryError::InvalidName;
    }
    if (classes_.size() >= limits_.max_classes) {
        return A32JniRegistryError::ClassLimitExceeded;
    }
    for (const ClassEntry& entry : classes_) {
        if (entry.handle == handle) {
            return A32JniRegistryError::DuplicateClassHandle;
        }
        if (entry.name == name) {
            return A32JniRegistryError::DuplicateClassName;
        }
    }

    ClassEntry entry;
    entry.handle = handle;
    entry.name.assign(name);
    classes_.push_back(std::move(entry));
    return A32JniRegistryError::None;
}

std::optional<std::uint32_t> A32JniClassRegistry::find_class(
    std::string_view name) const noexcept {
    for (const ClassEntry& entry : classes_) {
        if (entry.name == name) {
            return entry.handle;
        }
    }
    return std::nullopt;
}

bool A32JniClassRegistry::contains_class_handle(
    std::uint32_t handle) const noexcept {
    return find_class_entry(handle) != nullptr;
}

const A32JniClassRegistry::ClassEntry*
A32JniClassRegistry::find_class_entry(
    std::uint32_t handle) const noexcept {
    for (const ClassEntry& entry : classes_) {
        if (entry.handle == handle) {
            return &entry;
        }
    }
    return nullptr;
}

const A32JniRegisteredNative* A32JniClassRegistry::find_native(
    std::uint32_t class_handle,
    std::string_view name,
    std::string_view signature) const noexcept {
    for (const A32JniRegisteredNative& method : natives_) {
        if (method.class_handle == class_handle &&
            method.name == name &&
            method.signature == signature) {
            return &method;
        }
    }
    return nullptr;
}

A32JniRegistryError A32JniClassRegistry::register_natives(
    std::uint32_t class_handle,
    std::vector<A32JniRegisteredNative> pending) {
    if (!valid()) {
        return A32JniRegistryError::InvalidLimits;
    }
    const ClassEntry* class_entry = find_class_entry(class_handle);
    if (class_entry == nullptr) {
        return A32JniRegistryError::UnknownClass;
    }
    if (pending.size() > limits_.max_methods_per_registration) {
        return A32JniRegistryError::MethodLimitExceeded;
    }

    for (std::size_t left = 0U; left < pending.size(); ++left) {
        A32JniRegisteredNative& method = pending[left];
        if (method.class_handle != class_handle ||
            method.function == 0U ||
            method.function ==
                std::numeric_limits<std::uint32_t>::max() ||
            method.name.empty() ||
            method.name.size() > limits_.max_method_name_bytes ||
            method.signature.empty() ||
            method.signature.size() > limits_.max_signature_bytes) {
            return A32JniRegistryError::InvalidMethod;
        }
        for (std::size_t right = left + 1U;
             right < pending.size();
             ++right) {
            if (pending[right].class_handle == method.class_handle &&
                pending[right].name == method.name &&
                pending[right].signature == method.signature) {
                return A32JniRegistryError::DuplicateMethod;
            }
        }
        method.class_name = class_entry->name;
    }

    std::size_t additions = 0U;
    for (const A32JniRegisteredNative& method : pending) {
        if (find_native(
                method.class_handle,
                method.name,
                method.signature) == nullptr) {
            ++additions;
        }
    }
    if (additions >
            limits_.max_registered_methods - natives_.size()) {
        return A32JniRegistryError::MethodLimitExceeded;
    }

    std::vector<A32JniRegisteredNative> updated = natives_;
    updated.reserve(updated.size() + additions);
    for (A32JniRegisteredNative& method : pending) {
        auto found = std::find_if(
            updated.begin(),
            updated.end(),
            [&](const A32JniRegisteredNative& current) {
                return current.class_handle == method.class_handle &&
                       current.name == method.name &&
                       current.signature == method.signature;
            });
        if (found == updated.end()) {
            updated.push_back(std::move(method));
        } else {
            *found = std::move(method);
        }
    }
    natives_.swap(updated);
    return A32JniRegistryError::None;
}

bool A32JniVmService::layout_valid() const noexcept {
    const std::array<AddressRange, 7> ranges{{
        {layout_.java_vm_address, kJavaVmBytes},
        {layout_.invoke_table_address, kInvokeTableBytes},
        {layout_.jni_env_address, kJniEnvBytes},
        {layout_.native_table_address, kNativeTableBytes},
        {layout_.get_env_stub_address, kServiceStubBytes},
        {layout_.find_class_stub_address, kServiceStubBytes},
        {layout_.register_natives_stub_address, kServiceStubBytes},
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

    std::array<InstallRegion, 7> regions{{
        {.address = layout_.java_vm_address, .size = kJavaVmBytes},
        {.address = layout_.invoke_table_address,
         .size = kInvokeTableBytes},
        {.address = layout_.jni_env_address, .size = kJniEnvBytes},
        {.address = layout_.native_table_address,
         .size = kNativeTableBytes},
        {.address = layout_.get_env_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.find_class_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.register_natives_stub_address,
         .size = kServiceStubBytes},
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
    write_u32(
        regions[3].desired,
        kFindClassSlot * 4U,
        layout_.find_class_stub_address);
    write_u32(
        regions[3].desired,
        kRegisterNativesSlot * 4U,
        layout_.register_natives_stub_address);

    write_service_stub(
        regions[4].desired,
        kA32JniGetEnvSvcImmediate);
    write_service_stub(
        regions[5].desired,
        kA32JniFindClassSvcImmediate);
    write_service_stub(
        regions[6].desired,
        kA32JniRegisterNativesSvcImmediate);

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
    const bool known_service =
        svc_immediate == kA32JniGetEnvSvcImmediate ||
        svc_immediate == kA32JniFindClassSvcImmediate ||
        svc_immediate == kA32JniRegisterNativesSvcImmediate;
    if (!known_service) {
        return runtime::A32HostServiceDisposition::Unhandled;
    }
    if (!installed_) {
        return runtime::A32HostServiceDisposition::Failed;
    }

    if (svc_immediate == kA32JniGetEnvSvcImmediate) {
        if (regs[0] != layout_.java_vm_address) {
            return runtime::A32HostServiceDisposition::Failed;
        }

        // Dalvik validates the version before touching *env. Preserve the
        // caller's output slot on JNI_EVERSION.
        if (!is_a32_supported_jni_getenv_version(regs[2])) {
            regs[0] = jint_bits(kA32JniEversion);
            return runtime::A32HostServiceDisposition::Handled;
        }
        if (regs[1] == 0U) {
            return runtime::A32HostServiceDisposition::Failed;
        }

        const auto env_bytes =
            u32_bytes(layout_.jni_env_address);
        if (!memory.write(regs[1], env_bytes)) {
            return runtime::A32HostServiceDisposition::Failed;
        }

        regs[0] = jint_bits(kA32JniOk);
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (regs[0] != layout_.jni_env_address) {
        return runtime::A32HostServiceDisposition::Failed;
    }

    if (svc_immediate == kA32JniFindClassSvcImmediate) {
        if (registry_ == nullptr) {
            regs[0] = 0U;
            return runtime::A32HostServiceDisposition::Handled;
        }
        if (!registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }

        std::string name;
        if (!read_guest_c_string(
                memory,
                regs[1],
                registry_->limits().max_class_name_bytes,
                name)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const auto found = registry_->find_class(name);
        regs[0] = found.value_or(0U);
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (registry_ == nullptr) {
        regs[0] = jint_bits(kA32JniErr);
        return runtime::A32HostServiceDisposition::Handled;
    }
    if (!registry_->valid()) {
        return runtime::A32HostServiceDisposition::Failed;
    }
    if (!registry_->contains_class_handle(regs[1])) {
        regs[0] = jint_bits(kA32JniErr);
        return runtime::A32HostServiceDisposition::Handled;
    }

    const std::int32_t signed_count =
        static_cast<std::int32_t>(regs[3]);
    if (signed_count < 0) {
        regs[0] = jint_bits(kA32JniErr);
        return runtime::A32HostServiceDisposition::Handled;
    }
    const std::size_t count =
        static_cast<std::size_t>(signed_count);
    if (count == 0U) {
        regs[0] = jint_bits(kA32JniOk);
        return runtime::A32HostServiceDisposition::Handled;
    }
    const A32JniRegistryLimits limits = registry_->limits();
    if (count > limits.max_methods_per_registration ||
        regs[2] == 0U) {
        regs[0] = jint_bits(kA32JniErr);
        return runtime::A32HostServiceDisposition::Handled;
    }

    const std::uint64_t array_end =
        static_cast<std::uint64_t>(regs[2]) +
        static_cast<std::uint64_t>(count) *
            kJniNativeMethodBytes;
    if (array_end > (std::uint64_t{1} << 32U)) {
        regs[0] = jint_bits(kA32JniErr);
        return runtime::A32HostServiceDisposition::Handled;
    }

    std::vector<A32JniRegisteredNative> pending;
    pending.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const std::uint64_t record64 =
            static_cast<std::uint64_t>(regs[2]) +
            index * kJniNativeMethodBytes;
        const auto record =
            static_cast<std::uint32_t>(record64);

        std::uint32_t name_pointer{};
        std::uint32_t signature_pointer{};
        std::uint32_t function{};
        if (!read_guest_u32(
                memory, record, name_pointer) ||
            !read_guest_u32(
                memory, record + 4U, signature_pointer) ||
            !read_guest_u32(
                memory, record + 8U, function)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (name_pointer == 0U ||
            signature_pointer == 0U ||
            function == 0U ||
            function ==
                std::numeric_limits<std::uint32_t>::max()) {
            regs[0] = jint_bits(kA32JniErr);
            return runtime::A32HostServiceDisposition::Handled;
        }

        A32JniRegisteredNative method;
        method.class_handle = regs[1];
        method.function = function;
        if (!read_guest_c_string(
                memory,
                name_pointer,
                limits.max_method_name_bytes,
                method.name) ||
            !read_guest_c_string(
                memory,
                signature_pointer,
                limits.max_signature_bytes,
                method.signature)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (method.name.empty() ||
            method.signature.empty()) {
            regs[0] = jint_bits(kA32JniErr);
            return runtime::A32HostServiceDisposition::Handled;
        }
        pending.push_back(std::move(method));
    }

    const A32JniRegistryError registered =
        registry_->register_natives(
            regs[1],
            std::move(pending));
    regs[0] = jint_bits(
        registered == A32JniRegistryError::None
            ? kA32JniOk
            : kA32JniErr);
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

    JniExecutionContextScope context_scope{
        options.execution_context,
        object_index};

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

A32JniNativeInvokeResult
invoke_a32_registered_native_noargs(
    memory::GuestMemory& memory,
    const A32JniClassRegistry& registry,
    std::uint32_t class_handle,
    std::string_view name,
    std::string_view signature,
    std::uint32_t jni_env_address,
    std::uint32_t receiver_or_class,
    runtime::A32HostServiceHandler& service_handler,
    const A32JniNativeInvokeOptions& options) {
    if (!registry.valid() ||
        class_handle == 0U ||
        jni_env_address == 0U ||
        (jni_env_address & 3U) != 0U ||
        receiver_or_class == 0U ||
        options.stack_top == 0U ||
        (options.stack_top & 7U) != 0U ||
        (options.return_pc & 3U) != 0U ||
        options.max_instructions == 0U ||
        options.max_service_calls == 0U) {
        return native_invoke_failure(
            A32JniNativeInvokeError::InvalidOptions);
    }
    if (!registry.contains_class_handle(class_handle)) {
        return native_invoke_failure(
            A32JniNativeInvokeError::ClassNotFound);
    }

    const A32JniRegisteredNative* method =
        registry.find_native(
            class_handle,
            name,
            signature);
    if (method == nullptr) {
        return native_invoke_failure(
            A32JniNativeInvokeError::NativeNotFound);
    }
    if (!is_zero_argument_signature(method->signature)) {
        return native_invoke_failure(
            A32JniNativeInvokeError::NonZeroArgumentSignature);
    }

    const std::uint32_t function = method->function;
    const bool thumb = (function & 1U) != 0U;
    const std::uint32_t entry_pc = function & ~1U;
    if (function == 0U ||
        function == std::numeric_limits<std::uint32_t>::max() ||
        entry_pc == options.return_pc ||
        (!thumb && (entry_pc & 3U) != 0U)) {
        return native_invoke_failure(
            A32JniNativeInvokeError::InvalidFunctionAddress);
    }

    cpu::ExecutionRequest request{};
    request.instruction_set =
        thumb ? cpu::InstructionSet::Thumb
              : cpu::InstructionSet::Arm;
    request.entry_pc = entry_pc;
    request.regs[0] = jni_env_address;
    request.regs[1] = receiver_or_class;
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

    A32JniNativeInvokeResult result;
    result.function = function;
    result.execution = execution;
    if (execution.service_suspended) {
        result.error =
            A32JniNativeInvokeError::ServiceSuspended;
        result.failing_svc_immediate =
            execution.suspended_svc_immediate;
        return result;
    }

    switch (execution.error) {
    case runtime::A32ServiceDispatchError::None:
        break;
    case runtime::A32ServiceDispatchError::MemoryFault:
        result.error = A32JniNativeInvokeError::MemoryFault;
        break;
    case runtime::A32ServiceDispatchError::CpuException:
        result.error = A32JniNativeInvokeError::CpuException;
        break;
    case runtime::A32ServiceDispatchError::ServiceLimitExceeded:
        result.error =
            A32JniNativeInvokeError::ServiceLimitExceeded;
        break;
    case runtime::A32ServiceDispatchError::ServiceUnhandled:
        result.error =
            A32JniNativeInvokeError::ServiceUnhandled;
        break;
    case runtime::A32ServiceDispatchError::ServiceFailed:
        result.error = A32JniNativeInvokeError::ServiceFailed;
        break;
    case runtime::A32ServiceDispatchError::InstructionLimitExceeded:
        result.error =
            A32JniNativeInvokeError::InstructionLimitExceeded;
        break;
    }
    if (result.error != A32JniNativeInvokeError::None) {
        result.failing_svc_immediate =
            execution.failing_svc_immediate;
        return result;
    }
    if (!execution.stop_pc_reached) {
        result.error =
            A32JniNativeInvokeError::InstructionLimitExceeded;
        return result;
    }

    result.returned_value = execution.regs[0];
    return result;
}

const char* to_string(
    A32JniRegistryError error) noexcept {
    switch (error) {
    case A32JniRegistryError::None:
        return "none";
    case A32JniRegistryError::InvalidLimits:
        return "invalid_limits";
    case A32JniRegistryError::InvalidClassHandle:
        return "invalid_class_handle";
    case A32JniRegistryError::InvalidName:
        return "invalid_name";
    case A32JniRegistryError::ClassLimitExceeded:
        return "class_limit_exceeded";
    case A32JniRegistryError::DuplicateClassName:
        return "duplicate_class_name";
    case A32JniRegistryError::DuplicateClassHandle:
        return "duplicate_class_handle";
    case A32JniRegistryError::UnknownClass:
        return "unknown_class";
    case A32JniRegistryError::InvalidMethod:
        return "invalid_method";
    case A32JniRegistryError::MethodLimitExceeded:
        return "method_limit_exceeded";
    case A32JniRegistryError::DuplicateMethod:
        return "duplicate_method";
    }
    return "unknown";
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

const char* to_string(
    A32JniNativeInvokeError error) noexcept {
    switch (error) {
    case A32JniNativeInvokeError::None:
        return "none";
    case A32JniNativeInvokeError::InvalidOptions:
        return "invalid_options";
    case A32JniNativeInvokeError::ClassNotFound:
        return "class_not_found";
    case A32JniNativeInvokeError::NativeNotFound:
        return "native_not_found";
    case A32JniNativeInvokeError::NonZeroArgumentSignature:
        return "nonzero_argument_signature";
    case A32JniNativeInvokeError::InvalidFunctionAddress:
        return "invalid_function_address";
    case A32JniNativeInvokeError::MemoryFault:
        return "memory_fault";
    case A32JniNativeInvokeError::CpuException:
        return "cpu_exception";
    case A32JniNativeInvokeError::ServiceLimitExceeded:
        return "service_limit_exceeded";
    case A32JniNativeInvokeError::ServiceUnhandled:
        return "service_unhandled";
    case A32JniNativeInvokeError::ServiceFailed:
        return "service_failed";
    case A32JniNativeInvokeError::ServiceSuspended:
        return "service_suspended";
    case A32JniNativeInvokeError::InstructionLimitExceeded:
        return "instruction_limit_exceeded";
    }
    return "unknown";
}

}  // namespace liba32android::compat
