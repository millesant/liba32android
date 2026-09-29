#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/fixture_io.h"

#include "compat/a32_jni.h"
#include "elf/elf32_dependency_loader.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_registry.h"

namespace {

using liba32android::compat::A32JniClassRegistry;
using liba32android::compat::A32JniMemberKind;
using liba32android::compat::A32JniNativeInvokeOptions;
using liba32android::compat::A32JniOnLoadOptions;
using liba32android::compat::A32JniRegistryError;
using liba32android::compat::A32JniVmLayout;
using liba32android::compat::A32JniVmService;
using liba32android::compat::kA32JniVersion16;
using liba32android::compat::invoke_a32_jni_on_load;
using liba32android::compat::invoke_a32_registered_native_noargs;
using liba32android::elf::Elf32DependencyLoadOptions;
using liba32android::elf::Elf32DependencyLoadSource;
using liba32android::elf::Elf32DependencyProvider;
using liba32android::elf::Elf32DependencyProviderError;
using liba32android::elf::Elf32DependencyProviderResult;
using liba32android::elf::Elf32LifecycleExecutionContext;
using liba32android::elf::Elf32SymbolLookupOptions;
using liba32android::elf::load_elf32_dependency_graph;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;
using liba32android::runtime::A32HostServiceDisposition;
using liba32android::runtime::A32HostServiceHandler;

constexpr std::uint64_t kMaxFixtureImageBytes = 4U << 20;
constexpr std::size_t kStackPages = 4U;

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

class ContextCheckingJniHandler final
    : public A32HostServiceHandler {
public:
    ContextCheckingJniHandler(
        A32JniVmService& vm,
        Elf32LifecycleExecutionContext& context,
        std::size_t expected_object) noexcept
        : vm_(vm),
          context_(context),
          expected_object_(expected_object) {}

    A32HostServiceDisposition handle(
        liba32android::memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override {
        if (!context_.object_index.has_value() ||
            *context_.object_index != expected_object_) {
            return A32HostServiceDisposition::Failed;
        }
        saw_expected_context_ = true;
        return vm_.handle(
            memory, svc_immediate, regs, cpsr);
    }

    [[nodiscard]] bool saw_expected_context() const noexcept {
        return saw_expected_context_;
    }

private:
    A32JniVmService& vm_;
    Elf32LifecycleExecutionContext& context_;
    std::size_t expected_object_{};
    bool saw_expected_context_{};
};

class FailIfCalledProvider final : public Elf32DependencyProvider {
public:
    std::size_t calls{};

    Elf32DependencyProviderResult resolve(
        std::string_view,
        std::uint64_t) override {
        ++calls;
        Elf32DependencyProviderResult result;
        result.error = Elf32DependencyProviderError::Failed;
        return result;
    }
};

std::optional<std::uint32_t> find_unmapped_region(
    const MappedGuestMemory& memory,
    std::uint32_t start,
    std::size_t page_count) {
    const std::uint64_t page_size = memory.page_size();
    const std::uint64_t length = page_size * page_count;
    if (page_count == 0U ||
        length > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }

    for (std::uint64_t candidate = start;
         candidate + length <= 0xf0000000ULL;
         candidate += page_size * 16U) {
        bool available = true;
        for (std::size_t index = 0U;
             index < page_count;
             ++index) {
            const std::uint64_t page =
                candidate + index * page_size;
            if (page >
                    std::numeric_limits<std::uint32_t>::max() ||
                memory.is_mapped(
                    static_cast<std::uint32_t>(page))) {
                available = false;
                break;
            }
        }
        if (available) {
            return static_cast<std::uint32_t>(candidate);
        }
    }
    return std::nullopt;
}

Elf32SymbolLookupOptions symbol_options() {
    return Elf32SymbolLookupOptions{
        .max_symbols = 64U,
        .max_hash_buckets = 64U,
        .max_gnu_bloom_words = 32U,
        .max_scope_objects = 4U,
        .max_name_bytes = 128U,
        .max_version_records = 32U,
    };
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return fail("expected generated ARM32 JNI_OnLoad fixture path");
    }

    const std::vector<std::uint8_t> image =
        liba32android::test_support::read_binary_file(argv[1]);
    if (image.empty()) {
        return fail("generated ARM32 JNI_OnLoad fixture is missing or empty");
    }

    MappedGuestMemory memory;
    FailIfCalledProvider provider;

    Elf32DependencyLoadOptions load_options;
    load_options.max_objects = 2U;
    load_options.max_depth = 2U;
    load_options.max_dependency_occurrences = 2U;
    load_options.max_image_bytes = kMaxFixtureImageBytes;
    load_options.max_total_image_bytes = kMaxFixtureImageBytes;
    load_options.max_string_bytes = 128U;

    const auto loaded = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "jni-onload-fixture",
            .image = image,
        },
        provider,
        load_options);
    if (!loaded ||
        loaded.graph.objects.size() != 1U ||
        provider.calls != 0U) {
        return fail(
            std::string("JNI fixture dependency load failed: ") +
            liba32android::elf::to_string(loaded.error));
    }

    const auto data_page =
        find_unmapped_region(memory, 0x70000000U, 1U);
    const auto stub_page =
        find_unmapped_region(memory, 0x71000000U, 1U);
    const auto stack =
        find_unmapped_region(memory, 0x72000000U, kStackPages);
    const auto stop =
        find_unmapped_region(memory, 0x73000000U, 1U);
    if (!data_page.has_value() ||
        !stub_page.has_value() ||
        !stack.has_value() ||
        !stop.has_value()) {
        return fail("could not reserve JNI integration guest regions");
    }

    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    const auto rx = MemoryPermission::Read | MemoryPermission::Execute;
    if (!memory.map(*data_page, memory.page_size(), rw) ||
        !memory.map(*stub_page, memory.page_size(), rw) ||
        !memory.map(
            *stack,
            memory.page_size() * kStackPages,
            rw)) {
        return fail("could not map JNI integration guest regions");
    }

    const A32JniVmLayout vm_layout{
        .java_vm_address = *data_page + 0x00U,
        .invoke_table_address = *data_page + 0x20U,
        .jni_env_address = *data_page + 0x60U,
        .native_table_address = *data_page + 0x80U,
        .get_env_stub_address = *stub_page + 0x00U,
        .find_class_stub_address = *stub_page + 0x20U,
        .register_natives_stub_address = *stub_page + 0x40U,
        .get_method_id_stub_address = *stub_page + 0x60U,
        .get_field_id_stub_address = *stub_page + 0x80U,
        .get_static_field_id_stub_address = *stub_page + 0xa0U,
        .attach_current_thread_stub_address = *stub_page + 0xc0U,
        .detach_current_thread_stub_address = *stub_page + 0xe0U,
        .new_global_ref_stub_address = *stub_page + 0x100U,
        .delete_global_ref_stub_address = *stub_page + 0x120U,
        .delete_local_ref_stub_address = *stub_page + 0x140U,
        .get_array_length_stub_address = *stub_page + 0x160U,
        .get_static_int_field_stub_address = *stub_page + 0x180U,
        .new_string_utf_stub_address = *stub_page + 0x1a0U,
        .get_string_utf_chars_stub_address = *stub_page + 0x1c0U,
        .release_string_utf_chars_stub_address = *stub_page + 0x1e0U,
        .string_utf_scratch_address = *data_page + 0x800U,
        .string_utf_scratch_bytes = 128U,
        .new_long_array_stub_address = *stub_page + 0x200U,
        .get_long_array_elements_stub_address = *stub_page + 0x220U,
        .release_long_array_elements_stub_address = *stub_page + 0x240U,
        .set_long_array_region_stub_address = *stub_page + 0x260U,
        .long_array_scratch_address = *data_page + 0x900U,
        .long_array_scratch_bytes = 128U,
    };
    constexpr std::uint32_t kFixtureClassHandle = 0x44550000U;
    constexpr std::uint32_t kFixtureStaticFieldHandle = 0x44551000U;
    constexpr std::uint32_t kFixtureArrayHandle = 0x44560000U;
    A32JniClassRegistry registry;
    if (!registry.valid() ||
        registry.add_class(
            kFixtureClassHandle,
            "org/videolan/Fixture") !=
            A32JniRegistryError::None ||
        registry.add_member(
            kFixtureClassHandle,
            A32JniMemberKind::StaticField,
            kFixtureStaticFieldHandle,
            "answer",
            "I") != A32JniRegistryError::None ||
        registry.set_static_int_field_value(
            kFixtureStaticFieldHandle,
            42) != A32JniRegistryError::None ||
        registry.add_array(kFixtureArrayHandle, 7U) !=
            A32JniRegistryError::None) {
        return fail("could not seed JNI fixture class registry");
    }

    A32JniVmService vm{vm_layout, &registry};
    const auto installed = vm.install(memory);
    if (!installed ||
        !memory.protect(
            *stub_page,
            memory.page_size(),
            rx)) {
        return fail(
            std::string("could not install/seal JNI VM: ") +
            liba32android::compat::to_string(
                installed.error));
    }

    Elf32LifecycleExecutionContext execution_context;
    ContextCheckingJniHandler handler{
        vm, execution_context, 0U};

    const std::uint64_t stack_top64 =
        static_cast<std::uint64_t>(*stack) +
        memory.page_size() * kStackPages - 16U;
    if (stack_top64 >
        std::numeric_limits<std::uint32_t>::max()) {
        return fail("JNI integration stack top overflowed");
    }

    const std::uint32_t stack_top =
        static_cast<std::uint32_t>(stack_top64) & ~7U;
    const A32JniOnLoadOptions options{
        .stack_top = stack_top,
        .return_pc = *stop,
        .max_instructions = 1024U,
        .max_service_calls = 20U,
        .symbols = symbol_options(),
        .execution_context = &execution_context,
    };
    const auto result = invoke_a32_jni_on_load(
        memory,
        loaded.graph,
        0U,
        vm_layout.java_vm_address,
        handler,
        options);
    if (!result ||
        result.returned_version != kA32JniVersion16 ||
        !result.execution.has_value() ||
        !result.execution->stop_pc_reached ||
        result.execution->services_handled != 20U ||
        !handler.saw_expected_context() ||
        execution_context.object_index.has_value()) {
        const std::uint32_t failing_svc =
            result.failing_svc_immediate.value_or(0U);
        const std::size_t handled_services =
            result.execution.has_value()
                ? result.execution->services_handled
                : 0U;
        return fail(
            std::string("ARM32 JNI registration execution failed: ") +
            liba32android::compat::to_string(result.error) +
            " svc=" + std::to_string(failing_svc) +
            " handled=" +
            std::to_string(handled_services));
    }

    const auto array_length =
        registry.array_length(kFixtureArrayHandle);
    if (!array_length.has_value() || *array_length != 7U) {
        return fail("ARM32 JNI_OnLoad lost seeded array metadata");
    }

    const auto static_int =
        registry.static_int_field_value(
            kFixtureStaticFieldHandle);
    if (!static_int.has_value() || *static_int != 42) {
        return fail("ARM32 JNI_OnLoad lost seeded static-int value");
    }

    if (registry.string_count() != 1U ||
        vm.utf_chars_lease_active() ||
        vm.long_array_lease_active()) {
        return fail("ARM32 JNI_OnLoad left a JNI copy lease active");
    }

    constexpr std::uint32_t kDynamicLongArrayHandle = 0x75000000U;
    const auto* long_array =
        registry.find_long_array(kDynamicLongArrayHandle);
    const auto long_reference_counts =
        registry.reference_counts(kDynamicLongArrayHandle);
    if (long_array == nullptr ||
        long_array->elements !=
            std::vector<std::int64_t>({1, 7, 42}) ||
        !long_reference_counts.has_value() ||
        long_reference_counts->local != 0U ||
        long_reference_counts->global != 0U) {
        return fail("ARM32 JNI_OnLoad lost jlong-array state/lifetime");
    }

    const auto reference_counts =
        registry.reference_counts(kFixtureClassHandle);
    if (!reference_counts.has_value() ||
        reference_counts->local != 0U ||
        reference_counts->global != 0U) {
        return fail("ARM32 JNI_OnLoad did not release fixture references");
    }

    const auto* registered = registry.find_native(
        kFixtureClassHandle,
        "nativePing",
        "()I");
    if (registered == nullptr ||
        registered->class_name != "org/videolan/Fixture" ||
        registered->function == 0U) {
        return fail("ARM32 JNI_OnLoad did not retain native registration");
    }

    const A32JniNativeInvokeOptions native_options{
        .stack_top = stack_top,
        .return_pc = *stop,
        .max_instructions = 64U,
        .max_service_calls = 1U,
    };
    const auto native_result =
        invoke_a32_registered_native_noargs(
            memory,
            registry,
            kFixtureClassHandle,
            "nativePing",
            "()I",
            vm_layout.jni_env_address,
            kFixtureClassHandle,
            vm,
            native_options);
    if (!native_result ||
        native_result.function != registered->function ||
        native_result.returned_value != 42U ||
        !native_result.execution.has_value() ||
        !native_result.execution->stop_pc_reached ||
        native_result.execution->services_handled != 0U) {
        return fail(
            std::string("registered ARM32 JNI native execution failed: ") +
            liba32android::compat::to_string(
                native_result.error));
    }

    std::cout
        << "fixture.jni.object_count="
        << loaded.graph.objects.size() << '\n'
        << "fixture.jni.vm=0x"
        << std::hex << vm_layout.java_vm_address << '\n'
        << "fixture.jni.env=0x"
        << std::hex << vm_layout.jni_env_address << '\n'
        << "fixture.jni.service_calls="
        << std::dec << result.execution->services_handled << '\n'
        << "fixture.jni.version=0x"
        << std::hex << result.returned_version << '\n'
        << "fixture.jni.registered_function=0x"
        << std::hex << registered->function << '\n'
        << "fixture.jni.native_return="
        << std::dec << native_result.returned_value << '\n'
        << "fixture.jni.local_refs="
        << reference_counts->local << '\n'
        << "fixture.jni.global_refs="
        << reference_counts->global << '\n'
        << "fixture.jni.array_length="
        << *array_length << '\n'
        << "fixture.jni.static_int="
        << *static_int << '\n'
        << "fixture.jni.string_count="
        << registry.string_count() << '\n'
        << "fixture.jni.utf_lease="
        << (vm.utf_chars_lease_active() ? 1 : 0) << '\n'
        << "fixture.jni.long_array="
        << long_array->elements[0] << ","
        << long_array->elements[1] << ","
        << long_array->elements[2] << '\n'
        << "fixture.jni.long_refs="
        << long_reference_counts->local +
               long_reference_counts->global
        << '\n'
        << "fixture.jni.status=PASS\n";
    return 0;
}
