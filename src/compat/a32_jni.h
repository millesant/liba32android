#pragma once

#define LIBA32ANDROID_A32_JNI_GET_ENV_SVC 0xD7

#ifdef __cplusplus

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "cpu/a32_cpu.h"
#include "elf/elf32_dependency_graph.h"
#include "elf/elf32_symbol_lookup.h"
#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

inline constexpr std::uint32_t kA32JniGetEnvSvcImmediate =
    LIBA32ANDROID_A32_JNI_GET_ENV_SVC;

inline constexpr std::uint32_t kA32JniVersion11 = 0x00010001U;
inline constexpr std::uint32_t kA32JniVersion12 = 0x00010002U;
inline constexpr std::uint32_t kA32JniVersion14 = 0x00010004U;
inline constexpr std::uint32_t kA32JniVersion16 = 0x00010006U;

inline constexpr std::int32_t kA32JniOk = 0;
inline constexpr std::int32_t kA32JniErr = -1;
inline constexpr std::int32_t kA32JniEversion = -3;

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

struct A32JniVmLayout {
    std::uint32_t java_vm_address{};
    std::uint32_t invoke_table_address{};
    std::uint32_t jni_env_address{};
    std::uint32_t native_table_address{};
    std::uint32_t get_env_stub_address{};
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

// One currently-attached guest JNI context. The caller owns all guest mappings.
// install() publishes only logical 32-bit guest pointers and one ARM SVC stub;
// it never maps, unmaps, or changes page permissions.
class A32JniVmService final : public runtime::A32HostServiceHandler {
public:
    explicit A32JniVmService(A32JniVmLayout layout) noexcept
        : layout_(layout) {}

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

private:
    [[nodiscard]] bool layout_valid() const noexcept;

    A32JniVmLayout layout_;
    bool installed_{};
};

struct A32JniOnLoadOptions {
    std::uint32_t stack_top{};
    std::uint32_t return_pc{};
    std::size_t max_instructions{};
    std::size_t max_service_calls{};
    elf::Elf32SymbolLookupOptions symbols{};
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

[[nodiscard]] const char* to_string(
    A32JniVmInstallError error) noexcept;
[[nodiscard]] const char* to_string(
    A32JniOnLoadError error) noexcept;

}  // namespace liba32android::compat

#endif
