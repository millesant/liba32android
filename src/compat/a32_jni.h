#pragma once

#define LIBA32ANDROID_A32_JNI_GET_ENV_SVC 0xD7
#define LIBA32ANDROID_A32_JNI_FIND_CLASS_SVC 0xD8
#define LIBA32ANDROID_A32_JNI_REGISTER_NATIVES_SVC 0xD9
#define LIBA32ANDROID_A32_JNI_GET_METHOD_ID_SVC 0xDA
#define LIBA32ANDROID_A32_JNI_GET_FIELD_ID_SVC 0xDB
#define LIBA32ANDROID_A32_JNI_GET_STATIC_FIELD_ID_SVC 0xDC
#define LIBA32ANDROID_A32_JNI_ATTACH_CURRENT_THREAD_SVC 0xDD
#define LIBA32ANDROID_A32_JNI_DETACH_CURRENT_THREAD_SVC 0xDE
#define LIBA32ANDROID_A32_JNI_NEW_GLOBAL_REF_SVC 0xDF
#define LIBA32ANDROID_A32_JNI_DELETE_GLOBAL_REF_SVC 0xE0
#define LIBA32ANDROID_A32_JNI_DELETE_LOCAL_REF_SVC 0xE1
#define LIBA32ANDROID_A32_JNI_GET_ARRAY_LENGTH_SVC 0xE2
#define LIBA32ANDROID_A32_JNI_GET_STATIC_INT_FIELD_SVC 0xE3

#ifdef __cplusplus

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cpu/a32_cpu.h"
#include "elf/elf32_dependency_graph.h"
#include "elf/elf32_lifecycle.h"
#include "elf/elf32_symbol_lookup.h"
#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

inline constexpr std::uint32_t kA32JniGetEnvSvcImmediate =
    LIBA32ANDROID_A32_JNI_GET_ENV_SVC;
inline constexpr std::uint32_t kA32JniFindClassSvcImmediate =
    LIBA32ANDROID_A32_JNI_FIND_CLASS_SVC;
inline constexpr std::uint32_t kA32JniRegisterNativesSvcImmediate =
    LIBA32ANDROID_A32_JNI_REGISTER_NATIVES_SVC;
inline constexpr std::uint32_t kA32JniGetMethodIdSvcImmediate =
    LIBA32ANDROID_A32_JNI_GET_METHOD_ID_SVC;
inline constexpr std::uint32_t kA32JniGetFieldIdSvcImmediate =
    LIBA32ANDROID_A32_JNI_GET_FIELD_ID_SVC;
inline constexpr std::uint32_t kA32JniGetStaticFieldIdSvcImmediate =
    LIBA32ANDROID_A32_JNI_GET_STATIC_FIELD_ID_SVC;
inline constexpr std::uint32_t kA32JniAttachCurrentThreadSvcImmediate =
    LIBA32ANDROID_A32_JNI_ATTACH_CURRENT_THREAD_SVC;
inline constexpr std::uint32_t kA32JniDetachCurrentThreadSvcImmediate =
    LIBA32ANDROID_A32_JNI_DETACH_CURRENT_THREAD_SVC;
inline constexpr std::uint32_t kA32JniNewGlobalRefSvcImmediate =
    LIBA32ANDROID_A32_JNI_NEW_GLOBAL_REF_SVC;
inline constexpr std::uint32_t kA32JniDeleteGlobalRefSvcImmediate =
    LIBA32ANDROID_A32_JNI_DELETE_GLOBAL_REF_SVC;
inline constexpr std::uint32_t kA32JniDeleteLocalRefSvcImmediate =
    LIBA32ANDROID_A32_JNI_DELETE_LOCAL_REF_SVC;
inline constexpr std::uint32_t kA32JniGetArrayLengthSvcImmediate =
    LIBA32ANDROID_A32_JNI_GET_ARRAY_LENGTH_SVC;
inline constexpr std::uint32_t kA32JniGetStaticIntFieldSvcImmediate =
    LIBA32ANDROID_A32_JNI_GET_STATIC_INT_FIELD_SVC;

inline constexpr std::uint32_t kA32JniVersion11 = 0x00010001U;
inline constexpr std::uint32_t kA32JniVersion12 = 0x00010002U;
inline constexpr std::uint32_t kA32JniVersion14 = 0x00010004U;
inline constexpr std::uint32_t kA32JniVersion16 = 0x00010006U;

inline constexpr std::int32_t kA32JniOk = 0;
inline constexpr std::int32_t kA32JniErr = -1;
inline constexpr std::int32_t kA32JniEdetached = -2;
inline constexpr std::int32_t kA32JniEversion = -3;

