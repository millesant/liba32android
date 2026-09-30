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
using liba32android::compat::A32JniMethodCallBridge;
using liba32android::compat::A32JniValue;
using liba32android::compat::A32JniValueKind;
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
using liba32android::compat::kA32JniNewGlobalRefSvcImmediate;
using liba32android::compat::kA32JniDeleteGlobalRefSvcImmediate;
using liba32android::compat::kA32JniDeleteLocalRefSvcImmediate;
using liba32android::compat::kA32JniGetArrayLengthSvcImmediate;
using liba32android::compat::kA32JniGetStaticIntFieldSvcImmediate;
using liba32android::compat::kA32JniNewStringUtfSvcImmediate;
using liba32android::compat::kA32JniGetStringUtfCharsSvcImmediate;
using liba32android::compat::kA32JniReleaseStringUtfCharsSvcImmediate;
using liba32android::compat::kA32JniNewLongArraySvcImmediate;
using liba32android::compat::kA32JniGetLongArrayElementsSvcImmediate;
using liba32android::compat::kA32JniReleaseLongArrayElementsSvcImmediate;
using liba32android::compat::kA32JniSetLongArrayRegionSvcImmediate;
using liba32android::compat::kA32JniNewObjectArraySvcImmediate;
using liba32android::compat::kA32JniGetObjectArrayElementSvcImmediate;
using liba32android::compat::kA32JniSetObjectArrayElementSvcImmediate;
using liba32android::compat::kA32JniGetLongFieldSvcImmediate;
using liba32android::compat::kA32JniSetLongFieldSvcImmediate;
using liba32android::compat::kA32JniThrowNewSvcImmediate;
using liba32android::compat::kA32JniCallVoidMethodVSvcImmediate;
using liba32android::compat::kA32JniGetByteArrayElementsSvcImmediate;
using liba32android::compat::kA32JniReleaseByteArrayElementsSvcImmediate;
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

