#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "compat/a32_jni.h"
#include "elf/elf32_linker_metadata.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_registry.h"

namespace {

using liba32android::compat::A32JniClassRegistry;
using liba32android::compat::A32JniMemberKind;
using liba32android::compat::A32JniNativeInvokeError;
using liba32android::compat::A32JniNativeInvokeOptions;
using liba32android::compat::A32JniOnLoadError;
using liba32android::compat::A32JniOnLoadOptions;
using liba32android::compat::A32JniRegistryError;
using liba32android::compat::A32JniRegistryLimits;
using liba32android::compat::A32JniVmInstallError;
using liba32android::compat::A32JniVmLayout;
using liba32android::compat::A32JniVmService;
using liba32android::compat::kA32JniEdetached;
using liba32android::compat::kA32JniEversion;
using liba32android::compat::kA32JniErr;
using liba32android::compat::kA32JniFindClassSvcImmediate;
using liba32android::compat::kA32JniGetEnvSvcImmediate;
using liba32android::compat::kA32JniGetFieldIdSvcImmediate;
using liba32android::compat::kA32JniGetMethodIdSvcImmediate;
using liba32android::compat::kA32JniGetStaticFieldIdSvcImmediate;
using liba32android::compat::kA32JniAttachCurrentThreadSvcImmediate;
using liba32android::compat::kA32JniDetachCurrentThreadSvcImmediate;
using liba32android::compat::kA32JniOk;
using liba32android::compat::kA32JniRegisterNativesSvcImmediate;
using liba32android::compat::kA32JniVersion11;
using liba32android::compat::kA32JniVersion12;
using liba32android::compat::kA32JniVersion14;
using liba32android::compat::kA32JniVersion16;
using liba32android::compat::invoke_a32_jni_on_load;
using liba32android::compat::invoke_a32_registered_native_noargs;
using liba32android::elf::Elf32DependencyEdge;
using liba32android::elf::Elf32DependencyGraph;
using liba32android::elf::Elf32HashTableMetadata;
using liba32android::elf::Elf32LoadedDependencyObject;
using liba32android::elf::Elf32StringTableMetadata;
using liba32android::elf::Elf32SymbolLookupError;
using liba32android::elf::Elf32SymbolLookupOptions;
using liba32android::elf::Elf32SymbolTableMetadata;
using liba32android::memory::GuestMemory;
using liba32android::memory::LinearGuestMemory;
using liba32android::runtime::A32HostServiceDisposition;
using liba32android::runtime::A32HostServiceRegistry;
using liba32android::runtime::A32HostServiceRegistryEntry;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

bool write_u32(
    GuestMemory& memory,
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

bool write_c_string(
    GuestMemory& memory,
    std::uint32_t address,
    std::string_view value) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(value.size() + 1U);
    for (const char ch : value) {
        bytes.push_back(static_cast<std::uint8_t>(ch));
    }
    bytes.push_back(0U);
    return memory.write(address, bytes);
}

std::uint32_t read_u32(
    const GuestMemory& memory,
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

A32JniVmLayout layout() {
    return A32JniVmLayout{
        .java_vm_address = 0x1100U,
        .invoke_table_address = 0x1120U,
        .jni_env_address = 0x1160U,
        .native_table_address = 0x1180U,
        .get_env_stub_address = 0x1500U,
        .find_class_stub_address = 0x1520U,
        .register_natives_stub_address = 0x1540U,
        .get_method_id_stub_address = 0x1560U,
        .get_field_id_stub_address = 0x1580U,
        .get_static_field_id_stub_address = 0x15a0U,
        .attach_current_thread_stub_address = 0x15c0U,
        .detach_current_thread_stub_address = 0x15e0U,
    };
}

class FailingWriteMemory final : public GuestMemory {
public:
    FailingWriteMemory(
        std::size_t size,
        std::uint32_t base,
        std::uint32_t fail_address)
        : backing_(size, base),
          fail_address_(fail_address) {}

    bool read(
        std::uint32_t address,
        std::span<std::uint8_t> output) const override {
        return backing_.read(address, output);
    }

    bool write(
        std::uint32_t address,
        std::span<const std::uint8_t> input) override {
        if (address == fail_address_) {
            return false;
        }
        return backing_.write(address, input);
    }

private:
    LinearGuestMemory backing_;
    std::uint32_t fail_address_{};
};

int test_vm_install_and_getenv() {
    LinearGuestMemory memory{0x1000U, 0x1000U};
    A32JniVmService service{layout()};
    const auto installed = service.install(memory);
    if (!installed || !service.installed()) {
        return fail("valid JNI VM layout did not install");
    }

    const auto configured = layout();
    if (read_u32(memory, configured.java_vm_address) !=
            configured.invoke_table_address ||
        read_u32(memory, configured.invoke_table_address + 4U * 4U) !=
            configured.attach_current_thread_stub_address ||
        read_u32(memory, configured.invoke_table_address + 5U * 4U) !=
            configured.detach_current_thread_stub_address ||
        read_u32(memory, configured.invoke_table_address + 6U * 4U) !=
            configured.get_env_stub_address ||
        read_u32(memory, configured.jni_env_address) !=
            configured.native_table_address ||
        read_u32(memory, configured.native_table_address + 6U * 4U) !=
            configured.find_class_stub_address ||
        read_u32(memory, configured.native_table_address + 33U * 4U) !=
            configured.get_method_id_stub_address ||
        read_u32(memory, configured.native_table_address + 94U * 4U) !=
            configured.get_field_id_stub_address ||
        read_u32(memory, configured.native_table_address + 144U * 4U) !=
            configured.get_static_field_id_stub_address ||
        read_u32(memory, configured.native_table_address + 215U * 4U) !=
            configured.register_natives_stub_address) {
        return fail("JNI VM pointer tables contain wrong guest pointers");
    }

    for (std::uint32_t index = 0U; index < 8U; ++index) {
        if (index == 4U || index == 5U || index == 6U) {
            continue;
        }
        if (read_u32(
                memory,
                configured.invoke_table_address + index * 4U) != 0U) {
            return fail("unsupported JavaVM invoke slot was non-null");
        }
    }
    for (std::uint32_t index = 0U; index < 216U; ++index) {
        if (index == 6U ||
            index == 33U ||
            index == 94U ||
            index == 144U ||
            index == 215U) {
            continue;
        }
        if (read_u32(
                memory,
                configured.native_table_address + index * 4U) != 0U) {
            return fail("unsupported JNIEnv native slot was non-null");
        }
    }

    constexpr std::array<std::array<std::uint8_t, 8>, 8> expected_stubs{{
        {{0xD7U, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xD8U, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xD9U, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xDAU, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xDBU, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xDCU, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xDDU, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xDEU, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
    }};
    const std::array<std::uint32_t, 8> stub_addresses{{
        configured.get_env_stub_address,
        configured.find_class_stub_address,
        configured.register_natives_stub_address,
        configured.get_method_id_stub_address,
        configured.get_field_id_stub_address,
        configured.get_static_field_id_stub_address,
        configured.attach_current_thread_stub_address,
        configured.detach_current_thread_stub_address,
    }};
    for (std::size_t index = 0U; index < stub_addresses.size(); ++index) {
        std::array<std::uint8_t, 8> observed_stub{};
        if (!memory.read(stub_addresses[index], observed_stub) ||
            observed_stub != expected_stubs[index]) {
            return fail("JNI ARM SVC stub bytes are wrong");
        }
    }

    std::uint32_t cpsr{};
    constexpr std::array<std::uint32_t, 6> supported_versions{{
        kA32JniVersion11,
        kA32JniVersion12,
        0x00010003U,
        kA32JniVersion14,
        0x00010005U,
        kA32JniVersion16,
    }};
    for (const std::uint32_t version : supported_versions) {
        std::array<std::uint32_t, 16> regs{};
        regs[0] = configured.java_vm_address;
        regs[1] = 0x1200U;
        regs[2] = version;
        if (service.handle(
                memory,
                kA32JniGetEnvSvcImmediate,
                regs,
                cpsr) != A32HostServiceDisposition::Handled ||
            regs[0] != static_cast<std::uint32_t>(kA32JniOk) ||
            read_u32(memory, 0x1200U) !=
                configured.jni_env_address) {
            return fail("JNI GetEnv rejected supported JNI version");
        }
    }

    if (!write_u32(memory, 0x1200U, 0xdeadbeefU)) {
        return fail("could not seed below-range version output");
    }
    std::array<std::uint32_t, 16> regs{};
    regs[0] = configured.java_vm_address;
    regs[1] = 0x1200U;
    regs[2] = 0x00010000U;
    if (service.handle(
            memory,
            kA32JniGetEnvSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] !=
            static_cast<std::uint32_t>(kA32JniEversion) ||
        read_u32(memory, 0x1200U) != 0xdeadbeefU) {
        return fail("JNI GetEnv below-range version touched output on EVERSION");
    }

    if (!write_u32(memory, 0x1200U, 0xdeadbeefU)) {
        return fail("could not seed unsupported-version output");
    }
    regs = {};
    regs[0] = configured.java_vm_address;
    regs[1] = 0x1200U;
    regs[2] = 0x00010008U;
    if (service.handle(
            memory,
            kA32JniGetEnvSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] !=
            static_cast<std::uint32_t>(kA32JniEversion) ||
        read_u32(memory, 0x1200U) != 0xdeadbeefU) {
        return fail("JNI GetEnv above-range version touched output on EVERSION");
    }

    regs = {};
    regs[0] = configured.java_vm_address + 4U;
    regs[1] = 0x1200U;
    regs[2] = kA32JniVersion16;
    if (service.handle(
            memory,
            kA32JniGetEnvSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI GetEnv accepted wrong JavaVM pointer");
    }

    regs = {};
    regs[0] = configured.java_vm_address;
    regs[1] = 0U;
    regs[2] = 0x00010008U;
    if (service.handle(
            memory,
            kA32JniGetEnvSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] !=
            static_cast<std::uint32_t>(kA32JniEversion)) {
        return fail("JNI GetEnv EVERSION incorrectly required output pointer");
    }

    regs = {};
    regs[0] = configured.java_vm_address;
    regs[1] = 0U;
    regs[2] = kA32JniVersion16;
    if (service.handle(
            memory,
            kA32JniGetEnvSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI GetEnv accepted null output pointer");
    }

    regs = {};
    regs[0] = configured.java_vm_address;
    regs[1] = 0x3000U;
    regs[2] = kA32JniVersion16;
    if (service.handle(
            memory,
            kA32JniGetEnvSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI GetEnv accepted unreadable output pointer");
    }

    regs = {};
    regs[0] = configured.java_vm_address;
    if (service.handle(
            memory,
            kA32JniDetachCurrentThreadSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != static_cast<std::uint32_t>(kA32JniOk) ||
        service.attached()) {
        return fail("JNI DetachCurrentThread did not detach context");
    }

    if (!write_u32(memory, 0x1200U, 0xdeadbeefU)) {
        return fail("could not seed detached GetEnv output");
    }
    regs = {};
    regs[0] = configured.java_vm_address;
    regs[1] = 0x1200U;
    regs[2] = kA32JniVersion16;
    if (service.handle(
            memory,
            kA32JniGetEnvSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != static_cast<std::uint32_t>(kA32JniEdetached) ||
        read_u32(memory, 0x1200U) != 0xdeadbeefU) {
        return fail("JNI GetEnv did not report detached context");
    }

    regs = {};
    regs[0] = configured.java_vm_address;
    if (service.handle(
            memory,
            kA32JniDetachCurrentThreadSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != static_cast<std::uint32_t>(kA32JniErr)) {
        return fail("JNI DetachCurrentThread accepted detached context");
    }

    regs = {};
    regs[0] = configured.java_vm_address;
    regs[1] = 0x1200U;
    regs[2] = 0U;
    if (service.handle(
            memory,
            kA32JniAttachCurrentThreadSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != static_cast<std::uint32_t>(kA32JniOk) ||
        read_u32(memory, 0x1200U) != configured.jni_env_address ||
        !service.attached()) {
        return fail("JNI AttachCurrentThread did not restore context");
    }

    regs = {};
    regs[0] = configured.java_vm_address + 4U;
    regs[1] = 0x1200U;
    if (service.handle(
            memory,
            kA32JniAttachCurrentThreadSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI AttachCurrentThread accepted wrong JavaVM pointer");
    }

    regs = {};
    if (service.handle(
            memory,
            kA32JniDetachCurrentThreadSvcImmediate + 1U,
            regs,
            cpsr) != A32HostServiceDisposition::Unhandled) {
        return fail("JNI service accepted wrong SVC immediate");
    }
    return 0;
}

int test_observed_member_id_lookup() {
    LinearGuestMemory memory{0x1000U, 0x1000U};
    const A32JniRegistryLimits limits{
        .max_classes = 2U,
        .max_registered_methods = 2U,
        .max_methods_per_registration = 2U,
        .max_member_ids = 3U,
        .max_class_name_bytes = 32U,
        .max_method_name_bytes = 32U,
        .max_signature_bytes = 32U,
    };
    A32JniClassRegistry registry{limits};
    constexpr std::uint32_t kClassHandle = 0x44550000U;
    constexpr std::uint32_t kMethodHandle = 0x44551000U;
    constexpr std::uint32_t kFieldHandle = 0x44552000U;
    constexpr std::uint32_t kStaticFieldHandle = 0x44553000U;
    if (registry.add_class(
            kClassHandle,
            "org/videolan/Fixture") !=
            A32JniRegistryError::None ||
        registry.add_member(
            kClassHandle,
            A32JniMemberKind::InstanceMethod,
            kMethodHandle,
            "member",
            "()I") != A32JniRegistryError::None ||
        registry.add_member(
            kClassHandle,
            A32JniMemberKind::InstanceField,
            kFieldHandle,
            "member",
            "I") != A32JniRegistryError::None ||
        registry.add_member(
            kClassHandle,
            A32JniMemberKind::StaticField,
            kStaticFieldHandle,
            "member",
            "I") != A32JniRegistryError::None ||
        registry.member_count() != 3U) {
        return fail("could not seed bounded JNI member identities");
    }

    const auto* method = registry.find_member(
        kClassHandle,
        A32JniMemberKind::InstanceMethod,
        "member",
        "()I");
    const auto* field = registry.find_member(
        kClassHandle,
        A32JniMemberKind::InstanceField,
        "member",
        "I");
    const auto* static_field = registry.find_member(
        kClassHandle,
        A32JniMemberKind::StaticField,
        "member",
        "I");
    if (method == nullptr ||
        field == nullptr ||
        static_field == nullptr ||
        method->handle != kMethodHandle ||
        field->handle != kFieldHandle ||
        static_field->handle != kStaticFieldHandle ||
        method->class_name != "org/videolan/Fixture") {
        return fail("JNI member registry lost exact identity");
    }

    if (registry.add_member(
            kClassHandle,
            A32JniMemberKind::InstanceMethod,
            kFieldHandle,
            "other",
            "()V") !=
            A32JniRegistryError::DuplicateMemberHandle ||
        registry.add_member(
            kClassHandle,
            A32JniMemberKind::InstanceMethod,
            0x44554000U,
            "overflow",
            "()V") !=
            A32JniRegistryError::MemberLimitExceeded) {
        return fail("JNI member registry limits/identity were not enforced");
    }

    const auto configured = layout();
    A32JniVmService service{configured, &registry};
    if (!service.install(memory)) {
        return fail("JNI member lookup service did not install");
    }

    constexpr std::uint32_t kMethodName = 0x1600U;
    constexpr std::uint32_t kMethodSignature = 0x1620U;
    constexpr std::uint32_t kFieldName = 0x1640U;
    constexpr std::uint32_t kFieldSignature = 0x1660U;
    constexpr std::uint32_t kMissingName = 0x1680U;
    if (!write_c_string(memory, kMethodName, "member") ||
        !write_c_string(memory, kMethodSignature, "()I") ||
        !write_c_string(memory, kFieldName, "member") ||
        !write_c_string(memory, kFieldSignature, "I") ||
        !write_c_string(memory, kMissingName, "missing")) {
        return fail("could not stage JNI member lookup strings");
    }

    std::uint32_t cpsr{};
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 3> lookups{{
        {kA32JniGetMethodIdSvcImmediate, kMethodHandle},
        {kA32JniGetFieldIdSvcImmediate, kFieldHandle},
        {kA32JniGetStaticFieldIdSvcImmediate, kStaticFieldHandle},
    }};
    for (const auto& [svc, expected] : lookups) {
        std::array<std::uint32_t, 16> regs{};
        regs[0] = configured.jni_env_address;
        regs[1] = kClassHandle;
        regs[2] = svc == kA32JniGetMethodIdSvcImmediate
            ? kMethodName
            : kFieldName;
        regs[3] = svc == kA32JniGetMethodIdSvcImmediate
            ? kMethodSignature
            : kFieldSignature;
        if (service.handle(memory, svc, regs, cpsr) !=
                A32HostServiceDisposition::Handled ||
            regs[0] != expected) {
            return fail("JNI member service missed exact identity");
        }
    }

    std::array<std::uint32_t, 16> regs{};
    regs[0] = configured.jni_env_address;
    regs[1] = kClassHandle;
    regs[2] = kMissingName;
    regs[3] = kFieldSignature;
    if (service.handle(
            memory,
            kA32JniGetFieldIdSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("JNI member semantic miss did not return null");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = 0x99887766U;
    regs[2] = kFieldName;
    regs[3] = kFieldSignature;
    if (service.handle(
            memory,
            kA32JniGetFieldIdSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("JNI member lookup accepted unknown class");
    }

    regs = {};
    regs[0] = configured.jni_env_address + 4U;
    regs[1] = kClassHandle;
    regs[2] = kFieldName;
    regs[3] = kFieldSignature;
    if (service.handle(
            memory,
            kA32JniGetFieldIdSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI member lookup accepted wrong JNIEnv pointer");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kClassHandle;
    regs[2] = 0x3000U;
    regs[3] = kFieldSignature;
    if (service.handle(
            memory,
            kA32JniGetFieldIdSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI member lookup accepted unreadable name");
    }

    return 0;
}

int test_find_class_register_natives_and_reverse_dispatch() {
    LinearGuestMemory memory{0x1000U, 0x1000U};
    const A32JniRegistryLimits limits{
        .max_classes = 2U,
        .max_registered_methods = 2U,
        .max_methods_per_registration = 2U,
        .max_class_name_bytes = 32U,
        .max_method_name_bytes = 32U,
        .max_signature_bytes = 32U,
    };
    A32JniClassRegistry registry{limits};
    constexpr std::uint32_t kClassHandle = 0x44550000U;
    if (!registry.valid() ||
        registry.add_class(
            kClassHandle,
            "org/videolan/Fixture") !=
            A32JniRegistryError::None ||
        registry.class_count() != 1U) {
        return fail("could not seed bounded JNI class registry");
    }
    if (registry.add_class(
            kClassHandle,
            "org/videolan/Other") !=
            A32JniRegistryError::DuplicateClassHandle ||
        registry.add_class(
            0x44550004U,
            "org/videolan/Fixture") !=
            A32JniRegistryError::DuplicateClassName) {
        return fail("JNI class registry accepted duplicate identity");
    }

    const auto configured = layout();
    A32JniVmService service{configured, &registry};
    if (!service.install(memory)) {
        return fail("JNI registry service did not install");
    }

    constexpr std::uint32_t kClassName = 0x1600U;
    constexpr std::uint32_t kMissingName = 0x1640U;
    constexpr std::uint32_t kMethodName = 0x1680U;
    constexpr std::uint32_t kMethodSignature = 0x16a0U;
    constexpr std::uint32_t kMethods = 0x16c0U;
    constexpr std::uint32_t kNativeFunction = 0x1800U;
    if (!write_c_string(
            memory, kClassName, "org/videolan/Fixture") ||
        !write_c_string(
            memory, kMissingName, "org/videolan/Missing") ||
        !write_c_string(
            memory, kMethodName, "nativePing") ||
        !write_c_string(
            memory, kMethodSignature, "()I")) {
        return fail("could not stage JNI registry guest strings");
    }

    std::uint32_t cpsr{};
    std::array<std::uint32_t, 16> regs{};
    regs[0] = configured.jni_env_address;
    regs[1] = kClassName;
    if (service.handle(
            memory,
            kA32JniFindClassSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != kClassHandle) {
        return fail("JNI FindClass did not return exact registered handle");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kMissingName;
    if (service.handle(
            memory,
            kA32JniFindClassSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("JNI FindClass miss did not return null");
    }

    regs = {};
    regs[0] = configured.jni_env_address + 4U;
    regs[1] = kClassName;
    if (service.handle(
            memory,
            kA32JniFindClassSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI FindClass accepted wrong JNIEnv pointer");
    }

    if (!write_u32(memory, kMethods + 0U, kMethodName) ||
        !write_u32(memory, kMethods + 4U, kMethodSignature) ||
        !write_u32(memory, kMethods + 8U, kNativeFunction)) {
        return fail("could not stage ARM32 JNINativeMethod");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kClassHandle;
    regs[2] = kMethods;
    regs[3] = 1U;
    if (service.handle(
            memory,
            kA32JniRegisterNativesSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != static_cast<std::uint32_t>(kA32JniOk) ||
        registry.registered_native_count() != 1U) {
        return fail("JNI RegisterNatives rejected valid ARM32 method");
    }

    const auto* registered = registry.find_native(
        kClassHandle, "nativePing", "()I");
    if (registered == nullptr ||
        registered->class_name != "org/videolan/Fixture" ||
        registered->function != kNativeFunction) {
        return fail("JNI RegisterNatives retained wrong owned metadata");
    }

    // Duplicate keys in one call must reject the complete transaction. The
    // already-registered function must remain unchanged.
    if (!write_u32(memory, kMethods + 8U, 0x1810U) ||
        !write_u32(memory, kMethods + 12U, kMethodName) ||
        !write_u32(memory, kMethods + 16U, kMethodSignature) ||
        !write_u32(memory, kMethods + 20U, 0x1820U)) {
        return fail("could not stage duplicate JNINativeMethod transaction");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kClassHandle;
    regs[2] = kMethods;
    regs[3] = 2U;
    if (service.handle(
            memory,
            kA32JniRegisterNativesSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != static_cast<std::uint32_t>(kA32JniErr) ||
        registry.registered_native_count() != 1U ||
        registry.find_native(
            kClassHandle, "nativePing", "()I")->function !=
            kNativeFunction) {
        return fail("failed JNI registration partially mutated registry");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = 0x99887766U;
    regs[2] = kMethods;
    regs[3] = 1U;
    if (service.handle(
            memory,
            kA32JniRegisterNativesSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != static_cast<std::uint32_t>(kA32JniErr)) {
        return fail("JNI RegisterNatives accepted unknown class handle");
    }

    const std::array<std::uint8_t, 8> code{{
        0x2AU, 0x00U, 0xA0U, 0xE3U,  // mov r0, #42
        0x1EU, 0xFFU, 0x2FU, 0xE1U,  // bx lr
    }};
    if (!memory.write(kNativeFunction, code)) {
        return fail("could not stage registered native code");
    }
    const A32JniNativeInvokeOptions invoke_options{
        .stack_top = 0x1ff8U,
        .return_pc = 0x1f00U,
        .max_instructions = 16U,
        .max_service_calls = 1U,
    };
    const auto invoked = invoke_a32_registered_native_noargs(
        memory,
        registry,
        kClassHandle,
        "nativePing",
        "()I",
        configured.jni_env_address,
        kClassHandle,
        service,
        invoke_options);
    if (!invoked ||
        invoked.function != kNativeFunction ||
        invoked.returned_value != 42U ||
        !invoked.execution.has_value() ||
        !invoked.execution->stop_pc_reached ||
        invoked.execution->services_handled != 0U) {
        return fail("registered JNI native reverse dispatch failed");
    }
    if (invoke_a32_registered_native_noargs(
            memory,
            registry,
            kClassHandle,
            "missing",
            "()I",
            configured.jni_env_address,
            kClassHandle,
            service,
            invoke_options).error !=
        A32JniNativeInvokeError::NativeNotFound) {
        return fail("registered JNI native lookup escaped exact identity");
    }
    return 0;
}

int test_vm_layout_and_install_rollback() {
    {
        auto invalid = layout();
        invalid.jni_env_address =
            invalid.invoke_table_address + 4U;
        LinearGuestMemory memory{0x1000U, 0x1000U};
        A32JniVmService service{invalid};
        if (service.install(memory).error !=
                A32JniVmInstallError::InvalidLayout ||
            service.installed()) {
            return fail("overlapping JNI VM layout was accepted");
        }
    }
    {
        auto invalid = layout();
        invalid.get_env_stub_address |= 2U;
        LinearGuestMemory memory{0x1000U, 0x1000U};
        A32JniVmService service{invalid};
        if (service.install(memory).error !=
            A32JniVmInstallError::InvalidLayout) {
            return fail("misaligned JNI stub address was accepted");
        }
    }
    {
        const auto configured = layout();
        FailingWriteMemory memory{
            0x1000U,
            0x1000U,
            configured.jni_env_address};
        if (!write_u32(
                memory,
                configured.java_vm_address,
                0xaabbccddU) ||
            !write_u32(
                memory,
                configured.invoke_table_address,
                0x11223344U)) {
            return fail("could not seed JNI rollback fixture");
        }
        A32JniVmService service{configured};
        if (service.install(memory).error !=
                A32JniVmInstallError::WriteFailed ||
            service.installed() ||
            read_u32(memory, configured.java_vm_address) !=
                0xaabbccddU ||
            read_u32(memory, configured.invoke_table_address) !=
                0x11223344U) {
            return fail("failed JNI install did not restore prior table bytes");
        }
    }
    return 0;
}

Elf32SymbolLookupOptions symbol_options() {
    return Elf32SymbolLookupOptions{
        .max_symbols = 8U,
        .max_hash_buckets = 4U,
        .max_gnu_bloom_words = 4U,
        .max_scope_objects = 4U,
        .max_name_bytes = 64U,
        .max_version_records = 16U,
    };
}

bool stage_onload_object(
    LinearGuestMemory& memory,
    Elf32LoadedDependencyObject& object,
    std::uint32_t base,
    std::string_view symbol_name,
    std::uint32_t return_version) {
    const std::uint32_t strings = base;
    const std::uint32_t symbols = base + 0x100U;
    const std::uint32_t hash = base + 0x200U;
    const std::uint32_t code = base + 0x300U;

    std::vector<std::uint8_t> string_bytes;
    string_bytes.reserve(symbol_name.size() + 2U);
    string_bytes.push_back(0U);
    for (const char ch : symbol_name) {
        string_bytes.push_back(
            static_cast<std::uint8_t>(ch));
    }
    string_bytes.push_back(0U);
    if (!memory.write(strings, string_bytes)) {
        return false;
    }

    std::array<std::uint8_t, 32> symbol_bytes{};
    symbol_bytes[16U + 0U] = 1U;
    symbol_bytes[16U + 4U] =
        static_cast<std::uint8_t>(code);
    symbol_bytes[16U + 5U] =
        static_cast<std::uint8_t>(code >> 8U);
    symbol_bytes[16U + 6U] =
        static_cast<std::uint8_t>(code >> 16U);
    symbol_bytes[16U + 7U] =
        static_cast<std::uint8_t>(code >> 24U);
    symbol_bytes[16U + 8U] = 12U;
    symbol_bytes[16U + 12U] = 0x12U;
    symbol_bytes[16U + 14U] = 1U;
    if (!memory.write(symbols, symbol_bytes)) {
        return false;
    }

    // SysV hash: nbucket=1, nchain=2, bucket[0]=1, chains={0,0}.
    if (!write_u32(memory, hash + 0U, 1U) ||
        !write_u32(memory, hash + 4U, 2U) ||
        !write_u32(memory, hash + 8U, 1U) ||
        !write_u32(memory, hash + 12U, 0U) ||
        !write_u32(memory, hash + 16U, 0U)) {
        return false;
    }

    const std::array<std::uint8_t, 12> code_bytes{{
        0x00U, 0x00U, 0x9FU, 0xE5U,  // ldr r0, [pc]
        0x1EU, 0xFFU, 0x2FU, 0xE1U,  // bx lr
        static_cast<std::uint8_t>(return_version),
        static_cast<std::uint8_t>(return_version >> 8U),
        static_cast<std::uint8_t>(return_version >> 16U),
        static_cast<std::uint8_t>(return_version >> 24U),
    }};
    if (!memory.write(code, code_bytes)) {
        return false;
    }

    object.identity.assign(symbol_name);
    object.load.load_bias = 0U;
    object.linker_metadata.string_table =
        Elf32StringTableMetadata{
            .guest_address = strings,
            .size = static_cast<std::uint32_t>(
                string_bytes.size()),
        };
    object.linker_metadata.symbol_table =
        Elf32SymbolTableMetadata{
            .guest_address = symbols,
            .entry_size = 16U,
        };
    object.linker_metadata.sysv_hash_table =
        Elf32HashTableMetadata{
            .guest_address = hash,
        };
    return true;
}

int test_exact_object_onload_and_version_validation() {
    LinearGuestMemory memory{0x5000U, 0x1000U};
    Elf32DependencyGraph graph;
    graph.objects.resize(3U);

    if (!stage_onload_object(
            memory,
            graph.objects[0],
            0x1800U,
            "NotOnLoad",
            kA32JniVersion16) ||
        !stage_onload_object(
            memory,
            graph.objects[1],
            0x2800U,
            "JNI_OnLoad",
            kA32JniVersion16) ||
        !stage_onload_object(
            memory,
            graph.objects[2],
            0x3800U,
            "JNI_OnLoad",
            0x00010008U)) {
        return fail("could not stage JNI_OnLoad symbol fixtures");
    }
    graph.objects[0].dependencies.push_back(
        Elf32DependencyEdge{
            .requested_name = "provider",
            .target_object = 1U,
        });

    const std::array<A32HostServiceRegistryEntry, 0> entries{};
    A32HostServiceRegistry registry{std::span{entries}};
    const A32JniOnLoadOptions options{
        .stack_top = 0x5ff8U,
        .return_pc = 0x6000U,
        .max_instructions = 32U,
        .max_service_calls = 1U,
        .symbols = symbol_options(),
    };

    const auto exact_miss = invoke_a32_jni_on_load(
        memory,
        graph,
        0U,
        0x1100U,
        registry,
        options);
    if (exact_miss.error !=
            A32JniOnLoadError::SymbolLookupFailed ||
        exact_miss.lookup_error !=
            Elf32SymbolLookupError::SymbolNotFound) {
        return fail("JNI_OnLoad lookup escaped exact object into dependency");
    }

    const auto supported = invoke_a32_jni_on_load(
        memory,
        graph,
        1U,
        0x1100U,
        registry,
        options);
    if (!supported ||
        supported.returned_version != kA32JniVersion16 ||
        !supported.execution.has_value() ||
        !supported.execution->stop_pc_reached ||
        supported.execution->services_handled != 0U) {
        return fail("supported exact-object JNI_OnLoad did not execute");
    }

    const auto unsupported = invoke_a32_jni_on_load(
        memory,
        graph,
        2U,
        0x1100U,
        registry,
        options);
    if (unsupported.error !=
            A32JniOnLoadError::UnsupportedVersion ||
        unsupported.returned_version != 0x00010008U) {
        return fail("unsupported JNI_OnLoad version was accepted");
    }

    if (invoke_a32_jni_on_load(
            memory,
            graph,
            9U,
            0x1100U,
            registry,
            options).error !=
        A32JniOnLoadError::InvalidObject) {
        return fail("invalid JNI_OnLoad object index was accepted");
    }

    auto invalid_options = options;
    invalid_options.max_service_calls = 0U;
    if (invoke_a32_jni_on_load(
            memory,
            graph,
            1U,
            0x1100U,
            registry,
            invalid_options).error !=
        A32JniOnLoadError::InvalidOptions) {
        return fail("zero JNI_OnLoad service budget was accepted");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_vm_install_and_getenv();
        status != 0) {
        return status;
    }
    if (const int status = test_observed_member_id_lookup();
        status != 0) {
        return status;
    }
    if (const int status =
            test_find_class_register_natives_and_reverse_dispatch();
        status != 0) {
        return status;
    }
    if (const int status = test_vm_layout_and_install_rollback();
        status != 0) {
        return status;
    }
    return test_exact_object_onload_and_version_validation();
}