inline constexpr std::size_t kA32JniHardMaxClasses = 1024U;
inline constexpr std::size_t kA32JniHardMaxRegisteredMethods = 4096U;
inline constexpr std::size_t kA32JniHardMaxMethodsPerRegistration = 1024U;
inline constexpr std::size_t kA32JniHardMaxMemberIds = 4096U;
inline constexpr std::size_t kA32JniHardMaxReferenceHandles = 4096U;
inline constexpr std::size_t kA32JniHardMaxReferenceCountPerHandle = 1U << 20U;
inline constexpr std::size_t kA32JniHardMaxArrays = 4096U;
inline constexpr std::size_t kA32JniHardMaxStringBytes = 4096U;

// Android/Dalvik GetEnv accepts the inclusive numeric JNI 1.1..1.6
// range. JNI_OnLoad is stricter and accepts only 1.2, 1.4, or 1.6.
[[nodiscard]] constexpr bool is_a32_supported_jni_getenv_version(
    std::uint32_t version) noexcept {
    return version >= kA32JniVersion11 &&
           version <= kA32JniVersion16;
}

[[nodiscard]] constexpr bool is_a32_supported_jni_onload_version(
    std::uint32_t version) noexcept {
    return version == kA32JniVersion12 ||
           version == kA32JniVersion14 ||
           version == kA32JniVersion16;
}

struct A32JniRegistryLimits {
    std::size_t max_classes{64U};
    std::size_t max_registered_methods{256U};
    std::size_t max_methods_per_registration{64U};
    std::size_t max_member_ids{512U};
    std::size_t max_reference_handles{512U};
    std::size_t max_reference_count_per_handle{4096U};
    std::size_t max_arrays{512U};
    std::size_t max_class_name_bytes{256U};
    std::size_t max_method_name_bytes{256U};
    std::size_t max_signature_bytes{256U};
};

enum class A32JniRegistryError : std::uint8_t {
    None = 0,
    InvalidLimits,
    InvalidClassHandle,
    InvalidName,
    ClassLimitExceeded,
    DuplicateClassName,
    DuplicateClassHandle,
    UnknownClass,
    InvalidMethod,
    MethodLimitExceeded,
    DuplicateMethod,
    InvalidMemberHandle,
    InvalidMemberKind,
    MemberLimitExceeded,
    DuplicateMemberHandle,
    DuplicateMember,
    InvalidReferenceHandle,
    ReferenceLimitExceeded,
    DuplicateReferenceHandle,
    ReferenceCountExceeded,
    InvalidArrayHandle,
    InvalidArrayLength,
    ArrayLimitExceeded,
    DuplicateArrayHandle,
};

enum class A32JniMemberKind : std::uint8_t {
    InstanceMethod = 0,
    InstanceField,
    StaticField,
};

struct A32JniMemberId {
    std::uint32_t class_handle{};
    std::uint32_t handle{};
    A32JniMemberKind kind{A32JniMemberKind::InstanceMethod};
    std::string class_name;
    std::string name;
    std::string signature;
};

struct A32JniReferenceCounts {
    std::uint32_t local{};
    std::uint32_t global{};
};

struct A32JniArrayInfo {
    std::uint32_t handle{};
    std::uint32_t length{};
};

struct A32JniStaticIntFieldValue {
    std::uint32_t field_handle{};
    std::int32_t value{};
};

struct A32JniRegisteredNative {
    std::uint32_t class_handle{};
    std::string class_name;
    std::string name;
    std::string signature;
    std::uint32_t function{};
};