bool write_i64_values(
    GuestMemory& memory,
    std::uint32_t address,
    const std::vector<std::int64_t>& values) {
    std::vector<std::uint8_t> bytes(values.size() * 8U, 0U);
    for (std::size_t index = 0U;
         index < values.size();
         ++index) {
        const std::uint64_t bits =
            static_cast<std::uint64_t>(values[index]);
        for (std::size_t byte = 0U; byte < 8U; ++byte) {
            bytes[index * 8U + byte] =
                static_cast<std::uint8_t>(
                    bits >> (byte * 8U));
        }
    }
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

class RecordingMethodCallBridge final : public A32JniMethodCallBridge {
public:
    bool accept_calls{true};
    std::size_t calls{};
    std::uint32_t receiver{};
    std::uint32_t method_handle{};
    std::string signature;
    std::vector<A32JniValue> arguments;

    bool call_void_method(
        std::uint32_t next_receiver,
        const liba32android::compat::A32JniMemberId& method,
        std::span<const A32JniValue> next_arguments) override {
        ++calls;
        receiver = next_receiver;
        method_handle = method.handle;
        signature = method.signature;
        arguments.assign(
            next_arguments.begin(),
            next_arguments.end());
        return accept_calls;
    }
};

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
        .new_global_ref_stub_address = 0x1900U,
        .delete_global_ref_stub_address = 0x1920U,
        .delete_local_ref_stub_address = 0x1940U,
        .get_array_length_stub_address = 0x1960U,
        .get_static_int_field_stub_address = 0x1980U,
        .new_string_utf_stub_address = 0x19a0U,
        .get_string_utf_chars_stub_address = 0x19c0U,
        .release_string_utf_chars_stub_address = 0x19e0U,
        .string_utf_scratch_address = 0x1c00U,
        .string_utf_scratch_bytes = 64U,
        .new_long_array_stub_address = 0x1a00U,
        .get_long_array_elements_stub_address = 0x1a20U,
        .release_long_array_elements_stub_address = 0x1a40U,
        .set_long_array_region_stub_address = 0x1a60U,
        .long_array_scratch_address = 0x1d00U,
        .long_array_scratch_bytes = 64U,
        .new_object_array_stub_address = 0x1a80U,
        .get_object_array_element_stub_address = 0x1aa0U,
        .set_object_array_element_stub_address = 0x1ac0U,
        .get_long_field_stub_address = 0x1ae0U,
        .set_long_field_stub_address = 0x1b00U,
        .throw_new_stub_address = 0x1b20U,
        .call_void_method_v_stub_address = 0x1b40U,
        .get_byte_array_elements_stub_address = 0x1b60U,
        .release_byte_array_elements_stub_address = 0x1b80U,
        .byte_array_scratch_address = 0x1e00U,
        .byte_array_scratch_bytes = 64U,
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
        read_u32(memory, configured.native_table_address + 14U * 4U) !=
            configured.throw_new_stub_address ||
        read_u32(memory, configured.native_table_address + 21U * 4U) !=
            configured.new_global_ref_stub_address ||
        read_u32(memory, configured.native_table_address + 22U * 4U) !=
            configured.delete_global_ref_stub_address ||
        read_u32(memory, configured.native_table_address + 23U * 4U) !=
            configured.delete_local_ref_stub_address ||
        read_u32(memory, configured.native_table_address + 33U * 4U) !=
            configured.get_method_id_stub_address ||
        read_u32(memory, configured.native_table_address + 62U * 4U) !=
            configured.call_void_method_v_stub_address ||
        read_u32(memory, configured.native_table_address + 94U * 4U) !=
            configured.get_field_id_stub_address ||
        read_u32(memory, configured.native_table_address + 101U * 4U) !=
            configured.get_long_field_stub_address ||
        read_u32(memory, configured.native_table_address + 110U * 4U) !=
            configured.set_long_field_stub_address ||
        read_u32(memory, configured.native_table_address + 144U * 4U) !=
            configured.get_static_field_id_stub_address ||
        read_u32(memory, configured.native_table_address + 150U * 4U) !=
            configured.get_static_int_field_stub_address ||
        read_u32(memory, configured.native_table_address + 167U * 4U) !=
            configured.new_string_utf_stub_address ||
        read_u32(memory, configured.native_table_address + 169U * 4U) !=
            configured.get_string_utf_chars_stub_address ||
        read_u32(memory, configured.native_table_address + 170U * 4U) !=
            configured.release_string_utf_chars_stub_address ||
        read_u32(memory, configured.native_table_address + 171U * 4U) !=
            configured.get_array_length_stub_address ||
        read_u32(memory, configured.native_table_address + 172U * 4U) !=
            configured.new_object_array_stub_address ||
        read_u32(memory, configured.native_table_address + 173U * 4U) !=
            configured.get_object_array_element_stub_address ||
        read_u32(memory, configured.native_table_address + 174U * 4U) !=
            configured.set_object_array_element_stub_address ||
        read_u32(memory, configured.native_table_address + 180U * 4U) !=
            configured.new_long_array_stub_address ||
        read_u32(memory, configured.native_table_address + 184U * 4U) !=
            configured.get_byte_array_elements_stub_address ||
        read_u32(memory, configured.native_table_address + 188U * 4U) !=
            configured.get_long_array_elements_stub_address ||
        read_u32(memory, configured.native_table_address + 192U * 4U) !=
            configured.release_byte_array_elements_stub_address ||
        read_u32(memory, configured.native_table_address + 196U * 4U) !=
            configured.release_long_array_elements_stub_address ||
        read_u32(memory, configured.native_table_address + 212U * 4U) !=
            configured.set_long_array_region_stub_address ||
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
            index == 14U ||
            index == 21U ||
            index == 22U ||
            index == 23U ||
            index == 33U ||
            index == 62U ||
            index == 94U ||
            index == 101U ||
            index == 110U ||
            index == 144U ||
            index == 150U ||
            index == 167U ||
            index == 169U ||
            index == 170U ||
            index == 171U ||
            index == 172U ||
            index == 173U ||
            index == 174U ||
            index == 180U ||
            index == 184U ||
            index == 188U ||
            index == 192U ||
            index == 196U ||
            index == 212U ||
            index == 215U) {
            continue;
        }
        if (read_u32(
                memory,
                configured.native_table_address + index * 4U) != 0U) {
            return fail("unsupported JNIEnv native slot was non-null");
        }
    }

    constexpr std::array<std::array<std::uint8_t, 8>, 29> expected_stubs{{
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
        {{0xDFU, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xE0U, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xE1U, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xE2U, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xE3U, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xE4U, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xE5U, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xE6U, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xE7U, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xE8U, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xE9U, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xEAU, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xEBU, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xECU, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xEDU, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xEEU, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xEFU, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xF0U, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xF1U, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xF2U, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
        {{0xF3U, 0x00U, 0x00U, 0xEFU,
          0x1EU, 0xFFU, 0x2FU, 0xE1U}},
    }};
    const std::array<std::uint32_t, 29> stub_addresses{{
        configured.get_env_stub_address,
        configured.find_class_stub_address,
        configured.register_natives_stub_address,
        configured.get_method_id_stub_address,
        configured.get_field_id_stub_address,
        configured.get_static_field_id_stub_address,
        configured.attach_current_thread_stub_address,
        configured.detach_current_thread_stub_address,
        configured.new_global_ref_stub_address,
        configured.delete_global_ref_stub_address,
        configured.delete_local_ref_stub_address,
        configured.get_array_length_stub_address,
        configured.get_static_int_field_stub_address,
        configured.new_string_utf_stub_address,
        configured.get_string_utf_chars_stub_address,
        configured.release_string_utf_chars_stub_address,
        configured.new_long_array_stub_address,
        configured.get_long_array_elements_stub_address,
        configured.release_long_array_elements_stub_address,
        configured.set_long_array_region_stub_address,
        configured.new_object_array_stub_address,
        configured.get_object_array_element_stub_address,
        configured.set_object_array_element_stub_address,
        configured.get_long_field_stub_address,
        configured.set_long_field_stub_address,
        configured.throw_new_stub_address,
        configured.call_void_method_v_stub_address,
        configured.get_byte_array_elements_stub_address,
        configured.release_byte_array_elements_stub_address,
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
            kA32JniReleaseByteArrayElementsSvcImmediate + 1U,
            regs,
            cpsr) != A32HostServiceDisposition::Unhandled) {
        return fail("JNI service accepted wrong SVC immediate");
    }
    return 0;
}

int test_strong_reference_lifetime() {
    LinearGuestMemory memory{0x1000U, 0x1000U};
    const A32JniRegistryLimits limits{
        .max_classes = 2U,
        .max_registered_methods = 2U,
        .max_methods_per_registration = 2U,
        .max_member_ids = 2U,
        .max_reference_handles = 2U,
        .max_reference_count_per_handle = 2U,
        .max_class_name_bytes = 32U,
        .max_method_name_bytes = 32U,
        .max_signature_bytes = 32U,
    };
    A32JniClassRegistry registry{limits};
    constexpr std::uint32_t kClassHandle = 0x44550000U;
    constexpr std::uint32_t kObjectHandle = 0x44560000U;
    if (registry.add_class(
            kClassHandle,
            "org/videolan/Fixture") !=
            A32JniRegistryError::None ||
        registry.add_reference_identity(kObjectHandle) !=
            A32JniRegistryError::None ||
        registry.add_reference_identity(0x44570000U) !=
            A32JniRegistryError::ReferenceLimitExceeded ||
        !registry.retain_local_reference(kObjectHandle)) {
        return fail("could not seed bounded JNI reference identities");
    }

    auto class_counts = registry.reference_counts(kClassHandle);
    auto object_counts = registry.reference_counts(kObjectHandle);
    if (!class_counts.has_value() ||
        class_counts->local != 0U ||
        class_counts->global != 0U ||
        !object_counts.has_value() ||
        object_counts->local != 1U ||
        object_counts->global != 0U) {
        return fail("JNI reference ledger seeded wrong counts");
    }

    const auto configured = layout();
    A32JniVmService service{configured, &registry};
    if (!service.install(memory)) {
        return fail("JNI reference service did not install");
    }

    constexpr std::uint32_t kClassName = 0x1600U;
    if (!write_c_string(
            memory,
            kClassName,
            "org/videolan/Fixture")) {
        return fail("could not stage JNI reference class name");
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
        return fail("JNI FindClass did not create local class reference");
    }
    class_counts = registry.reference_counts(kClassHandle);
    if (!class_counts.has_value() ||
        class_counts->local != 1U ||
        class_counts->global != 0U) {
        return fail("JNI FindClass retained wrong local reference count");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kClassHandle;
    if (service.handle(
            memory,
            kA32JniNewGlobalRefSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != kClassHandle) {
        return fail("JNI NewGlobalRef did not preserve opaque handle");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kClassHandle;
    if (service.handle(
            memory,
            kA32JniDeleteLocalRefSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled) {
        return fail("JNI DeleteLocalRef rejected live local reference");
    }
    class_counts = registry.reference_counts(kClassHandle);
    if (!class_counts.has_value() ||
        class_counts->local != 0U ||
        class_counts->global != 1U) {
        return fail("JNI reference kinds were not tracked independently");
    }

    for (std::size_t index = 0U; index < 2U; ++index) {
        regs = {};
        regs[0] = configured.jni_env_address;
        regs[1] = kClassHandle;
        const auto disposition = service.handle(
            memory,
            kA32JniNewGlobalRefSvcImmediate,
            regs,
            cpsr);
        if (disposition != A32HostServiceDisposition::Handled) {
            return fail("JNI NewGlobalRef service failed");
        }
        if (index == 0U && regs[0] != kClassHandle) {
            return fail("JNI NewGlobalRef rejected live global reference");
        }
        if (index == 1U && regs[0] != 0U) {
            return fail("JNI NewGlobalRef exceeded reference count limit");
        }
    }

    for (std::size_t index = 0U; index < 2U; ++index) {
        regs = {};
        regs[0] = configured.jni_env_address;
        regs[1] = kClassHandle;
        if (service.handle(
                memory,
                kA32JniDeleteGlobalRefSvcImmediate,
                regs,
                cpsr) != A32HostServiceDisposition::Handled) {
            return fail("JNI DeleteGlobalRef rejected live global reference");
        }
    }
    class_counts = registry.reference_counts(kClassHandle);
    if (!class_counts.has_value() ||
        class_counts->local != 0U ||
        class_counts->global != 0U) {
        return fail("JNI reference deletion did not reach dead state");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kClassHandle;
    if (service.handle(
            memory,
            kA32JniNewGlobalRefSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("JNI NewGlobalRef revived dead reference");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = 0U;
    if (service.handle(
            memory,
            kA32JniNewGlobalRefSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("JNI NewGlobalRef null behavior is wrong");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = 0U;
    if (service.handle(
            memory,
            kA32JniDeleteLocalRefSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled) {
        return fail("JNI DeleteLocalRef rejected null");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kObjectHandle;
    if (service.handle(
            memory,
            kA32JniNewGlobalRefSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != kObjectHandle) {
        return fail("JNI reference service was class-specific");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = 0x99887766U;
    if (service.handle(
            memory,
            kA32JniDeleteGlobalRefSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI DeleteGlobalRef accepted unknown reference");
    }

    return 0;
}

int test_seeded_array_length() {
    LinearGuestMemory memory{0x1000U, 0x1000U};
    const A32JniRegistryLimits limits{
        .max_classes = 2U,
        .max_registered_methods = 2U,
        .max_methods_per_registration = 2U,
        .max_member_ids = 2U,
        .max_reference_handles = 4U,
        .max_reference_count_per_handle = 4U,
        .max_arrays = 2U,
        .max_class_name_bytes = 32U,
        .max_method_name_bytes = 32U,
        .max_signature_bytes = 32U,
    };
    A32JniClassRegistry registry{limits};
    constexpr std::uint32_t kArray = 0x44560000U;
    constexpr std::uint32_t kEmptyArray = 0x44560004U;
    if (registry.add_array(kArray, 7U) != A32JniRegistryError::None ||
        registry.add_array(kEmptyArray, 0U) !=
            A32JniRegistryError::None ||
        registry.array_count() != 2U ||
        registry.add_array(kArray, 9U) !=
            A32JniRegistryError::DuplicateArrayHandle ||
        registry.add_array(0x44560008U, 1U) !=
            A32JniRegistryError::ArrayLimitExceeded ||
        registry.add_array(0U, 1U) !=
            A32JniRegistryError::InvalidArrayHandle ||
        registry.add_array(
            0x4456000cU,
            0x80000000U) !=
            A32JniRegistryError::InvalidArrayLength) {
        return fail("JNI array registry bounds/identity are wrong");
    }

    const auto counts = registry.reference_counts(kArray);
    if (!counts.has_value() ||
        counts->local != 1U ||
        counts->global != 0U ||
        registry.array_length(kArray).value_or(99U) != 7U ||
        registry.array_length(kEmptyArray).value_or(99U) != 0U) {
        return fail("JNI seeded arrays lost metadata/reference ownership");
    }

    const auto configured = layout();
    A32JniVmService service{configured, &registry};
    if (!service.install(memory)) {
        return fail("JNI array-length service did not install");
    }

    std::uint32_t cpsr{};
    std::array<std::uint32_t, 16> regs{};
    regs[0] = configured.jni_env_address;
    regs[1] = kArray;
    if (service.handle(
            memory,
            kA32JniGetArrayLengthSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 7U) {
        return fail("JNI GetArrayLength returned wrong seeded length");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kEmptyArray;
    if (service.handle(
            memory,
            kA32JniGetArrayLengthSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("JNI GetArrayLength rejected zero-length array");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = 0U;
    if (service.handle(
            memory,
            kA32JniGetArrayLengthSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI GetArrayLength accepted null");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = 0x99887766U;
    if (service.handle(
            memory,
            kA32JniGetArrayLengthSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI GetArrayLength accepted unknown array");
    }

    regs = {};
    regs[0] = configured.jni_env_address + 4U;
    regs[1] = kArray;
    if (service.handle(
            memory,
            kA32JniGetArrayLengthSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI GetArrayLength accepted wrong JNIEnv pointer");
    }

    return 0;
}

int test_static_int_field_value() {
    LinearGuestMemory memory{0x1000U, 0x1000U};
    const A32JniRegistryLimits limits{
        .max_classes = 2U,
        .max_registered_methods = 2U,
        .max_methods_per_registration = 2U,
        .max_member_ids = 4U,
        .max_reference_handles = 4U,
        .max_reference_count_per_handle = 4U,
        .max_arrays = 2U,
        .max_class_name_bytes = 32U,
        .max_method_name_bytes = 32U,
        .max_signature_bytes = 32U,
    };
    A32JniClassRegistry registry{limits};
    constexpr std::uint32_t kClass = 0x44550000U;
    constexpr std::uint32_t kOtherClass = 0x44550004U;
    constexpr std::uint32_t kStaticField = 0x44551000U;
    constexpr std::uint32_t kUnsetStaticField = 0x44551004U;
    constexpr std::uint32_t kInstanceField = 0x44551008U;
    if (registry.add_class(kClass, "org/videolan/Fixture") !=
            A32JniRegistryError::None ||
        registry.add_class(kOtherClass, "org/videolan/Other") !=
            A32JniRegistryError::None ||
        registry.add_member(
            kClass,
            A32JniMemberKind::StaticField,
            kStaticField,
            "answer",
            "I") != A32JniRegistryError::None ||
        registry.add_member(
            kClass,
            A32JniMemberKind::StaticField,
            kUnsetStaticField,
            "unset",
            "I") != A32JniRegistryError::None ||
        registry.add_member(
            kClass,
            A32JniMemberKind::InstanceField,
            kInstanceField,
            "instance",
            "I") != A32JniRegistryError::None ||
        registry.set_static_int_field_value(
            kStaticField,
            -42) != A32JniRegistryError::None ||
        registry.set_static_int_field_value(
            kInstanceField,
            7) != A32JniRegistryError::InvalidMemberKind ||
        registry.set_static_int_field_value(
            0x99887766U,
            7) != A32JniRegistryError::InvalidMemberHandle) {
        return fail("could not seed bounded JNI static-int field state");
    }

    if (registry.static_int_field_value(kStaticField).value_or(0) != -42 ||
        registry.static_int_field_value(kUnsetStaticField).has_value()) {
        return fail("JNI static-int registry retained wrong value state");
    }

    const auto configured = layout();
    A32JniVmService service{configured, &registry};
    if (!service.install(memory)) {
        return fail("JNI static-int field service did not install");
    }

    std::uint32_t cpsr{};
    std::array<std::uint32_t, 16> regs{};
    regs[0] = configured.jni_env_address;
    regs[1] = kClass;
    regs[2] = kStaticField;
    if (service.handle(
            memory,
            kA32JniGetStaticIntFieldSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != static_cast<std::uint32_t>(
            static_cast<std::int32_t>(-42))) {
        return fail("JNI GetStaticIntField returned wrong jint bits");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kOtherClass;
    regs[2] = kStaticField;
    if (service.handle(
            memory,
            kA32JniGetStaticIntFieldSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI GetStaticIntField accepted wrong class");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kClass;
    regs[2] = kInstanceField;
    if (service.handle(
            memory,
            kA32JniGetStaticIntFieldSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI GetStaticIntField accepted instance field");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kClass;
    regs[2] = kUnsetStaticField;
    if (service.handle(
            memory,
            kA32JniGetStaticIntFieldSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI GetStaticIntField fabricated missing value");
    }

    if (registry.set_static_int_field_value(
            kStaticField,
            42) != A32JniRegistryError::None ||
        registry.static_int_field_value(kStaticField).value_or(0) != 42) {
        return fail("JNI static-int field update was not deterministic");
    }

    return 0;
}

int test_modified_utf8_strings() {
    LinearGuestMemory memory{0x1000U, 0x1000U};
    const A32JniRegistryLimits limits{
        .max_classes = 2U,
        .max_registered_methods = 2U,
        .max_methods_per_registration = 2U,
        .max_member_ids = 2U,
        .max_reference_handles = 4U,
        .max_reference_count_per_handle = 4U,
        .max_arrays = 2U,
        .max_strings = 2U,
        .max_modified_utf8_bytes = 8U,
        .dynamic_string_handle_base = 0x44570000U,
        .dynamic_string_handle_stride = 4U,
        .max_class_name_bytes = 32U,
        .max_method_name_bytes = 32U,
        .max_signature_bytes = 32U,
    };
    A32JniClassRegistry registry{limits};
    if (registry.add_reference_identity(0x44570000U) !=
        A32JniRegistryError::None) {
        return fail("could not seed JNI dynamic-string handle collision");
    }

    const auto configured = layout();
    A32JniVmService service{configured, &registry};
    if (!service.install(memory)) {
        return fail("JNI modified-UTF8 service did not install");
    }

    constexpr std::uint32_t kHello = 0x1600U;
    constexpr std::uint32_t kEmpty = 0x1620U;
    constexpr std::uint32_t kTooLong = 0x1640U;
    constexpr std::uint32_t kIsCopy = 0x1680U;
    if (!write_c_string(memory, kHello, "hello") ||
        !write_c_string(memory, kEmpty, "") ||
        !write_c_string(memory, kTooLong, "123456789")) {
        return fail("could not stage JNI modified-UTF8 inputs");
    }

    std::uint32_t cpsr{};
    std::array<std::uint32_t, 16> regs{};
    regs[0] = configured.jni_env_address;
    regs[1] = kHello;
    if (service.handle(
            memory,
            kA32JniNewStringUtfSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0x44570004U ||
        registry.string_count() != 1U) {
        return fail("JNI NewStringUTF did not allocate bounded logical string");
    }
    const std::uint32_t string_handle = regs[0];
    const auto* info =
        registry.find_modified_utf8_string(string_handle);
    const auto counts = registry.reference_counts(string_handle);
    if (info == nullptr ||
        info->modified_utf8 != "hello" ||
        !counts.has_value() ||
        counts->local != 1U ||
        counts->global != 0U) {
        return fail("JNI NewStringUTF lost string/reference metadata");
    }

    if (!write_u32(memory, kIsCopy, 0U)) {
        return fail("could not seed JNI isCopy output");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = string_handle;
    regs[2] = kIsCopy;
    if (service.handle(
            memory,
            kA32JniGetStringUtfCharsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != configured.string_utf_scratch_address ||
        !service.utf_chars_lease_active()) {
        return fail("JNI GetStringUTFChars did not publish scratch copy");
    }
    std::array<std::uint8_t, 6> observed{};
    const std::array<std::uint8_t, 6> expected{{
        'h', 'e', 'l', 'l', 'o', 0U,
    }};
    std::array<std::uint8_t, 1> is_copy{};
    if (!memory.read(configured.string_utf_scratch_address, observed) ||
        observed != expected ||
        !memory.read(kIsCopy, is_copy) ||
        is_copy[0] != 1U) {
        return fail("JNI GetStringUTFChars published wrong bytes/isCopy");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = string_handle;
    if (service.handle(
            memory,
            kA32JniGetStringUtfCharsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI GetStringUTFChars allowed overlapping scratch lease");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = string_handle;
    regs[2] = configured.string_utf_scratch_address + 4U;
    if (service.handle(
            memory,
            kA32JniReleaseStringUtfCharsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed ||
        !service.utf_chars_lease_active()) {
        return fail("JNI ReleaseStringUTFChars accepted wrong pointer");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = string_handle;
    regs[2] = configured.string_utf_scratch_address;
    if (service.handle(
            memory,
            kA32JniReleaseStringUtfCharsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        service.utf_chars_lease_active()) {
        return fail("JNI ReleaseStringUTFChars did not release lease");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = string_handle;
    if (service.handle(
            memory,
            kA32JniDeleteLocalRefSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled) {
        return fail("JNI string local reference could not be deleted");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = string_handle;
    if (service.handle(
            memory,
            kA32JniGetStringUtfCharsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("JNI GetStringUTFChars revived dead string reference");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kTooLong;
    if (service.handle(
            memory,
            kA32JniNewStringUtfSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI NewStringUTF accepted over-limit input");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kEmpty;
    if (service.handle(
            memory,
            kA32JniNewStringUtfSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] == 0U ||
        registry.string_count() != 2U) {
        return fail("JNI NewStringUTF rejected empty modified-UTF8 string");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kHello;
    if (service.handle(
            memory,
            kA32JniNewStringUtfSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("JNI NewStringUTF exceeded bounded string count");
    }

    return 0;
}

int test_long_array_family() {
    LinearGuestMemory memory{0x1000U, 0x1000U};
    const A32JniRegistryLimits limits{
        .max_classes = 2U,
        .max_registered_methods = 2U,
        .max_methods_per_registration = 2U,
        .max_member_ids = 2U,
        .max_reference_handles = 6U,
        .max_reference_count_per_handle = 4U,
        .max_arrays = 3U,
        .max_long_array_elements = 4U,
        .dynamic_array_handle_base = 0x44580000U,
        .dynamic_array_handle_stride = 4U,
        .max_strings = 2U,
        .max_modified_utf8_bytes = 8U,
        .dynamic_string_handle_base = 0x44590000U,
        .dynamic_string_handle_stride = 4U,
        .max_class_name_bytes = 32U,
        .max_method_name_bytes = 32U,
        .max_signature_bytes = 32U,
    };
    A32JniClassRegistry registry{limits};
    if (registry.add_reference_identity(0x44580000U) !=
        A32JniRegistryError::None) {
        return fail("could not seed JNI long-array handle collision");
    }

    const auto configured = layout();
    A32JniVmService service{configured, &registry};
    if (!service.install(memory)) {
        return fail("JNI long-array service did not install");
    }

    std::uint32_t cpsr{};
    std::array<std::uint32_t, 16> regs{};
    regs[0] = configured.jni_env_address;
    regs[1] = static_cast<std::uint32_t>(-1);
    if (service.handle(
            memory,
            kA32JniNewLongArraySvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("JNI NewLongArray accepted negative length");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = 3U;
    if (service.handle(
            memory,
            kA32JniNewLongArraySvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0x44580004U) {
        return fail("JNI NewLongArray did not skip occupied handle");
    }
    const std::uint32_t array_handle = regs[0];
    const auto* created = registry.find_long_array(array_handle);
    const auto counts = registry.reference_counts(array_handle);
    if (created == nullptr ||
        created->elements !=
            std::vector<std::int64_t>({0, 0, 0}) ||
        registry.array_length(array_handle).value_or(99U) != 3U ||
        !counts.has_value() ||
        counts->local != 1U ||
        counts->global != 0U) {
        return fail("JNI NewLongArray lost zero/init/reference metadata");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = array_handle;
    if (service.handle(
            memory,
            kA32JniGetArrayLengthSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 3U) {
        return fail("JNI GetArrayLength missed dynamic jlong array");
    }

    constexpr std::uint32_t kSource = 0x1700U;
    constexpr std::uint32_t kStack = 0x17f0U;
    const std::vector<std::int64_t> first_values{
        1,
        -2,
        static_cast<std::int64_t>(
            UINT64_C(0x1122334455667788)),
    };
    if (!write_i64_values(memory, kSource, first_values) ||
        !write_u32(memory, kStack, kSource)) {
        return fail("could not stage JNI SetLongArrayRegion input");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = array_handle;
    regs[2] = 0U;
    regs[3] = 3U;
    regs[13] = kStack;
    if (service.handle(
            memory,
            kA32JniSetLongArrayRegionSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        registry.find_long_array(array_handle) == nullptr ||
        registry.find_long_array(array_handle)->elements != first_values) {
        return fail("JNI SetLongArrayRegion decoded wrong ARM32 arguments");
    }

    constexpr std::uint32_t kIsCopy = 0x16f0U;
    if (!write_u32(memory, kIsCopy, 0U)) {
        return fail("could not seed JNI long-array isCopy");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = array_handle;
    regs[2] = kIsCopy;
    if (service.handle(
            memory,
            kA32JniGetLongArrayElementsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != configured.long_array_scratch_address ||
        !service.long_array_lease_active()) {
        return fail("JNI GetLongArrayElements did not open copy lease");
    }
    std::array<std::uint8_t, 1> is_copy{};
    if (!memory.read(kIsCopy, is_copy) || is_copy[0] != 1U) {
        return fail("JNI GetLongArrayElements did not report JNI_TRUE");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = array_handle;
    if (service.handle(
            memory,
            kA32JniGetLongArrayElementsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI GetLongArrayElements allowed overlapping lease");
    }

    const std::vector<std::int64_t> committed{10, 20, 30};
    if (!write_i64_values(
            memory,
            configured.long_array_scratch_address,
            committed)) {
        return fail("could not edit JNI long-array scratch");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = array_handle;
    regs[2] = configured.long_array_scratch_address;
    regs[3] = 1U;
    if (service.handle(
            memory,
            kA32JniReleaseLongArrayElementsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        !service.long_array_lease_active() ||
        registry.find_long_array(array_handle)->elements != committed) {
        return fail("JNI_COMMIT did not copy back/retain lease");
    }

    const std::vector<std::int64_t> aborted{40, 50, 60};
    if (!write_i64_values(
            memory,
            configured.long_array_scratch_address,
            aborted)) {
        return fail("could not edit JNI long-array abort scratch");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = array_handle;
    regs[2] = configured.long_array_scratch_address;
    regs[3] = 2U;
    if (service.handle(
            memory,
            kA32JniReleaseLongArrayElementsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        service.long_array_lease_active() ||
        registry.find_long_array(array_handle)->elements != committed) {
        return fail("JNI_ABORT copied back or retained lease");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = array_handle;
    if (service.handle(
            memory,
            kA32JniGetLongArrayElementsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled) {
        return fail("JNI long-array second lease failed");
    }
    const std::vector<std::int64_t> final_values{-1, -2, -3};
    if (!write_i64_values(
            memory,
            configured.long_array_scratch_address,
            final_values)) {
        return fail("could not stage JNI long-array final copy");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = array_handle;
    regs[2] = configured.long_array_scratch_address + 8U;
    regs[3] = 0U;
    if (service.handle(
            memory,
            kA32JniReleaseLongArrayElementsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed ||
        !service.long_array_lease_active()) {
        return fail("JNI long-array release accepted wrong pointer");
    }
    regs[2] = configured.long_array_scratch_address;
    if (service.handle(
            memory,
            kA32JniReleaseLongArrayElementsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        service.long_array_lease_active() ||
        registry.find_long_array(array_handle)->elements != final_values) {
        return fail("JNI long-array mode-0 release did not copy/release");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = array_handle;
    regs[2] = 2U;
    regs[3] = 2U;
    regs[13] = kStack;
    if (service.handle(
            memory,
            kA32JniSetLongArrayRegionSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI SetLongArrayRegion accepted out-of-bounds range");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = 5U;
    if (service.handle(
            memory,
            kA32JniNewLongArraySvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("JNI NewLongArray exceeded element ceiling");
    }

    return 0;
}

int test_byte_array_elements() {
    LinearGuestMemory memory{0x1000U, 0x1000U};
    const A32JniRegistryLimits limits{
        .max_classes = 2U,
        .max_registered_methods = 2U,
        .max_methods_per_registration = 2U,
        .max_member_ids = 2U,
        .max_reference_handles = 6U,
        .max_reference_count_per_handle = 4U,
        .max_arrays = 3U,
        .max_long_array_elements = 4U,
        .max_byte_array_elements = 4U,
        .max_object_array_elements = 4U,
        .max_strings = 2U,
        .max_modified_utf8_bytes = 8U,
        .max_class_name_bytes = 32U,
        .max_method_name_bytes = 32U,
        .max_signature_bytes = 32U,
    };
    A32JniClassRegistry registry{limits};
    constexpr std::uint32_t kArray = 0x44582000U;
    constexpr std::array<std::uint8_t, 3> kInitial{{0x01U, 0x80U, 0xffU}};
    if (registry.add_byte_array(kArray, kInitial) !=
            A32JniRegistryError::None ||
        registry.array_length(kArray).value_or(99U) != 3U ||
        registry.find_byte_array(kArray) == nullptr ||
        registry.find_byte_array(kArray)->elements !=
            std::vector<std::uint8_t>(kInitial.begin(), kInitial.end()) ||
        !registry.reference_counts(kArray).has_value() ||
        registry.reference_counts(kArray)->local != 1U) {
        return fail("could not seed bounded JNI byte array");
    }
    constexpr std::array<std::uint8_t, 5> kTooMany{{1U, 2U, 3U, 4U, 5U}};
    if (registry.add_byte_array(0x44582004U, kTooMany) !=
            A32JniRegistryError::InvalidArrayLength) {
        return fail("JNI byte-array seed exceeded element ceiling");
    }

    const auto configured = layout();
    A32JniVmService service{configured, &registry};
    if (!service.install(memory)) {
        return fail("JNI byte-array service did not install");
    }

    std::uint32_t cpsr{};
    std::array<std::uint32_t, 16> regs{};
    regs[0] = configured.jni_env_address;
    regs[1] = kArray;
    if (service.handle(
            memory,
            kA32JniGetArrayLengthSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 3U) {
        return fail("JNI GetArrayLength missed seeded byte array");
    }

    constexpr std::uint32_t kIsCopy = 0x16f0U;
    if (!write_u32(memory, kIsCopy, 0U)) {
        return fail("could not seed JNI byte-array isCopy output");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kArray;
    regs[2] = kIsCopy;
    if (service.handle(
            memory,
            kA32JniGetByteArrayElementsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != configured.byte_array_scratch_address ||
        !service.byte_array_lease_active()) {
        return fail("JNI GetByteArrayElements did not open copy lease");
    }
    std::array<std::uint8_t, 3> observed{};
    std::array<std::uint8_t, 1> is_copy{};
    if (!memory.read(configured.byte_array_scratch_address, observed) ||
        observed != kInitial ||
        !memory.read(kIsCopy, is_copy) ||
        is_copy[0] != 1U) {
        return fail("JNI GetByteArrayElements published wrong bytes/isCopy");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kArray;
    if (service.handle(
            memory,
            kA32JniGetByteArrayElementsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI GetByteArrayElements allowed overlapping lease");
    }

    constexpr std::array<std::uint8_t, 3> kCommitted{{0x10U, 0x20U, 0x30U}};
    if (!memory.write(configured.byte_array_scratch_address, kCommitted)) {
        return fail("could not stage JNI byte-array commit bytes");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kArray;
    regs[2] = configured.byte_array_scratch_address;
    regs[3] = 1U;
    if (service.handle(
            memory,
            kA32JniReleaseByteArrayElementsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        !service.byte_array_lease_active() ||
        registry.find_byte_array(kArray)->elements !=
            std::vector<std::uint8_t>(kCommitted.begin(), kCommitted.end())) {
        return fail("JNI byte-array COMMIT did not copy/retain lease");
    }

    constexpr std::array<std::uint8_t, 3> kAborted{{0xaaU, 0xbbU, 0xccU}};
    if (!memory.write(configured.byte_array_scratch_address, kAborted)) {
        return fail("could not stage JNI byte-array abort bytes");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kArray;
    regs[2] = configured.byte_array_scratch_address;
    regs[3] = 2U;
    if (service.handle(
            memory,
            kA32JniReleaseByteArrayElementsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        service.byte_array_lease_active() ||
        registry.find_byte_array(kArray)->elements !=
            std::vector<std::uint8_t>(kCommitted.begin(), kCommitted.end())) {
        return fail("JNI byte-array ABORT changed owned bytes or kept lease");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kArray;
    if (service.handle(
            memory,
            kA32JniGetByteArrayElementsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        !service.byte_array_lease_active()) {
        return fail("JNI byte-array reacquire failed");
    }
    constexpr std::array<std::uint8_t, 3> kFinal{{0x7fU, 0x00U, 0x81U}};
    if (!memory.write(configured.byte_array_scratch_address, kFinal)) {
        return fail("could not stage JNI byte-array final bytes");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kArray;
    regs[2] = configured.byte_array_scratch_address + 1U;
    regs[3] = 0U;
    if (service.handle(
            memory,
            kA32JniReleaseByteArrayElementsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed ||
        !service.byte_array_lease_active()) {
        return fail("JNI byte-array release accepted wrong pointer");
    }
    regs[2] = configured.byte_array_scratch_address;
    regs[3] = 3U;
    if (service.handle(
            memory,
            kA32JniReleaseByteArrayElementsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed ||
        !service.byte_array_lease_active()) {
        return fail("JNI byte-array release accepted invalid mode");
    }
    regs[3] = 0U;
    if (service.handle(
            memory,
            kA32JniReleaseByteArrayElementsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        service.byte_array_lease_active() ||
        registry.find_byte_array(kArray)->elements !=
            std::vector<std::uint8_t>(kFinal.begin(), kFinal.end())) {
        return fail("JNI byte-array mode-0 release did not copy/release");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kArray;
    if (service.handle(
            memory,
            kA32JniDeleteLocalRefSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled) {
        return fail("could not retire seeded JNI byte array");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kArray;
    if (service.handle(
            memory,
            kA32JniGetByteArrayElementsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("JNI GetByteArrayElements revived dead array reference");
    }

    A32JniClassRegistry memory_registry{limits};
    if (memory_registry.add_byte_array(kArray, kInitial) !=
            A32JniRegistryError::None) {
        return fail("could not seed JNI byte array for memory failure");
    }
    A32JniVmService memory_service{configured, &memory_registry};
    if (!memory_service.install(memory)) {
        return fail("JNI byte-array memory-failure service did not install");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kArray;
    regs[2] = 0x3000U;
    if (memory_service.handle(
            memory,
            kA32JniGetByteArrayElementsSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed ||
        memory_service.byte_array_lease_active()) {
        return fail("JNI GetByteArrayElements accepted unreadable isCopy output");
    }

    return 0;
}

int test_object_array_family() {
    LinearGuestMemory memory{0x1000U, 0x1000U};
    const A32JniRegistryLimits limits{
        .max_classes = 2U,
        .max_registered_methods = 2U,
        .max_methods_per_registration = 2U,
        .max_member_ids = 2U,
        .max_reference_handles = 8U,
        .max_reference_count_per_handle = 8U,
        .max_arrays = 4U,
        .max_long_array_elements = 4U,
        .max_object_array_elements = 3U,
        .dynamic_array_handle_base = 0x44581000U,
        .dynamic_array_handle_stride = 4U,
        .max_strings = 2U,
        .max_modified_utf8_bytes = 8U,
        .dynamic_string_handle_base = 0x44590000U,
        .dynamic_string_handle_stride = 4U,
        .max_class_name_bytes = 32U,
        .max_method_name_bytes = 32U,
        .max_signature_bytes = 32U,
    };
    A32JniClassRegistry registry{limits};
    constexpr std::uint32_t kClass = 0x44550000U;
    constexpr std::uint32_t kObject = 0x44560000U;
    constexpr std::uint32_t kOtherObject = 0x44560004U;
    if (registry.add_class(kClass, "org/videolan/Fixture") !=
            A32JniRegistryError::None ||
        registry.add_reference_identity(kObject) !=
            A32JniRegistryError::None ||
        registry.add_reference_identity(kOtherObject) !=
            A32JniRegistryError::None ||
        registry.add_reference_identity(0x44581000U) !=
            A32JniRegistryError::None ||
        !registry.retain_local_reference(kObject) ||
        !registry.retain_local_reference(kOtherObject)) {
        return fail("could not seed JNI object-array identities");
    }

    const auto configured = layout();
    A32JniVmService service{configured, &registry};
    if (!service.install(memory)) {
        return fail("JNI object-array service did not install");
    }

    std::uint32_t cpsr{};
    std::array<std::uint32_t, 16> regs{};
    regs[0] = configured.jni_env_address;
    regs[1] = 3U;
    regs[2] = kClass;
    regs[3] = kObject;
    if (service.handle(
            memory,
            kA32JniNewObjectArraySvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0x44581004U) {
        return fail("JNI NewObjectArray did not create bounded array");
    }
    const std::uint32_t array_handle = regs[0];
    const auto* array = registry.find_object_array(array_handle);
    const auto array_counts = registry.reference_counts(array_handle);
    if (array == nullptr ||
        array->element_class_handle != kClass ||
        array->elements !=
            std::vector<std::uint32_t>({kObject, kObject, kObject}) ||
        registry.array_length(array_handle).value_or(99U) != 3U ||
        !array_counts.has_value() ||
        array_counts->local != 1U) {
        return fail("JNI NewObjectArray lost class/elements/lifetime");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = array_handle;
    regs[2] = 1U;
    if (service.handle(
            memory,
            kA32JniGetObjectArrayElementSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != kObject ||
        registry.reference_counts(kObject)->local != 2U) {
        return fail("JNI GetObjectArrayElement did not create local ref");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kObject;
    if (service.handle(
            memory,
            kA32JniDeleteLocalRefSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        registry.reference_counts(kObject)->local != 1U) {
        return fail("JNI object-array returned local ref did not release");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = array_handle;
    regs[2] = 1U;
    regs[3] = 0U;
    if (service.handle(
            memory,
            kA32JniSetObjectArrayElementSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        registry.find_object_array(array_handle)->elements[1] != 0U) {
        return fail("JNI SetObjectArrayElement did not store null");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = array_handle;
    regs[2] = 2U;
    regs[3] = kOtherObject;
    if (service.handle(
            memory,
            kA32JniSetObjectArrayElementSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        registry.find_object_array(array_handle)->elements[2] !=
            kOtherObject) {
        return fail("JNI SetObjectArrayElement did not store live ref");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kOtherObject;
    if (service.handle(
            memory,
            kA32JniDeleteLocalRefSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled) {
        return fail("could not make JNI object-array input dead");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = array_handle;
    regs[2] = 0U;
    regs[3] = kOtherObject;
    if (service.handle(
            memory,
            kA32JniSetObjectArrayElementSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI SetObjectArrayElement accepted dead input ref");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = array_handle;
    regs[2] = static_cast<std::uint32_t>(-1);
    if (service.handle(
            memory,
            kA32JniGetObjectArrayElementSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI GetObjectArrayElement accepted negative index");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = 4U;
    regs[2] = kClass;
    regs[3] = 0U;
    if (service.handle(
            memory,
            kA32JniNewObjectArraySvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("JNI NewObjectArray exceeded element ceiling");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = array_handle;
    if (service.handle(
            memory,
            kA32JniDeleteLocalRefSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled) {
        return fail("could not release JNI object array");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = array_handle;
    regs[2] = 0U;
    if (service.handle(
            memory,
            kA32JniGetObjectArrayElementSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI object-array access accepted dead array ref");
    }

    return 0;
}

int test_instance_long_field_value() {
    LinearGuestMemory memory{0x1000U, 0x1000U};
    const A32JniRegistryLimits limits{
        .max_classes = 2U,
        .max_registered_methods = 2U,
        .max_methods_per_registration = 2U,
        .max_member_ids = 4U,
        .max_reference_handles = 4U,
        .max_reference_count_per_handle = 4U,
        .max_arrays = 2U,
        .max_long_array_elements = 4U,
        .max_object_array_elements = 4U,
        .max_strings = 2U,
        .max_modified_utf8_bytes = 8U,
        .max_class_name_bytes = 32U,
        .max_method_name_bytes = 32U,
        .max_signature_bytes = 32U,
    };
    A32JniClassRegistry registry{limits};
    constexpr std::uint32_t kClass = 0x44550000U;
    constexpr std::uint32_t kObject = 0x44560000U;
    constexpr std::uint32_t kDeadObject = 0x44560004U;
    constexpr std::uint32_t kLongField = 0x44551000U;
    constexpr std::uint32_t kUnsetLongField = 0x44551004U;
    constexpr std::uint32_t kStaticField = 0x44551008U;
    constexpr std::int64_t kSeeded =
        static_cast<std::int64_t>(
            UINT64_C(0xfefdfcfbfaf9f8f8));
    if (registry.add_class(kClass, "org/videolan/Fixture") !=
            A32JniRegistryError::None ||
        registry.add_reference_identity(kObject) !=
            A32JniRegistryError::None ||
        registry.add_reference_identity(kDeadObject) !=
            A32JniRegistryError::None ||
        !registry.retain_local_reference(kObject) ||
        registry.add_member(
            kClass,
            A32JniMemberKind::InstanceField,
            kLongField,
            "value",
            "J") != A32JniRegistryError::None ||
        registry.add_member(
            kClass,
            A32JniMemberKind::InstanceField,
            kUnsetLongField,
            "unset",
            "J") != A32JniRegistryError::None ||
        registry.add_member(
            kClass,
            A32JniMemberKind::StaticField,
            kStaticField,
            "staticValue",
            "J") != A32JniRegistryError::None ||
        registry.set_instance_long_field_value(
            kObject,
            kLongField,
            kSeeded) != A32JniRegistryError::None ||
        registry.set_instance_long_field_value(
            kObject,
            kStaticField,
            1) != A32JniRegistryError::InvalidMemberKind ||
        registry.set_instance_long_field_value(
            0x99887766U,
            kLongField,
            1) != A32JniRegistryError::InvalidReferenceHandle ||
        registry.set_instance_long_field_value(
            kObject,
            0x99887766U,
            1) != A32JniRegistryError::InvalidMemberHandle) {
        return fail("could not seed bounded JNI instance-long field state");
    }

    const auto configured = layout();
    A32JniVmService service{configured, &registry};
    if (!service.install(memory)) {
        return fail("JNI instance-long field service did not install");
    }

    std::uint32_t cpsr{};
    std::array<std::uint32_t, 16> regs{};
    regs[0] = configured.jni_env_address;
    regs[1] = kObject;
    regs[2] = kLongField;
    if (service.handle(
            memory,
            kA32JniGetLongFieldSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled) {
        return fail("JNI GetLongField rejected seeded value");
    }
    const std::uint64_t seeded_bits =
        static_cast<std::uint64_t>(kSeeded);
    if (regs[0] != static_cast<std::uint32_t>(seeded_bits) ||
        regs[1] != static_cast<std::uint32_t>(seeded_bits >> 32U)) {
        return fail("JNI GetLongField returned wrong r0/r1 jlong bits");
    }

    constexpr std::uint32_t kStack = 0x17e0U;
    constexpr std::uint64_t kUpdated =
        UINT64_C(0x1122334455667788);
    if (!write_u32(
            memory,
            kStack,
            static_cast<std::uint32_t>(kUpdated)) ||
        !write_u32(
            memory,
            kStack + 4U,
            static_cast<std::uint32_t>(kUpdated >> 32U))) {
        return fail("could not stage JNI SetLongField stack value");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kObject;
    regs[2] = kLongField;
    regs[13] = kStack;
    if (service.handle(
            memory,
            kA32JniSetLongFieldSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        registry.instance_long_field_value(
            kObject,
            kLongField).value_or(0) !=
            static_cast<std::int64_t>(kUpdated)) {
        return fail("JNI SetLongField decoded wrong aligned stack jlong");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kObject;
    regs[2] = kLongField;
    if (service.handle(
            memory,
            kA32JniGetLongFieldSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != static_cast<std::uint32_t>(kUpdated) ||
        regs[1] != static_cast<std::uint32_t>(kUpdated >> 32U)) {
        return fail("JNI GetLongField missed SetLongField update");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kObject;
    regs[2] = kUnsetLongField;
    if (service.handle(
            memory,
            kA32JniGetLongFieldSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI GetLongField fabricated missing field state");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kDeadObject;
    regs[2] = kLongField;
    if (service.handle(
            memory,
            kA32JniGetLongFieldSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI GetLongField accepted dead jobject");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kObject;
    regs[2] = kStaticField;
    if (service.handle(
            memory,
            kA32JniGetLongFieldSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI GetLongField accepted static field ID");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kObject;
    regs[2] = kLongField;
    regs[13] = 0x1ffcU;
    if (service.handle(
            memory,
            kA32JniSetLongFieldSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI SetLongField accepted unreadable high stack word");
    }

    return 0;
}

int test_throw_new_pending_exception() {
    LinearGuestMemory memory{0x1000U, 0x1000U};
    const A32JniRegistryLimits limits{
        .max_classes = 2U,
        .max_registered_methods = 2U,
        .max_methods_per_registration = 2U,
        .max_member_ids = 2U,
        .max_reference_handles = 4U,
        .max_reference_count_per_handle = 4U,
        .max_arrays = 2U,
        .max_long_array_elements = 4U,
        .max_object_array_elements = 4U,
        .max_strings = 2U,
        .max_modified_utf8_bytes = 8U,
        .max_exception_message_bytes = 8U,
        .max_class_name_bytes = 64U,
        .max_method_name_bytes = 32U,
        .max_signature_bytes = 32U,
    };
    A32JniClassRegistry registry{limits};
    constexpr std::uint32_t kClass = 0x44550000U;
    constexpr std::uint32_t kDeadClass = 0x44550004U;
    if (registry.add_class(
            kClass,
            "java/lang/IllegalStateException") !=
            A32JniRegistryError::None ||
        registry.add_class(
            kDeadClass,
            "java/lang/IllegalArgumentException") !=
            A32JniRegistryError::None ||
        !registry.retain_local_reference(kClass)) {
        return fail("could not seed JNI ThrowNew classes");
    }

    const auto configured = layout();
    A32JniVmService service{configured, &registry};
    if (!service.install(memory)) {
        return fail("JNI ThrowNew service did not install");
    }

    constexpr std::uint32_t kBoom = 0x1600U;
    constexpr std::uint32_t kLater = 0x1620U;
    constexpr std::uint32_t kTooLong = 0x1640U;
    if (!write_c_string(memory, kBoom, "boom") ||
        !write_c_string(memory, kLater, "later") ||
        !write_c_string(memory, kTooLong, "123456789")) {
        return fail("could not stage JNI ThrowNew messages");
    }

    std::uint32_t cpsr{};
    std::array<std::uint32_t, 16> regs{};
    regs[0] = configured.jni_env_address;
    regs[1] = kClass;
    regs[2] = kBoom;
    if (service.handle(
            memory,
            kA32JniThrowNewSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != static_cast<std::uint32_t>(kA32JniOk)) {
        return fail("JNI ThrowNew rejected first pending exception");
    }
    const auto* pending = registry.pending_exception();
    if (pending == nullptr ||
        pending->class_handle != kClass ||
        pending->class_name != "java/lang/IllegalStateException" ||
        pending->message != "boom") {
        return fail("JNI ThrowNew stored wrong pending exception");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kClass;
    regs[2] = kLater;
    if (service.handle(
            memory,
            kA32JniThrowNewSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != static_cast<std::uint32_t>(kA32JniErr) ||
        registry.pending_exception() == nullptr ||
        registry.pending_exception()->message != "boom") {
        return fail("JNI ThrowNew overwrote existing pending exception");
    }

    registry.clear_pending_exception();
    if (registry.pending_exception() != nullptr) {
        return fail("JNI pending-exception host clear failed");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kDeadClass;
    regs[2] = kBoom;
    if (service.handle(
            memory,
            kA32JniThrowNewSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != static_cast<std::uint32_t>(kA32JniErr) ||
        registry.pending_exception() != nullptr) {
        return fail("JNI ThrowNew accepted dead jclass reference");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kClass;
    regs[2] = kTooLong;
    if (service.handle(
            memory,
            kA32JniThrowNewSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed ||
        registry.pending_exception() != nullptr) {
        return fail("JNI ThrowNew accepted over-limit message");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kClass;
    regs[2] = 0x3000U;
    if (service.handle(
            memory,
            kA32JniThrowNewSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed ||
        registry.pending_exception() != nullptr) {
        return fail("JNI ThrowNew accepted unreadable message");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kClass;
    regs[2] = kLater;
    if (service.handle(
            memory,
            kA32JniThrowNewSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != static_cast<std::uint32_t>(kA32JniOk) ||
        registry.pending_exception() == nullptr ||
        registry.pending_exception()->message != "later") {
        return fail("JNI ThrowNew did not work after host clear");
    }
    registry.clear_pending_exception();

    return 0;
}

int test_call_void_method_v_bridge() {
    LinearGuestMemory memory{0x2000U, 0x1000U};
    const A32JniRegistryLimits limits{
        .max_classes = 2U,
        .max_registered_methods = 2U,
        .max_methods_per_registration = 2U,
        .max_member_ids = 6U,
        .max_reference_handles = 6U,
        .max_reference_count_per_handle = 4U,
        .max_arrays = 2U,
        .max_long_array_elements = 4U,
        .max_object_array_elements = 4U,
        .max_strings = 2U,
        .max_modified_utf8_bytes = 8U,
        .max_exception_message_bytes = 8U,
        .max_class_name_bytes = 64U,
        .max_method_name_bytes = 32U,
        .max_signature_bytes = 64U,
        .max_method_arguments = 10U,
    };
    A32JniClassRegistry registry{limits};
    constexpr std::uint32_t kClass = 0x44550000U;
    constexpr std::uint32_t kObject = 0x44560000U;
    constexpr std::uint32_t kDeadObject = 0x44560004U;
    constexpr std::uint32_t kString = 0x44560008U;
    constexpr std::uint32_t kArray = 0x4456000cU;
    constexpr std::uint32_t kMixedMethod = 0x44551000U;
    constexpr std::uint32_t kNoArgsMethod = 0x44551004U;
    constexpr std::uint32_t kWrongReturnMethod = 0x44551008U;
    constexpr std::uint32_t kMalformedMethod = 0x4455100cU;
    constexpr std::uint32_t kTooManyMethod = 0x44551010U;
    constexpr std::uint32_t kField = 0x44551014U;
    constexpr std::string_view kMixedSignature =
        "(IZBCSJFDLjava/lang/String;[J)V";

    if (registry.add_class(
            kClass,
            "org/videolan/Fixture") !=
            A32JniRegistryError::None ||
        registry.add_reference_identity(kObject) !=
            A32JniRegistryError::None ||
        registry.add_reference_identity(kDeadObject) !=
            A32JniRegistryError::None ||
        registry.add_reference_identity(kString) !=
            A32JniRegistryError::None ||
        registry.add_reference_identity(kArray) !=
            A32JniRegistryError::None ||
        !registry.retain_local_reference(kObject) ||
        !registry.retain_local_reference(kString) ||
        !registry.retain_local_reference(kArray) ||
        registry.add_member(
            kClass,
            A32JniMemberKind::InstanceMethod,
            kMixedMethod,
            "mixed",
            kMixedSignature) != A32JniRegistryError::None ||
        registry.add_member(
            kClass,
            A32JniMemberKind::InstanceMethod,
            kNoArgsMethod,
            "noArgs",
            "()V") != A32JniRegistryError::None ||
        registry.add_member(
            kClass,
            A32JniMemberKind::InstanceMethod,
            kWrongReturnMethod,
            "wrongReturn",
            "()I") != A32JniRegistryError::None ||
        registry.add_member(
            kClass,
            A32JniMemberKind::InstanceMethod,
            kMalformedMethod,
            "malformed",
            "(I)Vx") != A32JniRegistryError::None ||
        registry.add_member(
            kClass,
            A32JniMemberKind::InstanceMethod,
            kTooManyMethod,
            "tooMany",
            "(IIIIIIIIIII)V") != A32JniRegistryError::None ||
        registry.add_member(
            kClass,
            A32JniMemberKind::InstanceField,
            kField,
            "field",
            "J") != A32JniRegistryError::None) {
        return fail("could not seed JNI CallVoidMethodV identities");
    }

    constexpr std::uint32_t kVa = 0x1604U;
    if (!write_u32(memory, kVa + 0x00U, 0xfffffffbU) ||
        !write_u32(memory, kVa + 0x04U, 1U) ||
        !write_u32(memory, kVa + 0x08U, 0xffffff80U) ||
        !write_u32(memory, kVa + 0x0cU, 0x000000e9U) ||
        !write_u32(memory, kVa + 0x10U, 0xffff8001U) ||
        !write_u32(memory, kVa + 0x14U, 0x89abcdefU) ||
        !write_u32(memory, kVa + 0x18U, 0x01234567U) ||
        !write_u32(memory, kVa + 0x1cU, 0x00000000U) ||
        !write_u32(memory, kVa + 0x20U, 0x3ff80000U) ||
        !write_u32(memory, kVa + 0x24U, 0x00000000U) ||
        !write_u32(memory, kVa + 0x28U, 0xc0020000U) ||
        !write_u32(memory, kVa + 0x2cU, kString) ||
        !write_u32(memory, kVa + 0x30U, kArray)) {
        return fail("could not stage JNI CallVoidMethodV va_list");
    }

    const auto configured = layout();
    RecordingMethodCallBridge bridge;
    A32JniVmService service{configured, &registry, &bridge};
    if (!service.install(memory)) {
        return fail("JNI CallVoidMethodV service did not install");
    }

    std::uint32_t cpsr{};
    std::array<std::uint32_t, 16> regs{};
    regs[0] = configured.jni_env_address;
    regs[1] = kObject;
    regs[2] = kMixedMethod;
    regs[3] = kVa;
    if (service.handle(
            memory,
            kA32JniCallVoidMethodVSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        bridge.calls != 1U ||
        bridge.receiver != kObject ||
        bridge.method_handle != kMixedMethod ||
        bridge.signature != kMixedSignature ||
        bridge.arguments.size() != 10U ||
        bridge.arguments[0].kind != A32JniValueKind::Int ||
        bridge.arguments[0].bits != 0xfffffffbU ||
        bridge.arguments[1].kind != A32JniValueKind::Boolean ||
        bridge.arguments[1].bits != 1U ||
        bridge.arguments[2].kind != A32JniValueKind::Byte ||
        bridge.arguments[2].bits != 0xffffff80U ||
        bridge.arguments[3].kind != A32JniValueKind::Char ||
        bridge.arguments[3].bits != 0x000000e9U ||
        bridge.arguments[4].kind != A32JniValueKind::Short ||
        bridge.arguments[4].bits != 0xffff8001U ||
        bridge.arguments[5].kind != A32JniValueKind::Long ||
        bridge.arguments[5].bits != UINT64_C(0x0123456789abcdef) ||
        bridge.arguments[6].kind != A32JniValueKind::Float ||
        bridge.arguments[6].bits != 0x3fc00000U ||
        bridge.arguments[7].kind != A32JniValueKind::Double ||
        bridge.arguments[7].bits != UINT64_C(0xc002000000000000) ||
        bridge.arguments[8].kind != A32JniValueKind::Reference ||
        bridge.arguments[8].bits != kString ||
        bridge.arguments[9].kind != A32JniValueKind::Reference ||
        bridge.arguments[9].bits != kArray) {
        return fail("JNI CallVoidMethodV decoded mixed arguments incorrectly");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kObject;
    regs[2] = kNoArgsMethod;
    regs[3] = 0U;
    if (service.handle(
            memory,
            kA32JniCallVoidMethodVSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        bridge.calls != 2U ||
        !bridge.arguments.empty()) {
        return fail("JNI CallVoidMethodV rejected zero-argument method");
    }

    const std::size_t calls_after_success = bridge.calls;
    regs = {};
    regs[0] = configured.jni_env_address + 4U;
    regs[1] = kObject;
    regs[2] = kNoArgsMethod;
    if (service.handle(
            memory,
            kA32JniCallVoidMethodVSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed ||
        bridge.calls != calls_after_success) {
        return fail("JNI CallVoidMethodV accepted wrong JNIEnv pointer");
    }

    const std::size_t calls_before_failures = bridge.calls;
    const std::array<std::uint32_t, 4> rejected_methods{{
        kField,
        kWrongReturnMethod,
        kMalformedMethod,
        kTooManyMethod,
    }};
    for (const std::uint32_t method : rejected_methods) {
        regs = {};
        regs[0] = configured.jni_env_address;
        regs[1] = kObject;
        regs[2] = method;
        regs[3] = kVa;
        if (service.handle(
                memory,
                kA32JniCallVoidMethodVSvcImmediate,
                regs,
                cpsr) != A32HostServiceDisposition::Failed) {
            return fail("JNI CallVoidMethodV accepted invalid method metadata");
        }
    }
    if (bridge.calls != calls_before_failures) {
        return fail("JNI CallVoidMethodV invoked bridge after metadata failure");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kDeadObject;
    regs[2] = kNoArgsMethod;
    if (service.handle(
            memory,
            kA32JniCallVoidMethodVSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI CallVoidMethodV accepted dead receiver");
    }

    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kObject;
    regs[2] = kMixedMethod;
    regs[3] = 0x2ffcU;
    if (service.handle(
            memory,
            kA32JniCallVoidMethodVSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI CallVoidMethodV accepted unreadable va_list");
    }

    if (!registry.delete_local_reference(kString)) {
        return fail("could not make JNI CallVoidMethodV reference argument dead");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kObject;
    regs[2] = kMixedMethod;
    regs[3] = kVa;
    if (service.handle(
            memory,
            kA32JniCallVoidMethodVSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI CallVoidMethodV accepted dead reference argument");
    }
    if (!registry.retain_local_reference(kString)) {
        return fail("could not restore JNI CallVoidMethodV reference argument");
    }

    bridge.accept_calls = false;
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kObject;
    regs[2] = kNoArgsMethod;
    if (service.handle(
            memory,
            kA32JniCallVoidMethodVSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI CallVoidMethodV hid embedding bridge failure");
    }

    A32JniVmService without_bridge{configured, &registry};
    if (!without_bridge.install(memory)) {
        return fail("JNI CallVoidMethodV no-bridge service did not install");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kObject;
    regs[2] = kNoArgsMethod;
    if (without_bridge.handle(
            memory,
            kA32JniCallVoidMethodVSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("JNI CallVoidMethodV ran without embedding bridge");
    }

    const std::size_t calls_before_detach = bridge.calls;
    regs = {};
    regs[0] = configured.java_vm_address;
    if (service.handle(
            memory,
            kA32JniDetachCurrentThreadSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != static_cast<std::uint32_t>(kA32JniOk)) {
        return fail("could not detach before JNI CallVoidMethodV test");
    }
    regs = {};
    regs[0] = configured.jni_env_address;
    regs[1] = kObject;
    regs[2] = kNoArgsMethod;
    if (service.handle(
            memory,
            kA32JniCallVoidMethodVSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed ||
        bridge.calls != calls_before_detach) {
        return fail("JNI CallVoidMethodV accepted detached context");
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
    if (const int status = test_strong_reference_lifetime();
        status != 0) {
        return status;
    }
    if (const int status = test_seeded_array_length();
        status != 0) {
        return status;
    }
    if (const int status = test_static_int_field_value();
        status != 0) {
        return status;
    }
    if (const int status = test_modified_utf8_strings();
        status != 0) {
        return status;
    }
    if (const int status = test_long_array_family();
        status != 0) {
        return status;
    }
    if (const int status = test_byte_array_elements();
        status != 0) {
        return status;
    }
    if (const int status = test_object_array_family();
        status != 0) {
        return status;
    }
    if (const int status = test_instance_long_field_value();
        status != 0) {
        return status;
    }
    if (const int status = test_throw_new_pending_exception();
        status != 0) {
        return status;
    }
    if (const int status = test_call_void_method_v_bridge();
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
