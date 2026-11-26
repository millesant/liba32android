#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "compat/a32_aeabi_atexit.h"
#include "compat/a32_libdl.h"
#include "compat/a32_libdl_open_transaction.h"
#include "elf/elf32_dependency_resolver.h"
#include "elf/elf32_link_map.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"
#include "runtime/a32_service_registry.h"

namespace {

using liba32android::compat::A32AeabiAtexitRecord;
using liba32android::compat::A32AeabiAtexitService;
using liba32android::compat::A32AeabiObjectDsoBinding;
using liba32android::compat::A32LibDlHandle;
using liba32android::compat::kA32AeabiAtexitSvcImmediate;
using liba32android::compat::A32LibDlOpenPolicy;
using liba32android::compat::A32LibDlOpenTransaction;
using liba32android::compat::A32LibDlOpenTransactionError;
using liba32android::compat::A32LibDlOpenTransactionOptions;
using liba32android::compat::A32LibDlOptions;
using liba32android::compat::A32LibDlService;
using liba32android::compat::kA32LibDlDlopenSvcImmediate;
using liba32android::compat::kA32RtldGlobal;
using liba32android::compat::kA32RtldNow;
using liba32android::elf::Elf32DependencyCatalogEntry;
using liba32android::elf::Elf32DependencyCatalogProvider;
using liba32android::elf::Elf32LifecycleExecutionContext;
using liba32android::elf::Elf32LifecycleObjectStatus;
using liba32android::elf::Elf32LifecycleState;
using liba32android::elf::Elf32LinkMap;
using liba32android::elf::Elf32LinkMapObjectState;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;
using liba32android::runtime::A32HostServiceDisposition;
using liba32android::runtime::A32HostServiceRegistry;
using liba32android::runtime::A32HostServiceRegistryEntry;

constexpr std::size_t kHeaderSize = 52U;
constexpr std::size_t kProgramHeaderSize = 32U;
constexpr std::size_t kProgramHeaderOffset = kHeaderSize;
constexpr std::uint32_t kPtLoad = 1U;
constexpr std::uint32_t kPtGnuRelro = 0x6474e552U;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

class CountingNotFoundProvider final
    : public liba32android::elf::Elf32DependencyProvider {
public:
    liba32android::elf::Elf32DependencyProviderResult resolve(
        std::string_view,
        std::uint64_t) override {
        ++calls;
        liba32android::elf::Elf32DependencyProviderResult result;
        result.error =
            liba32android::elf::Elf32DependencyProviderError::NotFound;
        return result;
    }

    std::size_t calls{};
};

void write_u16(
    std::vector<std::uint8_t>& image,
    std::size_t offset,
    std::uint16_t value) {
    image[offset] = static_cast<std::uint8_t>(value);
    image[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void write_u32(
    std::vector<std::uint8_t>& image,
    std::size_t offset,
    std::uint32_t value) {
    image[offset] = static_cast<std::uint8_t>(value);
    image[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    image[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    image[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

std::vector<std::uint8_t> make_dynamic_image(bool with_relro) {
    std::vector<std::uint8_t> image(0x4010U, 0U);
    image[0] = 0x7fU;
    image[1] = 'E';
    image[2] = 'L';
    image[3] = 'F';
    image[4] = 1U;
    image[5] = 1U;
    image[6] = 1U;

    write_u16(image, 16U, 3U);
    write_u16(image, 18U, 40U);
    write_u32(image, 20U, 1U);
    write_u32(image, 24U, 0x80U);
    write_u32(image, 28U, kProgramHeaderOffset);
    write_u16(image, 40U, kHeaderSize);
    write_u16(image, 42U, kProgramHeaderSize);
    write_u16(image, 44U, with_relro ? 3U : 2U);

    const std::size_t first = kProgramHeaderOffset;
    write_u32(image, first + 0U, kPtLoad);
    write_u32(image, first + 4U, 0U);
    write_u32(image, first + 8U, 0U);
    write_u32(image, first + 16U, 0x100U);
    write_u32(image, first + 20U, 0x100U);
    write_u32(image, first + 24U, 5U);
    write_u32(image, first + 28U, 0x4000U);

    const std::size_t second = first + kProgramHeaderSize;
    write_u32(image, second + 0U, kPtLoad);
    write_u32(image, second + 4U, 0x4000U);
    write_u32(image, second + 8U, 0x4000U);
    write_u32(image, second + 16U, 4U);
    write_u32(image, second + 20U, 0x1000U);
    write_u32(image, second + 24U, 6U);
    write_u32(image, second + 28U, 0x4000U);
    image[0x4000U] = 0x78U;
    image[0x4001U] = 0x56U;
    image[0x4002U] = 0x34U;
    image[0x4003U] = 0x12U;

    if (with_relro) {
        const std::size_t third = second + kProgramHeaderSize;
        write_u32(image, third + 0U, kPtGnuRelro);
        write_u32(image, third + 4U, 0x4000U);
        write_u32(image, third + 8U, 0x4000U);
        write_u32(image, third + 16U, 4U);
        write_u32(image, third + 20U, 4U);
        write_u32(image, third + 24U, 4U);
        write_u32(image, third + 28U, 4U);
    }
    return image;
}

A32LibDlOpenTransactionOptions transaction_options() {
    A32LibDlOpenTransactionOptions result;
    result.handle_base = 0x70000000U;
    result.load.max_objects = 8U;
    result.load.max_depth = 8U;
    result.load.max_dependency_occurrences = 32U;
    result.load.max_image_bytes = 1U << 20;
    result.load.max_total_image_bytes = 8U << 20;
    result.load.max_string_bytes = 128U;
    result.load.placement.search_begin = 0x10000U;
    result.load.placement.search_end_exclusive = 0x80000U;

    result.relocation.max_relocations = 32U;
    result.relocation.symbols.max_symbols = 128U;
    result.relocation.symbols.max_hash_buckets = 128U;
    result.relocation.symbols.max_gnu_bloom_words = 64U;
    result.relocation.symbols.max_scope_objects = 8U;
    result.relocation.symbols.max_name_bytes = 128U;
    result.relocation.symbols.max_version_records = 128U;

    result.relro.max_pages = 8U;
    result.lifecycle.max_objects = 8U;
    result.lifecycle.max_array_entries = 8U;
    result.lifecycle.execution.stack_top = 0x8ff8U;
    result.lifecycle.execution.return_pc = 0x9000U;
    result.lifecycle.execution.max_instructions_per_call = 32U;

    result.reclamation.max_objects = 8U;
    result.reclamation.max_segments = 16U;
    result.reclamation.max_snapshot_bytes = 1U << 20;
    return result;
}

A32LibDlOptions service_options() {
    A32LibDlOptions result;
    result.max_name_bytes = 128U;
    result.handle_base = 0x70000000U;
    result.error_buffer_address = 0x90100U;
    result.error_buffer_bytes = 128U;
    result.info_string_buffer_address = 0x90200U;
    result.info_string_buffer_bytes = 512U;
    result.symbols.max_symbols = 128U;
    result.symbols.max_hash_buckets = 128U;
    result.symbols.max_gnu_bloom_words = 64U;
    result.symbols.max_scope_objects = 8U;
    result.symbols.max_name_bytes = 128U;
    result.symbols.max_version_records = 128U;
    return result;
}

bool object_mappings_mapped(
    const MappedGuestMemory& memory,
    const liba32android::elf::Elf32LoadedDependencyObject& object) {
    for (const auto& segment : object.load.segments) {
        if (!memory.is_mapped(segment.mapping_start)) {
            return false;
        }
    }
    return true;
}

bool object_mappings_unmapped(
    const MappedGuestMemory& memory,
    const liba32android::elf::Elf32LoadedDependencyObject& object) {
    for (const auto& segment : object.load.segments) {
        if (memory.is_mapped(segment.mapping_start)) {
            return false;
        }
    }
    return true;
}

int test_service_dynamic_open_and_resident_refcount() {
    MappedGuestMemory memory;
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    const std::uint32_t io_page = 0x90000U;
    if (!memory.map(io_page, memory.page_size(), rw)) {
        return fail("could not map dynamic-open service I/O page");
    }
    constexpr std::array<std::uint8_t, 10> name{{
        'l','i','b','d','y','n','.','s','o',0U,
    }};
    if (!memory.write(io_page, name)) {
        return fail("could not stage dynamic-open guest name");
    }

    const auto image = make_dynamic_image(false);
    const std::array<Elf32DependencyCatalogEntry, 1> entries{{
        {
            .requested_name = "libdyn.so",
            .identity = "dyn-id",
            .image = std::span<const std::uint8_t>{image},
        },
    }};
    Elf32DependencyCatalogProvider provider{std::span{entries}};
    Elf32LinkMap link_map;
    Elf32LifecycleState lifecycle;
    std::array<A32LibDlHandle, 2> handles{};

    A32LibDlOpenTransaction transaction{
        memory,
        link_map,
        provider,
        std::span{handles},
        lifecycle,
        transaction_options(),
    };
    A32LibDlService service{
        link_map,
        std::span{handles},
        service_options(),
        nullptr,
        &transaction,
    };

    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    regs[0] = io_page;
    regs[1] = kA32RtldGlobal;
    regs[13] = 0x8ff8U;
    if (service.handle(
            memory,
            kA32LibDlDlopenSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0x70000000U ||
        link_map.graph.objects.size() != 1U ||
        link_map.roots.size() != 1U ||
        link_map.roots[0].object_index != 0U ||
        link_map.roots[0].policy !=
            liba32android::elf::Elf32LinkMapRootPolicy::Global ||
        link_map.global_scope_objects !=
            std::vector<std::size_t>{0U} ||
        link_map.object_states !=
            std::vector<Elf32LinkMapObjectState>{
                Elf32LinkMapObjectState::Active} ||
        lifecycle.objects.size() != 1U ||
        lifecycle.objects[0].constructors !=
            Elf32LifecycleObjectStatus::Complete ||
        lifecycle.objects[0].destructors !=
            Elf32LifecycleObjectStatus::Pending ||
        handles[0].object_index != 0U ||
        handles[0].refcount != 1U ||
        !object_mappings_mapped(memory, link_map.graph.objects[0])) {
        return fail("dynamic named dlopen did not initialize and publish handle");
    }

    regs = {};
    regs[0] = io_page;
    regs[1] = kA32RtldNow;
    regs[13] = 0x8ff8U;
    if (service.handle(
            memory,
            kA32LibDlDlopenSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0x70000000U ||
        handles[0].refcount != 2U ||
        link_map.graph.objects.size() != 1U ||
        link_map.roots[0].policy !=
            liba32android::elf::Elf32LinkMapRootPolicy::Global ||
        link_map.global_scope_objects !=
            std::vector<std::size_t>{0U} ||
        lifecycle.objects[0].constructors !=
            Elf32LifecycleObjectStatus::Complete) {
        return fail("repeated dynamic dlopen did not reuse resident handle");
    }

    lifecycle.objects[0].destructors =
        Elf32LifecycleObjectStatus::Complete;
    const auto rejected = transaction.open("dyn-id", 0x8ff8U);
    if (rejected.error !=
            A32LibDlOpenTransactionError::InvalidResidentLifecycle ||
        handles[0].refcount != 2U) {
        return fail("dynamic open accepted already-destructed resident object");
    }
    return 0;
}

int test_preconstructor_failure_reclaims_added_root() {
    MappedGuestMemory memory;
    const auto image = make_dynamic_image(true);
    const std::array<Elf32DependencyCatalogEntry, 1> entries{{
        {
            .requested_name = "librelro.so",
            .identity = "relro-id",
            .image = std::span<const std::uint8_t>{image},
        },
    }};
    Elf32DependencyCatalogProvider provider{std::span{entries}};
    Elf32LinkMap link_map;
    Elf32LifecycleState lifecycle;
    std::array<A32LibDlHandle, 1> handles{};

    auto broken_options = transaction_options();
    broken_options.relro.max_pages = 0U;
    A32LibDlOpenTransaction broken{
        memory,
        link_map,
        provider,
        std::span{handles},
        lifecycle,
        broken_options,
    };
    const auto failed = broken.open("librelro.so", 0x8ff8U);
    if (failed.error != A32LibDlOpenTransactionError::RelroFailed ||
        !failed.root_added ||
        failed.objects_appended != 1U ||
        !link_map.roots.empty() ||
        link_map.graph.objects.size() != 1U ||
        link_map.object_states.size() != 1U ||
        link_map.object_states[0] != Elf32LinkMapObjectState::Retired ||
        lifecycle.objects.size() != 1U ||
        lifecycle.objects[0].constructors !=
            Elf32LifecycleObjectStatus::Pending ||
        handles[0].refcount != 0U ||
        !object_mappings_unmapped(memory, link_map.graph.objects[0])) {
        return fail("pre-constructor failure did not reclaim added dynamic root");
    }

    A32LibDlOpenTransaction retry{
        memory,
        link_map,
        provider,
        std::span{handles},
        lifecycle,
        transaction_options(),
    };
    const auto opened = retry.open("librelro.so", 0x8ff8U);
    if (!opened ||
        opened.guest_handle != 0x70000000U ||
        opened.object_index != std::optional<std::size_t>{1U} ||
        link_map.graph.objects.size() != 2U ||
        link_map.object_states !=
            std::vector<Elf32LinkMapObjectState>{
                Elf32LinkMapObjectState::Retired,
                Elf32LinkMapObjectState::Active} ||
        lifecycle.objects.size() != 2U ||
        lifecycle.objects[1].constructors !=
            Elf32LifecycleObjectStatus::Complete ||
        !object_mappings_mapped(memory, link_map.graph.objects[1])) {
        return fail("dynamic-open retry did not allocate fresh active slot");
    }
    return 0;
}

int test_open_constructor_learns_dso_binding() {
    MappedGuestMemory memory;
    const auto rwx =
        MemoryPermission::Read |
        MemoryPermission::Write |
        MemoryPermission::Execute;
    const std::uint32_t code_page = 0x10000U;
    if (!memory.map(code_page, memory.page_size(), rwx)) {
        return fail("could not map DSO-binding constructor page");
    }

    constexpr std::array<std::uint8_t, 32> code{
        0x0C, 0x00, 0x9F, 0xE5,  // ldr r0, [pc, #12]
        0x0C, 0x10, 0x9F, 0xE5,  // ldr r1, [pc, #12]
        0x0C, 0x20, 0x9F, 0xE5,  // ldr r2, [pc, #12]
        0xD2, 0x00, 0x00, 0xEF,  // svc #0xd2
        0x1E, 0xFF, 0x2F, 0xE1,  // bx lr
        0xDD, 0xCC, 0xBB, 0xAA,  // object
        0x40, 0x00, 0x01, 0x00,  // destructor word
        0x00, 0x00, 0x2D, 0xA3,  // opaque DSO
    };
    if (!memory.write(code_page, code)) {
        return fail("could not stage DSO-binding constructor code");
    }

    const std::array<Elf32DependencyCatalogEntry, 0> entries{};
    Elf32DependencyCatalogProvider provider{std::span{entries}};
    Elf32LinkMap link_map;
    link_map.graph.objects.emplace_back();
    link_map.graph.objects[0].identity = "resident-binding";
    link_map.graph.objects[0].linker_metadata.init_function = code_page;
    link_map.roots.push_back({
        .object_index = 0U,
        .policy = liba32android::elf::Elf32LinkMapRootPolicy::Local,
    });
    link_map.object_states = {Elf32LinkMapObjectState::Active};

    Elf32LifecycleState lifecycle;
    lifecycle.objects.resize(1U);
    std::array<A32LibDlHandle, 1> handles{};
    std::array<A32AeabiAtexitRecord, 2> records{};
    std::array<A32AeabiObjectDsoBinding, 1> bindings{};
    Elf32LifecycleExecutionContext context;
    A32AeabiAtexitService registrations{
        std::span{records},
        std::span{bindings},
        &context,
    };
    const std::array<A32HostServiceRegistryEntry, 1> service_entries{{
        {kA32AeabiAtexitSvcImmediate, &registrations},
    }};
    A32HostServiceRegistry registry{std::span{service_entries}};

    auto options = transaction_options();
    options.lifecycle.execution.service_handler = &registry;
    options.lifecycle.execution.max_service_calls_per_call = 1U;
    options.lifecycle.execution.execution_context = &context;
    A32LibDlOpenTransaction transaction{
        memory,
        link_map,
        provider,
        std::span{handles},
        lifecycle,
        options,
    };

    const auto opened =
        transaction.open("resident-binding", 0x8ff8U);
    bool ambiguous = false;
    const auto learned =
        registrations.dso_for_object(0U, ambiguous);
    if (!opened ||
        opened.guest_handle != 0x70000000U ||
        opened.object_index != std::optional<std::size_t>{0U} ||
        lifecycle.objects[0].constructors !=
            Elf32LifecycleObjectStatus::Complete ||
        registrations.record_count() != 1U ||
        registrations.binding_count() != 1U ||
        ambiguous ||
        learned != 0xA32D0000U ||
        context.object_index.has_value() ||
        handles[0].refcount != 1U) {
        return fail("dlopen constructor did not learn DSO binding before handle publication");
    }
    return 0;
}

int test_noload_never_calls_provider_and_promotes_resident() {
    {
        MappedGuestMemory memory;
        CountingNotFoundProvider provider;
        Elf32LinkMap link_map;
        Elf32LifecycleState lifecycle;
        std::array<A32LibDlHandle, 1> handles{};
        A32LibDlOpenTransaction transaction{
            memory,
            link_map,
            provider,
            std::span{handles},
            lifecycle,
            transaction_options(),
        };

        const auto missing = transaction.open(
            "missing.so",
            0x8ff8U,
            A32LibDlOpenPolicy{.no_load = true});
        if (missing.error != A32LibDlOpenTransactionError::NotLoaded ||
            provider.calls != 0U ||
            !link_map.graph.objects.empty() ||
            !link_map.roots.empty() ||
            handles[0].refcount != 0U) {
            return fail("NOLOAD miss invoked provider or mutated state");
        }
    }

    {
        MappedGuestMemory memory;
        CountingNotFoundProvider provider;
        Elf32LinkMap link_map;
        link_map.graph.objects.emplace_back();
        link_map.graph.objects[0].identity = "resident-no-load";
        link_map.object_states = {Elf32LinkMapObjectState::Active};

        Elf32LifecycleState lifecycle;
        lifecycle.objects.resize(1U);
        lifecycle.objects[0].constructors =
            Elf32LifecycleObjectStatus::Complete;
        std::array<A32LibDlHandle, 1> handles{};
        A32LibDlOpenTransaction transaction{
            memory,
            link_map,
            provider,
            std::span{handles},
            lifecycle,
            transaction_options(),
        };

        const auto opened = transaction.open(
            "resident-no-load",
            0x8ff8U,
            A32LibDlOpenPolicy{
                .global = true,
                .nodelete = true,
                .no_load = true,
            });
        if (!opened ||
            opened.guest_handle != 0x70000000U ||
            !opened.root_added ||
            provider.calls != 0U ||
            link_map.roots.size() != 1U ||
            link_map.roots[0].object_index != 0U ||
            link_map.roots[0].policy !=
                liba32android::elf::Elf32LinkMapRootPolicy::Global ||
            !link_map.roots[0].nodelete ||
            link_map.global_scope_objects !=
                std::vector<std::size_t>{0U}) {
            return fail("NOLOAD resident hit did not create/promote root");
        }
    }
    return 0;
}

int test_constructor_failure_remains_resident_and_latched() {
    MappedGuestMemory memory;
    const std::array<Elf32DependencyCatalogEntry, 0> entries{};
    Elf32DependencyCatalogProvider provider{std::span{entries}};
    Elf32LinkMap link_map;
    link_map.graph.objects.emplace_back();
    link_map.graph.objects[0].identity = "failed-resident";
    link_map.graph.objects[0].linker_metadata.init_function = 0x20000U;
    link_map.object_states = {Elf32LinkMapObjectState::Active};

    Elf32LifecycleState lifecycle;
    lifecycle.objects.resize(1U);
    std::array<A32LibDlHandle, 1> handles{};
    A32LibDlOpenTransaction transaction{
        memory,
        link_map,
        provider,
        std::span{handles},
        lifecycle,
        transaction_options(),
    };

    const auto failed = transaction.open("failed-resident", 0x8ff8U);
    if (failed.error != A32LibDlOpenTransactionError::ConstructorFailed ||
        !failed.retained_after_failure ||
        failed.root_added ||
        failed.object_index != std::optional<std::size_t>{0U} ||
        lifecycle.objects[0].constructors !=
            Elf32LifecycleObjectStatus::Failed ||
        handles[0].refcount != 0U ||
        link_map.object_states[0] != Elf32LinkMapObjectState::Active) {
        return fail("constructor failure did not latch resident object state");
    }

    const auto repeated = transaction.open("failed-resident", 0x8ff8U);
    if (repeated.error !=
            A32LibDlOpenTransactionError::InvalidResidentLifecycle ||
        handles[0].refcount != 0U ||
        lifecycle.objects[0].constructors !=
            Elf32LifecycleObjectStatus::Failed) {
        return fail("failed constructor state was replayed by later dlopen");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status =
            test_service_dynamic_open_and_resident_refcount();
        status != 0) {
        return status;
    }
    if (const int status =
            test_preconstructor_failure_reclaims_added_root();
        status != 0) {
        return status;
    }
    if (const int status = test_open_constructor_learns_dso_binding();
        status != 0) {
        return status;
    }
    if (const int status =
            test_noload_never_calls_provider_and_promotes_resident();
        status != 0) {
        return status;
    }
    return test_constructor_failure_remains_resident_and_latched();
}