// Caller-owned bounded class/native registry. Guest-visible values are logical
// 32-bit handles/function pointers; strings are copied into owned host storage.
class A32JniClassRegistry final {
public:
    explicit A32JniClassRegistry(
        A32JniRegistryLimits limits = {}) noexcept
        : limits_(limits) {}

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] A32JniRegistryError add_class(
        std::uint32_t handle,
        std::string_view name);

    [[nodiscard]] std::optional<std::uint32_t> find_class(
        std::string_view name) const noexcept;
    [[nodiscard]] bool contains_class_handle(
        std::uint32_t handle) const noexcept;

    [[nodiscard]] A32JniRegistryError add_reference_identity(
        std::uint32_t handle);
    [[nodiscard]] bool retain_local_reference(
        std::uint32_t handle) noexcept;
    [[nodiscard]] std::uint32_t new_global_reference(
        std::uint32_t handle) noexcept;
    [[nodiscard]] bool delete_local_reference(
        std::uint32_t handle) noexcept;
    [[nodiscard]] bool delete_global_reference(
        std::uint32_t handle) noexcept;
    [[nodiscard]] std::optional<A32JniReferenceCounts> reference_counts(
        std::uint32_t handle) const noexcept;

    [[nodiscard]] A32JniRegistryError add_array(
        std::uint32_t handle,
        std::uint32_t length);
    [[nodiscard]] std::optional<std::uint32_t> array_length(
        std::uint32_t handle) const noexcept;

    [[nodiscard]] A32JniRegistryError add_member(
        std::uint32_t class_handle,
        A32JniMemberKind kind,
        std::uint32_t handle,
        std::string_view name,
        std::string_view signature);
    [[nodiscard]] const A32JniMemberId* find_member(
        std::uint32_t class_handle,
        A32JniMemberKind kind,
        std::string_view name,
        std::string_view signature) const noexcept;
    [[nodiscard]] const A32JniMemberId* find_member_by_handle(
        std::uint32_t handle) const noexcept;
    [[nodiscard]] A32JniRegistryError set_static_int_field_value(
        std::uint32_t field_handle,
        std::int32_t value);
    [[nodiscard]] std::optional<std::int32_t> static_int_field_value(
        std::uint32_t field_handle) const noexcept;
    [[nodiscard]] const A32JniRegisteredNative* find_native(
        std::uint32_t class_handle,
        std::string_view name,
        std::string_view signature) const noexcept;

    [[nodiscard]] std::size_t class_count() const noexcept {
        return classes_.size();
    }
    [[nodiscard]] std::size_t member_count() const noexcept {
        return members_.size();
    }
    [[nodiscard]] std::size_t array_count() const noexcept {
        return arrays_.size();
    }
    [[nodiscard]] std::size_t registered_native_count() const noexcept {
        return natives_.size();
    }
    [[nodiscard]] A32JniRegistryLimits limits() const noexcept {
        return limits_;
    }

private:
    struct ClassEntry {
        std::uint32_t handle{};
        std::string name;
    };
    struct ReferenceEntry {
        std::uint32_t handle{};
        std::uint32_t local_count{};
        std::uint32_t global_count{};
    };

    [[nodiscard]] const ReferenceEntry* find_reference_entry(
        std::uint32_t handle) const noexcept;
    [[nodiscard]] ReferenceEntry* find_reference_entry(
        std::uint32_t handle) noexcept;
    [[nodiscard]] const ClassEntry* find_class_entry(
        std::uint32_t handle) const noexcept;
    [[nodiscard]] A32JniRegistryError register_natives(
        std::uint32_t class_handle,
        std::vector<A32JniRegisteredNative> pending);

    A32JniRegistryLimits limits_;
    std::vector<ClassEntry> classes_;
    std::vector<ReferenceEntry> references_;
    std::vector<A32JniArrayInfo> arrays_;
    std::vector<A32JniMemberId> members_;
    std::vector<A32JniStaticIntFieldValue> static_int_fields_;
    std::vector<A32JniRegisteredNative> natives_;

    friend class A32JniVmService;
};

struct A32JniVmLayout {
    std::uint32_t java_vm_address{};
    std::uint32_t invoke_table_address{};
    std::uint32_t jni_env_address{};
    std::uint32_t native_table_address{};
    std::uint32_t get_env_stub_address{};
    std::uint32_t find_class_stub_address{};
    std::uint32_t register_natives_stub_address{};
    std::uint32_t get_method_id_stub_address{};
    std::uint32_t get_field_id_stub_address{};
    std::uint32_t get_static_field_id_stub_address{};
    std::uint32_t attach_current_thread_stub_address{};
    std::uint32_t detach_current_thread_stub_address{};
    std::uint32_t new_global_ref_stub_address{};
    std::uint32_t delete_global_ref_stub_address{};
    std::uint32_t delete_local_ref_stub_address{};
    std::uint32_t get_array_length_stub_address{};
    std::uint32_t get_static_int_field_stub_address{};
};

enum class A32JniVmInstallError : std::uint8_t {
    None = 0,
    InvalidLayout,
    SnapshotFailed,
    WriteFailed,
    RollbackFailed,
};

struct A32JniVmInstallResult {
    A32JniVmInstallError error{A32JniVmInstallError::None};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == A32JniVmInstallError::None;
    }
};

// One bounded guest JNI context. install() begins attached; observed JavaVM
// AttachCurrentThread/DetachCurrentThread services can transition that state.
// The caller owns all guest mappings. install() publishes only logical 32-bit
// guest pointers and ARM SVC stubs; it never maps, unmaps, or changes page
// permissions.
class A32JniVmService final : public runtime::A32HostServiceHandler {
public:
    explicit A32JniVmService(
        A32JniVmLayout layout,
        A32JniClassRegistry* registry = nullptr) noexcept
        : layout_(layout),
          registry_(registry) {}

    [[nodiscard]] A32JniVmInstallResult install(
        memory::GuestMemory& memory);

    [[nodiscard]] runtime::A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;

    [[nodiscard]] A32JniVmLayout layout() const noexcept {
        return layout_;
    }

