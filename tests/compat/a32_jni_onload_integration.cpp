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

using liba32android::compat::A32JniOnLoadOptions;
using liba32android::compat::A32JniVmLayout;
using liba32android::compat::A32JniVmService;
using liba32android::compat::kA32JniGetEnvSvcImmediate;
using liba32android::compat::kA32JniVersion16;
using liba32android::compat::invoke_a32_jni_on_load;
using liba32android::elf::Elf32DependencyLoadOptions;
using liba32android::elf::Elf32DependencyLoadSource;
using liba32android::elf::Elf32DependencyProvider;
using liba32android::elf::Elf32DependencyProviderError;
using liba32android::elf::Elf32DependencyProviderResult;
using liba32android::elf::Elf32SymbolLookupOptions;
using liba32android::elf::load_elf32_dependency_graph;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;
using liba32android::runtime::A32HostServiceRegistry;
using liba32android::runtime::A32HostServiceRegistryEntry;

constexpr std::uint64_t kMaxFixtureImageBytes = 4U << 20;
constexpr std::size_t kStackPages = 4U;

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

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
        .get_env_stub_address = *stub_page,
    };
    A32JniVmService vm{vm_layout};
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

    const std::array<A32HostServiceRegistryEntry, 1> services{{
        {kA32JniGetEnvSvcImmediate, &vm},
    }};
    A32HostServiceRegistry registry{std::span{services}};

    const std::uint64_t stack_top64 =
        static_cast<std::uint64_t>(*stack) +
        memory.page_size() * kStackPages - 16U;
    if (stack_top64 >
        std::numeric_limits<std::uint32_t>::max()) {
        return fail("JNI integration stack top overflowed");
    }

    const A32JniOnLoadOptions options{
        .stack_top =
            static_cast<std::uint32_t>(stack_top64) & ~7U,
        .return_pc = *stop,
        .max_instructions = 256U,
        .max_service_calls = 1U,
        .symbols = symbol_options(),
    };
    const auto result = invoke_a32_jni_on_load(
        memory,
        loaded.graph,
        0U,
        vm_layout.java_vm_address,
        registry,
        options);
    if (!result ||
        result.returned_version != kA32JniVersion16 ||
        !result.execution.has_value() ||
        !result.execution->stop_pc_reached ||
        result.execution->services_handled != 1U ||
        result.execution->regs[1] != 0U) {
        return fail(
            std::string("ARM32 JNI_OnLoad/GetEnv execution failed: ") +
            liba32android::compat::to_string(result.error));
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
        << "fixture.jni.status=PASS\n";
    return 0;
}