    [[nodiscard]] bool installed() const noexcept {
        return installed_;
    }
    [[nodiscard]] bool attached() const noexcept {
        return attached_;
    }

    [[nodiscard]] A32JniClassRegistry* registry() noexcept {
        return registry_;
    }
    [[nodiscard]] const A32JniClassRegistry* registry() const noexcept {
        return registry_;
    }

private:
    [[nodiscard]] bool layout_valid() const noexcept;

    A32JniVmLayout layout_;
    A32JniClassRegistry* registry_{};
    bool installed_{};
    bool attached_{};
};

struct A32JniOnLoadOptions {
    std::uint32_t stack_top{};
    std::uint32_t return_pc{};
    std::size_t max_instructions{};
    std::size_t max_service_calls{};
    elf::Elf32SymbolLookupOptions symbols{};
    // Optional borrowed provenance context. During JNI_OnLoad it identifies
    // exactly object_index so nested __aeabi_atexit registration can learn the
    // same object/DSO ownership as constructor execution.
    elf::Elf32LifecycleExecutionContext* execution_context{};
};

enum class A32JniOnLoadError : std::uint8_t {
    None = 0,
    InvalidOptions,
    InvalidObject,
    SymbolIndexFailed,
    SymbolLookupFailed,
    InvalidFunctionAddress,
    MemoryFault,
    CpuException,
    ServiceLimitExceeded,
    ServiceUnhandled,
    ServiceFailed,
    ServiceSuspended,
    InstructionLimitExceeded,
    UnsupportedVersion,
};

struct A32JniOnLoadResult {
    A32JniOnLoadError error{A32JniOnLoadError::None};
    elf::Elf32SymbolIndexError index_error{
        elf::Elf32SymbolIndexError::None};
    elf::Elf32SymbolLookupError lookup_error{
        elf::Elf32SymbolLookupError::None};
    elf::Elf32LinkerStringError string_error{
        elf::Elf32LinkerStringError::None};
    std::uint32_t returned_version{};
    std::optional<runtime::A32ServiceDispatchResult> execution;
    std::optional<std::uint32_t> failing_svc_immediate;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == A32JniOnLoadError::None;
    }
};

// Resolve JNI_OnLoad from exactly object_index (never dependency/global scope),
// execute it as jint JNI_OnLoad(JavaVM*, void*) with r1 == nullptr, and accept
// only Android/Dalvik JNI 1.2, 1.4, or 1.6 return versions.
[[nodiscard]] A32JniOnLoadResult invoke_a32_jni_on_load(
    memory::GuestMemory& memory,
    const elf::Elf32DependencyGraph& graph,
    std::size_t object_index,
    std::uint32_t java_vm_address,
    runtime::A32HostServiceHandler& service_handler,
    const A32JniOnLoadOptions& options);

struct A32JniNativeInvokeOptions {
    std::uint32_t stack_top{};
    std::uint32_t return_pc{};
    std::size_t max_instructions{};
    std::size_t max_service_calls{};
};

enum class A32JniNativeInvokeError : std::uint8_t {
    None = 0,
    InvalidOptions,
    ClassNotFound,
    NativeNotFound,
    NonZeroArgumentSignature,
    InvalidFunctionAddress,
    MemoryFault,
    CpuException,
    ServiceLimitExceeded,
    ServiceUnhandled,
    ServiceFailed,
    ServiceSuspended,
    InstructionLimitExceeded,
};

struct A32JniNativeInvokeResult {
    A32JniNativeInvokeError error{A32JniNativeInvokeError::None};
    std::uint32_t function{};
    std::uint32_t returned_value{};
    std::optional<runtime::A32ServiceDispatchResult> execution;
    std::optional<std::uint32_t> failing_svc_immediate;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == A32JniNativeInvokeError::None;
    }
};

// Execute one exact registered zero-Java-argument native. The function receives
// JNIEnv* in r0 and receiver_or_class in r1. The raw r0 return bits are exposed.
[[nodiscard]] A32JniNativeInvokeResult
invoke_a32_registered_native_noargs(
    memory::GuestMemory& memory,
    const A32JniClassRegistry& registry,
    std::uint32_t class_handle,
    std::string_view name,
    std::string_view signature,
    std::uint32_t jni_env_address,
    std::uint32_t receiver_or_class,
    runtime::A32HostServiceHandler& service_handler,
    const A32JniNativeInvokeOptions& options);

[[nodiscard]] const char* to_string(
    A32JniRegistryError error) noexcept;
[[nodiscard]] const char* to_string(
    A32JniVmInstallError error) noexcept;
[[nodiscard]] const char* to_string(
    A32JniOnLoadError error) noexcept;
[[nodiscard]] const char* to_string(
    A32JniNativeInvokeError error) noexcept;

}  // namespace liba32android::compat

#endif
