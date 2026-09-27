#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "elf/elf32_dependency_loader.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32DependencyEdge;
using liba32android::elf::Elf32DependencyLoadError;
using liba32android::elf::Elf32DependencyLoadOptions;
using liba32android::elf::Elf32DependencyLoadSource;
using liba32android::elf::Elf32DependencyProvider;
using liba32android::elf::Elf32LinkMap;
using liba32android::elf::Elf32LinkMapRootPolicy;
using liba32android::elf::Elf32LinkMapReclamationError;
using liba32android::elf::Elf32LinkMapReclamationOptions;
using liba32android::elf::plan_elf32_link_map_reclamation;
using liba32android::elf::kElf32Df1Global;
using liba32android::elf::Elf32DependencyProviderError;
using liba32android::elf::Elf32DependencyProviderResult;
using liba32android::elf::Elf32DependencyResolveError;
using liba32android::elf::Elf32DependencySource;
using liba32android::elf::Elf32DynamicError;
using liba32android::elf::append_elf32_link_map_root;
using liba32android::elf::load_elf32_dependency_graph;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;

constexpr std::size_t kHeaderSize = 52;
constexpr std::size_t kProgramHeaderSize = 32;
constexpr std::size_t kProgramHeaderOffset = kHeaderSize;
constexpr std::size_t kFirstProgramHeader = kProgramHeaderOffset;
constexpr std::size_t kSecondProgramHeader =
    kProgramHeaderOffset + kProgramHeaderSize;
constexpr std::size_t kThirdProgramHeader =
    kProgramHeaderOffset + 2 * kProgramHeaderSize;

constexpr std::uint32_t kPtLoad = 1;
constexpr std::uint32_t kPtDynamic = 2;
constexpr std::uint32_t kDtNull = 0;
constexpr std::uint32_t kDtNeeded = 1;
constexpr std::uint32_t kDtStrtab = 5;
constexpr std::uint32_t kDtStrsz = 10;
constexpr std::uint32_t kDtFlags1 = 0x6ffffffb;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

void write_u16(std::vector<std::uint8_t>& image,
               std::size_t offset,
               std::uint16_t value) {
    image[offset] = static_cast<std::uint8_t>(value & 0xffU);
    image[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

void write_u32(std::vector<std::uint8_t>& image,
               std::size_t offset,
               std::uint32_t value) {
    image[offset] = static_cast<std::uint8_t>(value & 0xffU);
    image[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    image[offset + 2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    image[offset + 3] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

void write_dynamic_entry(std::vector<std::uint8_t>& image,
                         std::size_t offset,
                         std::uint32_t tag,
                         std::uint32_t value) {
    write_u32(image, offset, tag);
    write_u32(image, offset + 4, value);
}

std::vector<std::uint8_t> make_image(std::uint16_t type,
                                     std::uint32_t virtual_base,
                                     bool with_dynamic,
                                     bool terminate_dynamic) {
    std::vector<std::uint8_t> image(0x4010, 0);
    image[0] = 0x7f;
    image[1] = 'E';
    image[2] = 'L';
    image[3] = 'F';
    image[4] = 1;
    image[5] = 1;
    image[6] = 1;

    write_u16(image, 16, type);
    write_u16(image, 18, 40);
    write_u32(image, 20, 1);
    write_u32(image, 24, virtual_base + 0x80);
    write_u32(image, 28, kProgramHeaderOffset);
    write_u16(image, 40, kHeaderSize);
    write_u16(image, 42, kProgramHeaderSize);
    write_u16(image, 44, with_dynamic ? 3 : 2);

    write_u32(image, kFirstProgramHeader + 0, kPtLoad);
    write_u32(image, kFirstProgramHeader + 4, 0);
    write_u32(image, kFirstProgramHeader + 8, virtual_base);
    write_u32(image, kFirstProgramHeader + 16, 0x100);
    write_u32(image, kFirstProgramHeader + 20, 0x100);
    write_u32(image, kFirstProgramHeader + 24, 5);
    write_u32(image, kFirstProgramHeader + 28, 0x4000);

    write_u32(image, kSecondProgramHeader + 0, kPtLoad);
    write_u32(image, kSecondProgramHeader + 4, 0x4000);
    write_u32(image, kSecondProgramHeader + 8, virtual_base + 0x4000);
    write_u32(image, kSecondProgramHeader + 16, 4);
    write_u32(image, kSecondProgramHeader + 20, 0x20);
    write_u32(image, kSecondProgramHeader + 24, 6);
    write_u32(image, kSecondProgramHeader + 28, 0x4000);

    image[0x4000] = 0x78;
    image[0x4001] = 0x56;
    image[0x4002] = 0x34;
    image[0x4003] = 0x12;

    if (with_dynamic) {
        write_u32(image, kThirdProgramHeader + 0, kPtDynamic);
        write_u32(image, kThirdProgramHeader + 4, 0xc0);
        write_u32(image, kThirdProgramHeader + 8, virtual_base + 0xc0);
        write_u32(image, kThirdProgramHeader + 16, 8);
        write_u32(image, kThirdProgramHeader + 20, 8);
        write_u32(image, kThirdProgramHeader + 24, 4);
        write_u32(image, kThirdProgramHeader + 28, 4);

        if (!terminate_dynamic) {
            write_dynamic_entry(image, 0xc0, 0x70000001U, 0x12345678U);
        }
    }

    return image;
}

std::vector<std::uint8_t> make_flags1_image(bool global) {
    auto image = make_image(3, 0, true, true);
    constexpr std::size_t dynamic_offset = 0xc0;
    write_dynamic_entry(
        image, dynamic_offset, kDtFlags1,
        global ? (kElf32Df1Global | 0x1U) : 0x1U);
    write_dynamic_entry(image, dynamic_offset + 8U, kDtNull, 0);
    write_u32(image, kThirdProgramHeader + 16, 16);
    write_u32(image, kThirdProgramHeader + 20, 16);
    return image;
}

std::vector<std::uint8_t> make_needed_image(
    std::uint16_t type,
    std::uint32_t virtual_base,
    const std::vector<std::string>& needed) {
    std::vector<std::uint8_t> image(0x4010, 0);
    image[0] = 0x7f;
    image[1] = 'E';
    image[2] = 'L';
    image[3] = 'F';
    image[4] = 1;
    image[5] = 1;
    image[6] = 1;

    write_u16(image, 16, type);
    write_u16(image, 18, 40);
    write_u32(image, 20, 1);
    write_u32(image, 24, virtual_base + 0x80);
    write_u32(image, 28, kProgramHeaderOffset);
    write_u16(image, 40, kHeaderSize);
    write_u16(image, 42, kProgramHeaderSize);
    write_u16(image, 44, 3);

    write_u32(image, kFirstProgramHeader + 0, kPtLoad);
    write_u32(image, kFirstProgramHeader + 4, 0);
    write_u32(image, kFirstProgramHeader + 8, virtual_base);
    write_u32(image, kFirstProgramHeader + 16, 0x400);
    write_u32(image, kFirstProgramHeader + 20, 0x400);
    write_u32(image, kFirstProgramHeader + 24, 5);
    write_u32(image, kFirstProgramHeader + 28, 0x4000);

    write_u32(image, kSecondProgramHeader + 0, kPtLoad);
    write_u32(image, kSecondProgramHeader + 4, 0x4000);
    write_u32(image, kSecondProgramHeader + 8, virtual_base + 0x4000);
    write_u32(image, kSecondProgramHeader + 16, 4);
    write_u32(image, kSecondProgramHeader + 20, 0x20);
    write_u32(image, kSecondProgramHeader + 24, 6);
    write_u32(image, kSecondProgramHeader + 28, 0x4000);

    image[0x4000] = 0x78;
    image[0x4001] = 0x56;
    image[0x4002] = 0x34;
    image[0x4003] = 0x12;

    constexpr std::size_t dynamic_offset = 0x100;
    constexpr std::size_t string_offset = 0x200;
    std::vector<std::uint32_t> name_offsets;
    std::size_t cursor = string_offset;
    for (const std::string& name : needed) {
        name_offsets.push_back(
            static_cast<std::uint32_t>(cursor - string_offset));
        for (const unsigned char byte : name) {
            image[cursor++] = byte;
        }
        image[cursor++] = 0;
    }

    const std::uint32_t string_size =
        static_cast<std::uint32_t>(cursor - string_offset);
    std::size_t dynamic_cursor = dynamic_offset;
    write_dynamic_entry(image, dynamic_cursor, kDtStrtab,
                        virtual_base + static_cast<std::uint32_t>(string_offset));
    dynamic_cursor += 8;
    write_dynamic_entry(image, dynamic_cursor, kDtStrsz, string_size);
    dynamic_cursor += 8;
    for (const std::uint32_t offset : name_offsets) {
        write_dynamic_entry(image, dynamic_cursor, kDtNeeded, offset);
        dynamic_cursor += 8;
    }
    write_dynamic_entry(image, dynamic_cursor, kDtNull, 0);
    dynamic_cursor += 8;

    write_u32(image, kThirdProgramHeader + 0, kPtDynamic);
    write_u32(image, kThirdProgramHeader + 4,
              static_cast<std::uint32_t>(dynamic_offset));
    write_u32(image, kThirdProgramHeader + 8,
              virtual_base + static_cast<std::uint32_t>(dynamic_offset));
    write_u32(image, kThirdProgramHeader + 16,
              static_cast<std::uint32_t>(dynamic_cursor - dynamic_offset));
    write_u32(image, kThirdProgramHeader + 20,
              static_cast<std::uint32_t>(dynamic_cursor - dynamic_offset));
    write_u32(image, kThirdProgramHeader + 24, 4);
    write_u32(image, kThirdProgramHeader + 28, 4);

    return image;
}

Elf32DependencyProviderResult success(
    std::string identity,
    std::vector<std::uint8_t> image) {
    Elf32DependencyProviderResult result;
    result.source = Elf32DependencySource{
        .identity = std::move(identity),
        .image = std::move(image),
    };
    return result;
}

class RecordingProvider final : public Elf32DependencyProvider {
public:
    std::vector<Elf32DependencyProviderResult> responses;
    std::vector<std::string> requesters;
    std::vector<std::string> requests;
    std::vector<std::uint64_t> limits;

    Elf32DependencyProviderResult resolve(
        std::string_view requested_name,
        std::uint64_t max_image_bytes) override {
        requests.emplace_back(requested_name.data(), requested_name.size());
        limits.push_back(max_image_bytes);
        const std::size_t index = requests.size() - 1;
        if (index >= responses.size()) {
            Elf32DependencyProviderResult result;
            result.error = Elf32DependencyProviderError::Failed;
            return result;
        }
        return responses[index];
    }

    Elf32DependencyProviderResult resolve_for(
        std::string_view requester_identity,
        std::string_view requested_name,
        std::uint64_t max_image_bytes) override {
        requesters.emplace_back(requester_identity.data(),
                                requester_identity.size());
        return resolve(requested_name, max_image_bytes);
    }
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

Elf32DependencyLoadOptions options() {
    Elf32DependencyLoadOptions result;
    result.max_objects = 8;
    result.max_depth = 8;
    result.max_dependency_occurrences = 32;
    result.max_image_bytes = 1U << 20;
    result.max_total_image_bytes = 8U << 20;
    result.max_string_bytes = 4096;
    result.placement.search_begin = 0x10000;
    result.placement.search_end_exclusive = 0x80000;
    return result;
}

int test_persistent_link_map_root_reuse() {
    MappedGuestMemory memory;
    FailIfCalledProvider provider;
    Elf32LinkMap link_map;
    const auto root_image = make_image(3, 0, true, true);

    const auto first = append_elf32_link_map_root(
        memory, link_map,
        Elf32DependencyLoadSource{
            .identity = "persistent-root",
            .image = root_image,
        },
        provider, options(), Elf32LinkMapRootPolicy::Local);
    if (!first || !first.root_object_index.has_value() ||
        *first.root_object_index != 0 || first.reused_existing_root ||
        link_map.graph.objects.size() != 1 ||
        link_map.roots.size() != 1 ||
        link_map.roots[0].object_index != 0 ||
        link_map.roots[0].policy != Elf32LinkMapRootPolicy::Local) {
        return fail("first persistent root load produced wrong link-map state");
    }
    const std::uint32_t load_bias = link_map.graph.objects[0].load.load_bias;

    const auto second = append_elf32_link_map_root(
        memory, link_map,
        Elf32DependencyLoadSource{
            .identity = "persistent-root",
            .image = root_image,
        },
        provider, options(), Elf32LinkMapRootPolicy::Global);
    if (!second || !second.root_object_index.has_value() ||
        *second.root_object_index != 0 || !second.reused_existing_root ||
        link_map.graph.objects.size() != 1 ||
        link_map.roots.size() != 1 ||
        link_map.roots[0].policy != Elf32LinkMapRootPolicy::Global ||
        link_map.graph.objects[0].load.load_bias != load_bias ||
        provider.calls != 0) {
        return fail("persistent root identity was not reused/promoted exactly");
    }

    auto mismatched = root_image;
    mismatched[0x1000] ^= 0xffU;
    const auto mismatch = append_elf32_link_map_root(
        memory, link_map,
        Elf32DependencyLoadSource{
            .identity = "persistent-root",
            .image = std::move(mismatched),
        },
        provider, options(), Elf32LinkMapRootPolicy::Global);
    if (mismatch.error != Elf32DependencyLoadError::IdentityImageMismatch ||
        link_map.graph.objects.size() != 1 ||
        link_map.roots.size() != 1 ||
        link_map.graph.objects[0].load.load_bias != load_bias) {
        return fail("cross-load root identity/image mismatch mutated link-map state");
    }
    return 0;
}

int test_persistent_link_map_dependency_reuse() {
    MappedGuestMemory memory;
    Elf32LinkMap link_map;
    const auto shared = make_needed_image(
        3, 0, std::vector<std::string>{"leaf.so"});
    const auto leaf = make_image(3, 0, false, true);
    RecordingProvider provider;
    provider.responses = {
        success("shared-id", shared),
        success("leaf-id", leaf),
        success("shared-id", shared),
    };

    const auto first = append_elf32_link_map_root(
        memory, link_map,
        Elf32DependencyLoadSource{
            .identity = "root-a",
            .image = make_needed_image(
                3, 0, std::vector<std::string>{"shared-a.so"}),
        },
        provider, options(), Elf32LinkMapRootPolicy::Local);
    const auto second = append_elf32_link_map_root(
        memory, link_map,
        Elf32DependencyLoadSource{
            .identity = "root-b",
            .image = make_needed_image(
                3, 0, std::vector<std::string>{"shared-b.so"}),
        },
        provider, options(), Elf32LinkMapRootPolicy::Local);

    if (!first || !second || link_map.graph.objects.size() != 4 ||
        link_map.roots.size() != 2 ||
        link_map.roots[0].object_index != 0 ||
        link_map.roots[1].object_index != 3 ||
        link_map.graph.objects[1].identity != "shared-id" ||
        link_map.graph.objects[1].dependencies.size() != 1 ||
        link_map.graph.objects[1].dependencies[0].target_object != 2 ||
        link_map.graph.objects[2].identity != "leaf-id" ||
        link_map.graph.objects[3].dependencies.size() != 1 ||
        link_map.graph.objects[3].dependencies[0].target_object != 1 ||
        provider.requests !=
            std::vector<std::string>{"shared-a.so", "leaf.so", "shared-b.so"} ||
        provider.requesters !=
            std::vector<std::string>{"root-a", "shared-id", "root-b"}) {
        return fail("persistent link map did not preserve root/nested requester context while reusing dependency identity");
    }
    return 0;
}

int test_persistent_link_map_global_membership_order() {
    MappedGuestMemory memory;
    Elf32LinkMap link_map;
    RecordingProvider provider;
    provider.responses = {
        success("global-dep", make_flags1_image(true)),
    };

    const auto first = append_elf32_link_map_root(
        memory, link_map,
        Elf32DependencyLoadSource{
            .identity = "global-root",
            .image = make_needed_image(
                3, 0, std::vector<std::string>{"global-dep.so"}),
        },
        provider, options(), Elf32LinkMapRootPolicy::Global);
    if (!first || link_map.graph.objects.size() != 2 ||
        link_map.global_scope_objects != std::vector<std::size_t>{0, 1} ||
        link_map.roots.size() != 1 ||
        link_map.roots[0].policy != Elf32LinkMapRootPolicy::Global) {
        return fail("global root/DF_1_GLOBAL dependency ordering was incorrect");
    }

    FailIfCalledProvider no_provider;
    const auto second = append_elf32_link_map_root(
        memory, link_map,
        Elf32DependencyLoadSource{
            .identity = "local-root",
            .image = make_image(3, 0, true, true),
        },
        no_provider, options(), Elf32LinkMapRootPolicy::Local);
    if (!second || !second.root_object_index.has_value() ||
        *second.root_object_index != 2 ||
        link_map.global_scope_objects != std::vector<std::size_t>{0, 1}) {
        return fail("local root unexpectedly changed persistent global scope");
    }

    const auto promote = append_elf32_link_map_root(
        memory, link_map,
        Elf32DependencyLoadSource{
            .identity = "local-root",
            .image = make_image(3, 0, true, true),
        },
        no_provider, options(), Elf32LinkMapRootPolicy::Global);
    if (!promote || !promote.reused_existing_root ||
        link_map.global_scope_objects !=
            std::vector<std::size_t>{0, 1, 2}) {
        return fail("reused root promotion did not append stable global membership");
    }

    const auto repeat = append_elf32_link_map_root(
        memory, link_map,
        Elf32DependencyLoadSource{
            .identity = "global-root",
            .image = make_needed_image(
                3, 0, std::vector<std::string>{"global-dep.so"}),
        },
        no_provider, options(), Elf32LinkMapRootPolicy::Global);
    if (!repeat || link_map.global_scope_objects !=
                       std::vector<std::size_t>{0, 1, 2}) {
        return fail("reused global root duplicated persistent global scope");
    }
    return 0;
}

int test_persistent_link_map_promotion_preserves_discovery_order() {
    MappedGuestMemory memory;
    Elf32LinkMap link_map;
    const auto root_image = make_needed_image(
        3, 0, std::vector<std::string>{"global-dep.so"});
    RecordingProvider provider;
    provider.responses = {
        success("global-dep", make_flags1_image(true)),
    };

    const auto first = append_elf32_link_map_root(
        memory, link_map,
        Elf32DependencyLoadSource{
            .identity = "older-local-root",
            .image = root_image,
        },
        provider, options(), Elf32LinkMapRootPolicy::Local);
    if (!first || link_map.graph.objects.size() != 2 ||
        link_map.global_scope_objects != std::vector<std::size_t>{1}) {
        return fail("could not stage older local root with global dependency");
    }

    FailIfCalledProvider no_provider;
    const auto promoted = append_elf32_link_map_root(
        memory, link_map,
        Elf32DependencyLoadSource{
            .identity = "older-local-root",
            .image = root_image,
        },
        no_provider, options(), Elf32LinkMapRootPolicy::Global);
    if (!promoted || !promoted.reused_existing_root ||
        link_map.global_scope_objects != std::vector<std::size_t>{0, 1}) {
        return fail("root promotion did not restore accumulated discovery order");
    }

    link_map.global_scope_objects = {1, 0};
    const auto malformed = append_elf32_link_map_root(
        memory, link_map,
        Elf32DependencyLoadSource{
            .identity = "new-root",
            .image = make_image(3, 0, true, true),
        },
        no_provider, options(), Elf32LinkMapRootPolicy::Local);
    if (malformed.error != Elf32DependencyLoadError::InvalidLinkMap ||
        link_map.graph.objects.size() != 2 || no_provider.calls != 0) {
        return fail("out-of-order persistent global scope was not rejected");
    }
    return 0;
}

int test_persistent_link_map_append_failure_preserves_prior_state() {
    MappedGuestMemory memory;
    Elf32LinkMap link_map;
    FailIfCalledProvider initial_provider;

    const auto first = append_elf32_link_map_root(
        memory, link_map,
        Elf32DependencyLoadSource{
            .identity = "stable-root",
            .image = make_image(3, 0, true, true),
        },
        initial_provider, options(), Elf32LinkMapRootPolicy::Global);
    if (!first || link_map.global_scope_objects !=
                      std::vector<std::size_t>{0}) {
        return fail("could not stage pre-existing persistent global root");
    }

    const auto stable_load = link_map.graph.objects[0].load;
    RecordingProvider provider;
    provider.responses = {
        success("bad-exec", make_image(2, 0x50000, false, true)),
    };

    const auto failed = append_elf32_link_map_root(
        memory, link_map,
        Elf32DependencyLoadSource{
            .identity = "failing-root",
            .image = make_needed_image(
                3, 0, std::vector<std::string>{"bad.so"}),
        },
        provider, options(), Elf32LinkMapRootPolicy::Global);

    if (failed.error != Elf32DependencyLoadError::DependencyNotDynamic ||
        link_map.graph.objects.size() != 1 ||
        link_map.roots.size() != 1 ||
        link_map.roots[0].policy != Elf32LinkMapRootPolicy::Global ||
        link_map.global_scope_objects != std::vector<std::size_t>{0} ||
        link_map.graph.objects[0].identity != "stable-root") {
        return fail("failed append published partial persistent link-map state");
    }
    for (const auto& segment : stable_load.segments) {
        if (!memory.is_mapped(segment.mapping_start)) {
            return fail("failed append unmapped pre-existing persistent object");
        }
    }
    if (memory.is_mapped(0x18000) || memory.is_mapped(0x1c000)) {
        return fail("failed append did not roll back newly mapped root");
    }
    return 0;
}

int test_persistent_link_map_limits_and_invalid_state() {
    {
        MappedGuestMemory memory;
        Elf32LinkMap link_map;
        FailIfCalledProvider provider;
        auto limited = options();
        limited.max_objects = 1;

        const auto first = append_elf32_link_map_root(
            memory, link_map,
            Elf32DependencyLoadSource{
                .identity = "only-root",
                .image = make_image(3, 0, true, true),
            },
            provider, limited, Elf32LinkMapRootPolicy::Global);
        if (!first || link_map.graph.objects.size() != 1 ||
            link_map.global_scope_objects != std::vector<std::size_t>{0}) {
            return fail("could not stage accumulated-object limit fixture");
        }
        const auto stable_load = link_map.graph.objects[0].load;

        const auto blocked = append_elf32_link_map_root(
            memory, link_map,
            Elf32DependencyLoadSource{
                .identity = "second-root",
                .image = make_image(3, 0, true, true),
            },
            provider, limited, Elf32LinkMapRootPolicy::Local);
        if (blocked.error != Elf32DependencyLoadError::TooManyObjects ||
            link_map.graph.objects.size() != 1 ||
            link_map.roots.size() != 1 ||
            link_map.global_scope_objects != std::vector<std::size_t>{0}) {
            return fail("accumulated max_objects did not reject a new root transactionally");
        }
        for (const auto& segment : stable_load.segments) {
            if (!memory.is_mapped(segment.mapping_start)) {
                return fail("accumulated-object limit disturbed existing mappings");
            }
        }
        if (provider.calls != 0) {
            return fail("accumulated-object limit unexpectedly called provider");
        }
    }

    {
        MappedGuestMemory memory;
        Elf32LinkMap link_map;
        FailIfCalledProvider provider;
        const auto first = append_elf32_link_map_root(
            memory, link_map,
            Elf32DependencyLoadSource{
                .identity = "stable-root",
                .image = make_image(3, 0, true, true),
            },
            provider, options(), Elf32LinkMapRootPolicy::Local);
        if (!first) return fail("could not stage invalid-link-map fixture");

        const auto stable_load = link_map.graph.objects[0].load;
        link_map.roots.push_back(link_map.roots.front());
        const auto duplicate_root = append_elf32_link_map_root(
            memory, link_map,
            Elf32DependencyLoadSource{
                .identity = "new-root",
                .image = make_image(3, 0, true, true),
            },
            provider, options(), Elf32LinkMapRootPolicy::Local);
        if (duplicate_root.error != Elf32DependencyLoadError::InvalidLinkMap ||
            link_map.graph.objects.size() != 1 || provider.calls != 0) {
            return fail("duplicate persistent root record was not rejected before mutation");
        }

        link_map.roots.resize(1);
        link_map.global_scope_objects = {9};
        const auto bad_global = append_elf32_link_map_root(
            memory, link_map,
            Elf32DependencyLoadSource{
                .identity = "new-root",
                .image = make_image(3, 0, true, true),
            },
            provider, options(), Elf32LinkMapRootPolicy::Local);
        if (bad_global.error != Elf32DependencyLoadError::InvalidLinkMap ||
            link_map.graph.objects.size() != 1 || provider.calls != 0) {
            return fail("out-of-range persistent global index was not rejected before mutation");
        }

        link_map.global_scope_objects.clear();
        link_map.graph.objects[0].dependencies.push_back(
            Elf32DependencyEdge{
                .requested_name = "broken.so",
                .target_object = 9,
            });
        const auto bad_edge = append_elf32_link_map_root(
            memory, link_map,
            Elf32DependencyLoadSource{
                .identity = "new-root",
                .image = make_image(3, 0, true, true),
            },
            provider, options(), Elf32LinkMapRootPolicy::Local);
        if (bad_edge.error != Elf32DependencyLoadError::InvalidLinkMap ||
            bad_edge.failing_identity != "stable-root" ||
            link_map.graph.objects.size() != 1 || provider.calls != 0) {
            return fail("out-of-range persistent dependency edge was not rejected before mutation");
        }

        link_map.graph.objects[0].dependencies.clear();
        link_map.roots[0].policy = Elf32LinkMapRootPolicy::Global;
        const auto missing_global = append_elf32_link_map_root(
            memory, link_map,
            Elf32DependencyLoadSource{
                .identity = "new-root",
                .image = make_image(3, 0, true, true),
            },
            provider, options(), Elf32LinkMapRootPolicy::Local);
        if (missing_global.error != Elf32DependencyLoadError::InvalidLinkMap ||
            missing_global.failing_identity != "stable-root" ||
            link_map.graph.objects.size() != 1 || provider.calls != 0) {
            return fail("missing required persistent global membership was not rejected");
        }

        link_map.roots[0].policy = Elf32LinkMapRootPolicy::Local;
        link_map.global_scope_objects = {0};
        const auto extra_global = append_elf32_link_map_root(
            memory, link_map,
            Elf32DependencyLoadSource{
                .identity = "new-root",
                .image = make_image(3, 0, true, true),
            },
            provider, options(), Elf32LinkMapRootPolicy::Local);
        if (extra_global.error != Elf32DependencyLoadError::InvalidLinkMap ||
            link_map.graph.objects.size() != 1 || provider.calls != 0) {
            return fail("spurious persistent global membership was not rejected");
        }

        link_map.global_scope_objects.clear();
        for (const auto& segment : stable_load.segments) {
            if (!memory.is_mapped(segment.mapping_start)) {
                return fail("invalid link-map preflight disturbed existing mappings");
            }
        }
    }
    return 0;
}

int test_persistent_link_map_reclamation_planning() {
    Elf32LinkMap link_map;
    constexpr const char* identities[] = {
        "root", "shared", "leaf", "handle-root",
        "global-only", "cycle-a", "cycle-b",
    };
    for (const char* identity : identities) {
        link_map.graph.objects.emplace_back();
        link_map.graph.objects.back().identity = identity;
    }

    link_map.graph.objects[0].dependencies.push_back(
        Elf32DependencyEdge{
            .requested_name = "shared.so",
            .target_object = 1U,
        });
    link_map.graph.objects[1].dependencies.push_back(
        Elf32DependencyEdge{
            .requested_name = "leaf.so",
            .target_object = 2U,
        });
    link_map.graph.objects[3].dependencies.push_back(
        Elf32DependencyEdge{
            .requested_name = "shared-from-handle.so",
            .target_object = 1U,
        });
    link_map.graph.objects[5].dependencies.push_back(
        Elf32DependencyEdge{
            .requested_name = "cycle-b.so",
            .target_object = 6U,
        });
    link_map.graph.objects[6].dependencies.push_back(
        Elf32DependencyEdge{
            .requested_name = "cycle-a.so",
            .target_object = 5U,
        });

    link_map.roots.push_back(
        {.object_index = 0U, .policy = Elf32LinkMapRootPolicy::Local});
    link_map.graph.objects[4].linker_metadata.global = true;
    link_map.global_scope_objects = {4U};

    const std::vector<std::size_t> live_anchors{3U, 3U, 5U};
    const auto with_handles = plan_elf32_link_map_reclamation(
        link_map,
        std::span<const std::size_t>{live_anchors},
        Elf32LinkMapReclamationOptions{.max_objects = 7U});
    if (!with_handles ||
        with_handles.plan.reachable_objects !=
            std::vector<std::size_t>{0U, 1U, 2U, 3U, 5U, 6U} ||
        with_handles.plan.reclaimable_objects !=
            std::vector<std::size_t>{4U} ||
        link_map.global_scope_objects != std::vector<std::size_t>{4U}) {
        return fail(
            "persistent roots/live handles did not define exact reclamation reachability");
    }

    const std::span<const std::size_t> no_live_anchors{};
    const auto without_handles = plan_elf32_link_map_reclamation(
        link_map,
        no_live_anchors,
        Elf32LinkMapReclamationOptions{.max_objects = 7U});
    if (!without_handles ||
        without_handles.plan.reachable_objects !=
            std::vector<std::size_t>{0U, 1U, 2U} ||
        without_handles.plan.reclaimable_objects !=
            std::vector<std::size_t>{3U, 4U, 6U, 5U}) {
        return fail(
            "unowned handle/cycle/global objects were not reclaimed deterministically");
    }

    Elf32LinkMap ordering;
    for (const char* identity :
         {"independent", "older-dependency", "leaf", "newer-requester"}) {
        ordering.graph.objects.emplace_back();
        ordering.graph.objects.back().identity = identity;
    }
    ordering.graph.objects[1].dependencies.push_back(
        Elf32DependencyEdge{
            .requested_name = "leaf.so",
            .target_object = 2U,
        });
    ordering.graph.objects[3].dependencies.push_back(
        Elf32DependencyEdge{
            .requested_name = "older.so",
            .target_object = 1U,
        });

    const auto ordered = plan_elf32_link_map_reclamation(
        ordering,
        no_live_anchors,
        Elf32LinkMapReclamationOptions{.max_objects = 4U});
    if (!ordered || !ordered.plan.reachable_objects.empty() ||
        ordered.plan.reclaimable_objects !=
            std::vector<std::size_t>{0U, 3U, 1U, 2U}) {
        return fail(
            "reclamation order did not keep requesters before dependencies");
    }
    return 0;
}

int test_persistent_link_map_reclamation_validation() {
    Elf32LinkMap link_map;
    for (const char* identity : {"root", "child"}) {
        link_map.graph.objects.emplace_back();
        link_map.graph.objects.back().identity = identity;
    }
    link_map.graph.objects[0].dependencies.push_back(
        Elf32DependencyEdge{
            .requested_name = "child.so",
            .target_object = 1U,
        });
    link_map.roots.push_back(
        {.object_index = 0U, .policy = Elf32LinkMapRootPolicy::Local});

    const std::span<const std::size_t> no_live_anchors{};
    const auto invalid_options = plan_elf32_link_map_reclamation(
        link_map,
        no_live_anchors,
        Elf32LinkMapReclamationOptions{});
    if (invalid_options.error !=
        Elf32LinkMapReclamationError::InvalidOptions) {
        return fail("reclamation planner accepted zero object ceiling");
    }

    const auto object_limit = plan_elf32_link_map_reclamation(
        link_map,
        no_live_anchors,
        Elf32LinkMapReclamationOptions{.max_objects = 1U});
    if (object_limit.error !=
        Elf32LinkMapReclamationError::ObjectLimitExceeded) {
        return fail("reclamation planner ignored accumulated object ceiling");
    }

    const std::vector<std::size_t> bad_anchor{2U};
    const auto invalid_anchor = plan_elf32_link_map_reclamation(
        link_map,
        std::span<const std::size_t>{bad_anchor},
        Elf32LinkMapReclamationOptions{.max_objects = 2U});
    if (invalid_anchor.error !=
            Elf32LinkMapReclamationError::InvalidLiveAnchor ||
        invalid_anchor.failing_object != 2U) {
        return fail("reclamation planner accepted out-of-range live anchor");
    }

    link_map.roots.push_back(link_map.roots.front());
    const auto duplicate_root = plan_elf32_link_map_reclamation(
        link_map,
        no_live_anchors,
        Elf32LinkMapReclamationOptions{.max_objects = 2U});
    if (duplicate_root.error !=
            Elf32LinkMapReclamationError::InvalidLinkMap ||
        duplicate_root.failing_object != 0U) {
        return fail("reclamation planner accepted duplicate persistent root");
    }

    link_map.roots.resize(1U);
    link_map.graph.objects[1].dependencies.push_back(
        Elf32DependencyEdge{
            .requested_name = "broken.so",
            .target_object = 9U,
        });
    const auto invalid_edge = plan_elf32_link_map_reclamation(
        link_map,
        no_live_anchors,
        Elf32LinkMapReclamationOptions{.max_objects = 2U});
    if (invalid_edge.error !=
            Elf32LinkMapReclamationError::InvalidLinkMap ||
        invalid_edge.failing_object != 1U) {
        return fail("reclamation planner accepted invalid dependency edge");
    }
    return 0;
}

int test_exec_root_without_dynamic() {
    constexpr std::uint32_t fixed_base = 0x10000;
    MappedGuestMemory memory;
    FailIfCalledProvider provider;

    auto result = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "root-exec",
            .image = make_image(2, fixed_base, false, true),
        },
        provider,
        options());

    if (!result || result.graph.objects.size() != 1 ||
        !result.root_object_index.has_value() ||
        *result.root_object_index != 0 || result.reused_existing_root) {
        return fail("dependency-free ET_EXEC root did not load with stable root metadata");
    }
    const auto& root = result.graph.objects[0];
    if (root.identity != "root-exec" || root.load.load_bias != 0 ||
        root.load.entry != fixed_base + 0x80 ||
        !root.dynamic_entries.empty() ||
        !root.linker_strings.needed.empty() ||
        !root.dependencies.empty()) {
        return fail("ET_EXEC root graph metadata was incorrect");
    }
    if (!memory.is_mapped(fixed_base) ||
        !memory.is_mapped(fixed_base + 0x4000) ||
        provider.calls != 0) {
        return fail("ET_EXEC root mapping/provider behavior was incorrect");
    }
    return 0;
}

int test_dynamic_root_automatic_placement() {
    MappedGuestMemory memory;
    FailIfCalledProvider provider;

    auto result = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "root-dyn",
            .image = make_image(3, 0, true, true),
        },
        provider,
        options());

    if (!result || result.graph.objects.size() != 1) {
        return fail("dependency-free ET_DYN root did not load");
    }
    const auto& root = result.graph.objects[0];
    if (root.load.load_bias != 0x10000 ||
        (root.load.load_bias % 0x4000) != 0 ||
        root.dynamic_entries.size() != 1 ||
        root.dynamic_entries[0].tag != 0 ||
        !root.linker_strings.needed.empty() ||
        provider.calls != 0) {
        return fail("ET_DYN root was not automatically placed/parsed as expected");
    }
    if (!memory.is_mapped(root.load.load_bias) ||
        !memory.is_mapped(root.load.load_bias + 0x4000)) {
        return fail("ET_DYN root mappings were not retained on success");
    }
    return 0;
}

int test_preflight_failures_do_not_mutate_memory() {
    MappedGuestMemory memory;
    FailIfCalledProvider provider;
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    const std::uint32_t sentinel = 0x70000;
    if (!memory.map(sentinel, memory.page_size(), rw)) {
        return fail("could not create preexisting sentinel mapping");
    }

    {
        auto invalid = options();
        invalid.max_objects = 0;
        const auto result = load_elf32_dependency_graph(
            memory,
            Elf32DependencyLoadSource{
                .identity = "root",
                .image = make_image(3, 0, false, true),
            },
            provider,
            invalid);
        if (result.error != Elf32DependencyLoadError::InvalidOptions) {
            return fail("invalid graph options were not rejected");
        }
    }

    {
        auto limited = options();
        limited.max_image_bytes = 1;
        const auto result = load_elf32_dependency_graph(
            memory,
            Elf32DependencyLoadSource{
                .identity = "root",
                .image = make_image(3, 0, false, true),
            },
            provider,
            limited);
        if (result.error != Elf32DependencyLoadError::ImageTooLarge) {
            return fail("root per-image byte ceiling was not enforced");
        }
    }

    if (!memory.is_mapped(sentinel) || memory.permissions(sentinel) != rw ||
        memory.is_mapped(0x10000) || provider.calls != 0) {
        return fail("preflight failure mutated guest memory or called provider");
    }
    return 0;
}

int test_post_load_dynamic_failure_rolls_back_root_only() {
    MappedGuestMemory memory;
    FailIfCalledProvider provider;
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    const std::uint32_t sentinel = 0x70000;
    if (!memory.map(sentinel, memory.page_size(), rw)) {
        return fail("could not create rollback sentinel mapping");
    }

    const auto result = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "bad-dynamic-root",
            .image = make_image(3, 0, true, false),
        },
        provider,
        options());

    if (result.error != Elf32DependencyLoadError::DynamicParseFailed ||
        result.dynamic_error != Elf32DynamicError::Unterminated ||
        !result.graph.objects.empty()) {
        return fail("unterminated dynamic root did not fail transactionally");
    }
    if (memory.is_mapped(0x10000) || memory.is_mapped(0x14000)) {
        return fail("failed root mappings were not rolled back");
    }
    if (!memory.is_mapped(sentinel) || memory.permissions(sentinel) != rw) {
        return fail("rollback disturbed preexisting guest mapping");
    }
    if (provider.calls != 0) {
        return fail("provider was called during T001 rollback case");
    }
    return 0;
}

int test_ordered_direct_dependencies() {
    MappedGuestMemory memory;
    RecordingProvider provider;
    provider.responses = {
        success("id-a", make_image(3, 0, false, true)),
        success("id-b", make_image(3, 0, false, true)),
    };

    const auto result = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "root",
            .image = make_needed_image(
                3, 0, std::vector<std::string>{"liba.so", "libb.so"}),
        },
        provider,
        options());

    if (!result || result.graph.objects.size() != 3) {
        return fail("ordered direct dependency graph did not load");
    }
    if (provider.requests !=
        std::vector<std::string>{"liba.so", "libb.so"}) {
        return fail("direct dependency provider order changed");
    }
    const auto& root = result.graph.objects[0];
    if (root.dependencies.size() != 2 ||
        root.dependencies[0].requested_name != "liba.so" ||
        root.dependencies[0].target_object != 1 ||
        root.dependencies[1].requested_name != "libb.so" ||
        root.dependencies[1].target_object != 2) {
        return fail("direct dependency edges did not preserve order");
    }
    if (result.graph.objects[1].identity != "id-a" ||
        result.graph.objects[2].identity != "id-b" ||
        result.graph.objects[1].load.load_bias ==
            result.graph.objects[2].load.load_bias) {
        return fail("direct dependency objects were not loaded uniquely");
    }
    return 0;
}

int test_repeated_and_alias_names_reuse_identity() {
    MappedGuestMemory memory;
    RecordingProvider provider;
    const auto child = make_image(3, 0, false, true);
    provider.responses = {
        success("shared-id", child),
        success("shared-id", child),
        success("shared-id", child),
    };

    const auto result = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "root",
            .image = make_needed_image(
                3, 0,
                std::vector<std::string>{"alias-a.so", "alias-b.so",
                                         "alias-a.so"}),
        },
        provider,
        options());

    if (!result || result.graph.objects.size() != 2 ||
        provider.requests.size() != 3) {
        return fail("provider-identity reuse did not preserve occurrence calls");
    }
    const auto& edges = result.graph.objects[0].dependencies;
    if (edges.size() != 3 ||
        edges[0].requested_name != "alias-a.so" ||
        edges[1].requested_name != "alias-b.so" ||
        edges[2].requested_name != "alias-a.so" ||
        edges[0].target_object != 1 ||
        edges[1].target_object != 1 ||
        edges[2].target_object != 1) {
        return fail("repeated/alias edges did not reuse one object");
    }
    return 0;
}

int test_identity_image_mismatch_rolls_back_all() {
    MappedGuestMemory memory;
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    constexpr std::uint32_t sentinel = 0x70000;
    if (!memory.map(sentinel, memory.page_size(), rw)) {
        return fail("could not map identity-mismatch sentinel");
    }

    auto child_a = make_image(3, 0, false, true);
    auto child_b = child_a;
    child_b[0x1000] ^= 0xffU;

    RecordingProvider provider;
    provider.responses = {
        success("same-id", child_a),
        success("same-id", child_b),
    };

    const auto result = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "root",
            .image = make_needed_image(
                3, 0, std::vector<std::string>{"first.so", "second.so"}),
        },
        provider,
        options());

    if (result.error != Elf32DependencyLoadError::IdentityImageMismatch ||
        result.failing_identity != "same-id" ||
        result.requested_name != "second.so" ||
        !result.graph.objects.empty()) {
        return fail("identity/image mismatch was not classified correctly");
    }
    for (const std::uint32_t address :
         {0x10000U, 0x14000U, 0x18000U, 0x1c000U}) {
        if (memory.is_mapped(address)) {
            return fail("identity mismatch did not roll back graph mappings");
        }
    }
    if (!memory.is_mapped(sentinel) || memory.permissions(sentinel) != rw) {
        return fail("identity mismatch rollback disturbed preexisting mapping");
    }
    return 0;
}

int test_exec_dependency_rejected_and_rolled_back() {
    MappedGuestMemory memory;
    RecordingProvider provider;
    provider.responses = {
        success("bad-exec", make_image(2, 0x30000, false, true)),
    };

    const auto result = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "root",
            .image = make_needed_image(
                3, 0, std::vector<std::string>{"bad.so"}),
        },
        provider,
        options());

    if (result.error != Elf32DependencyLoadError::DependencyNotDynamic ||
        result.failing_identity != "bad-exec" ||
        result.requested_name != "bad.so") {
        return fail("ET_EXEC dependency was not rejected distinctly");
    }
    if (memory.is_mapped(0x10000) || memory.is_mapped(0x14000) ||
        memory.is_mapped(0x30000)) {
        return fail("ET_EXEC dependency failure did not roll back root");
    }
    return 0;
}

int test_provider_and_resource_failures_are_transactional() {
    {
        MappedGuestMemory memory;
        RecordingProvider provider;
        Elf32DependencyProviderResult missing;
        missing.error = Elf32DependencyProviderError::NotFound;
        provider.responses = {missing};

        const auto result = load_elf32_dependency_graph(
            memory,
            Elf32DependencyLoadSource{
                .identity = "root",
                .image = make_needed_image(
                    3, 0, std::vector<std::string>{"missing.so"}),
            },
            provider,
            options());
        if (result.error != Elf32DependencyLoadError::DependencyResolveFailed ||
            result.dependency_error !=
                Elf32DependencyResolveError::DependencyNotFound ||
            memory.is_mapped(0x10000) || memory.is_mapped(0x14000)) {
            return fail("provider failure was not surfaced transactionally");
        }
    }

    {
        MappedGuestMemory memory;
        RecordingProvider provider;
        provider.responses = {
            success("child", make_image(3, 0, false, true)),
        };
        auto limited = options();
        limited.max_objects = 1;

        const auto result = load_elf32_dependency_graph(
            memory,
            Elf32DependencyLoadSource{
                .identity = "root",
                .image = make_needed_image(
                    3, 0, std::vector<std::string>{"child.so"}),
            },
            provider,
            limited);
        if (result.error != Elf32DependencyLoadError::TooManyObjects ||
            memory.is_mapped(0x10000) || memory.is_mapped(0x14000)) {
            return fail("object limit failure was not transactional");
        }
    }

    {
        MappedGuestMemory memory;
        RecordingProvider provider;
        const auto child = make_image(3, 0, false, true);
        provider.responses = {success("child", child)};
        auto root =
            make_needed_image(3, 0, std::vector<std::string>{"child.so"});
        auto limited = options();
        limited.max_total_image_bytes =
            static_cast<std::uint64_t>(root.size()) + 8;

        const auto result = load_elf32_dependency_graph(
            memory,
            Elf32DependencyLoadSource{
                .identity = "root",
                .image = std::move(root),
            },
            provider,
            limited);
        if (result.error != Elf32DependencyLoadError::DependencyResolveFailed ||
            result.dependency_error !=
                Elf32DependencyResolveError::TotalImageBytesExceeded ||
            memory.is_mapped(0x10000) || memory.is_mapped(0x14000)) {
            return fail("aggregate image budget failure was not transactional");
        }
    }

    return 0;
}


int test_transitive_dependency_loading() {
    MappedGuestMemory memory;
    RecordingProvider provider;
    provider.responses = {
        success("id-b", make_needed_image(
            3, 0, std::vector<std::string>{"c.so"})),
        success("id-c", make_image(3, 0, false, true)),
    };

    const auto result = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "root",
            .image = make_needed_image(
                3, 0, std::vector<std::string>{"b.so"}),
        },
        provider,
        options());

    if (!result || result.graph.objects.size() != 3) {
        return fail("transitive dependency graph did not load");
    }
    if (provider.requests != std::vector<std::string>{"b.so", "c.so"} ||
        provider.requesters != std::vector<std::string>{"root", "id-b"}) {
        return fail("transitive provider order/requester identity was incorrect");
    }
    if (result.graph.objects[0].dependencies.size() != 1 ||
        result.graph.objects[0].dependencies[0].target_object != 1 ||
        result.graph.objects[1].identity != "id-b" ||
        result.graph.objects[1].dependencies.size() != 1 ||
        result.graph.objects[1].dependencies[0].requested_name != "c.so" ||
        result.graph.objects[1].dependencies[0].target_object != 2 ||
        result.graph.objects[2].identity != "id-c") {
        return fail("transitive graph edges/objects were incorrect");
    }
    return 0;
}

int test_cycle_reuses_loading_root() {
    MappedGuestMemory memory;
    RecordingProvider provider;

    const auto root_image =
        make_needed_image(3, 0, std::vector<std::string>{"b.so"});
    const auto child_image =
        make_needed_image(3, 0, std::vector<std::string>{"root.so"});
    provider.responses = {
        success("id-b", child_image),
        success("root", root_image),
    };

    const auto result = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "root",
            .image = root_image,
        },
        provider,
        options());

    if (!result || result.graph.objects.size() != 2) {
        return fail("cycle did not terminate with two unique objects");
    }
    if (provider.requests != std::vector<std::string>{"b.so", "root.so"}) {
        return fail("cycle provider request order was incorrect");
    }
    if (result.graph.objects[0].dependencies.size() != 1 ||
        result.graph.objects[0].dependencies[0].target_object != 1 ||
        result.graph.objects[1].dependencies.size() != 1 ||
        result.graph.objects[1].dependencies[0].target_object != 0) {
        return fail("cycle edges did not reuse the loading root");
    }
    return 0;
}

int test_shared_transitive_dependency_reused() {
    MappedGuestMemory memory;
    RecordingProvider provider;

    const auto c_image = make_image(3, 0, false, true);
    provider.responses = {
        success("id-b", make_needed_image(
            3, 0, std::vector<std::string>{"c.so"})),
        success("id-c", c_image),
        success("id-c", c_image),
    };

    const auto result = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "root",
            .image = make_needed_image(
                3, 0, std::vector<std::string>{"b.so", "c.so"}),
        },
        provider,
        options());

    if (!result || result.graph.objects.size() != 3) {
        return fail("shared transitive dependency graph did not load");
    }
    if (provider.requests !=
        std::vector<std::string>{"b.so", "c.so", "c.so"}) {
        return fail("direct-set acquisition/depth-first traversal order changed");
    }
    const auto& root_edges = result.graph.objects[0].dependencies;
    const auto& b_edges = result.graph.objects[1].dependencies;
    if (root_edges.size() != 2 || b_edges.size() != 1 ||
        root_edges[0].target_object != 1 ||
        root_edges[1].target_object != 2 ||
        b_edges[0].target_object != 2) {
        return fail("shared transitive dependency was not reused");
    }
    return 0;
}

int test_depth_limit_rolls_back_graph() {
    MappedGuestMemory memory;
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    constexpr std::uint32_t sentinel = 0x70000;
    if (!memory.map(sentinel, memory.page_size(), rw)) {
        return fail("could not map depth-limit sentinel");
    }

    RecordingProvider provider;
    provider.responses = {
        success("id-b", make_needed_image(
            3, 0, std::vector<std::string>{"c.so"})),
        success("id-c", make_image(3, 0, false, true)),
    };
    auto limited = options();
    limited.max_depth = 1;

    const auto result = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "root",
            .image = make_needed_image(
                3, 0, std::vector<std::string>{"b.so"}),
        },
        provider,
        limited);

    if (result.error != Elf32DependencyLoadError::MaxDepthExceeded ||
        result.failing_identity != "id-c" ||
        result.requested_name != "c.so" ||
        !result.graph.objects.empty()) {
        return fail("max-depth failure was not classified correctly");
    }
    for (const std::uint32_t address :
         {0x10000U, 0x14000U, 0x18000U, 0x1c000U, 0x20000U}) {
        if (memory.is_mapped(address)) {
            return fail("max-depth failure left graph-owned mappings behind");
        }
    }
    if (!memory.is_mapped(sentinel) || memory.permissions(sentinel) != rw) {
        return fail("max-depth rollback disturbed preexisting mapping");
    }
    return 0;
}

int test_transitive_load_failure_rolls_back_graph() {
    MappedGuestMemory memory;
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    constexpr std::uint32_t sentinel = 0x70000;
    if (!memory.map(sentinel, memory.page_size(), rw)) {
        return fail("could not map transitive-failure sentinel");
    }

    RecordingProvider provider;
    provider.responses = {
        success("id-b", make_needed_image(
            3, 0, std::vector<std::string>{"bad.so"})),
        success("bad-exec", make_image(2, 0x30000, false, true)),
    };

    const auto result = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "root",
            .image = make_needed_image(
                3, 0, std::vector<std::string>{"b.so"}),
        },
        provider,
        options());

    if (result.error != Elf32DependencyLoadError::DependencyNotDynamic ||
        result.failing_identity != "bad-exec" ||
        result.requested_name != "bad.so") {
        return fail("transitive dependency failure was not surfaced");
    }
    for (const std::uint32_t address :
         {0x10000U, 0x14000U, 0x18000U, 0x1c000U, 0x30000U}) {
        if (memory.is_mapped(address)) {
            return fail("transitive failure did not roll back prior objects");
        }
    }
    if (!memory.is_mapped(sentinel) || memory.permissions(sentinel) != rw) {
        return fail("transitive rollback disturbed preexisting mapping");
    }
    return 0;
}

int test_recursive_occurrence_and_image_budgets() {
    {
        MappedGuestMemory memory;
        RecordingProvider provider;
        provider.responses = {
            success("id-b", make_needed_image(
                3, 0, std::vector<std::string>{"c.so"})),
        };
        auto limited = options();
        limited.max_dependency_occurrences = 1;

        const auto result = load_elf32_dependency_graph(
            memory,
            Elf32DependencyLoadSource{
                .identity = "root",
                .image = make_needed_image(
                    3, 0, std::vector<std::string>{"b.so"}),
            },
            provider,
            limited);

        if (result.error !=
                Elf32DependencyLoadError::TooManyDependencyOccurrences ||
            result.failing_identity != "id-b" ||
            provider.requests != std::vector<std::string>{"b.so"} ||
            memory.is_mapped(0x10000) || memory.is_mapped(0x14000) ||
            memory.is_mapped(0x18000) || memory.is_mapped(0x1c000)) {
            return fail("recursive occurrence budget was not enforced globally");
        }
    }

    {
        MappedGuestMemory memory;
        RecordingProvider provider;
        const auto child =
            make_needed_image(3, 0, std::vector<std::string>{"c.so"});
        const auto grandchild = make_image(3, 0, false, true);
        provider.responses = {
            success("id-b", child),
            success("id-c", grandchild),
        };

        auto root =
            make_needed_image(3, 0, std::vector<std::string>{"b.so"});
        auto limited = options();
        limited.max_total_image_bytes =
            static_cast<std::uint64_t>(root.size()) +
            static_cast<std::uint64_t>(child.size()) + 8U;

        const auto result = load_elf32_dependency_graph(
            memory,
            Elf32DependencyLoadSource{
                .identity = "root",
                .image = std::move(root),
            },
            provider,
            limited);

        if (result.error != Elf32DependencyLoadError::DependencyResolveFailed ||
            result.dependency_error !=
                Elf32DependencyResolveError::TotalImageBytesExceeded ||
            result.failing_identity != "id-b" ||
            provider.requests != std::vector<std::string>{"b.so", "c.so"} ||
            memory.is_mapped(0x10000) || memory.is_mapped(0x14000) ||
            memory.is_mapped(0x18000) || memory.is_mapped(0x1c000)) {
            return fail("recursive image budget was not enforced globally");
        }
    }

    return 0;
}

}  // namespace

int main() {
    if (const int status = test_persistent_link_map_root_reuse(); status != 0) return status;
    if (const int status = test_persistent_link_map_dependency_reuse(); status != 0) return status;
    if (const int status = test_persistent_link_map_global_membership_order(); status != 0) return status;
    if (const int status = test_persistent_link_map_promotion_preserves_discovery_order(); status != 0) return status;
    if (const int status = test_persistent_link_map_append_failure_preserves_prior_state(); status != 0) return status;
    if (const int status = test_persistent_link_map_limits_and_invalid_state(); status != 0) return status;
    if (const int status = test_persistent_link_map_reclamation_planning(); status != 0) return status;
    if (const int status = test_persistent_link_map_reclamation_validation(); status != 0) return status;
    if (const int status = test_exec_root_without_dynamic(); status != 0) return status;
    if (const int status = test_dynamic_root_automatic_placement(); status != 0) return status;
    if (const int status = test_preflight_failures_do_not_mutate_memory(); status != 0) return status;
    if (const int status = test_post_load_dynamic_failure_rolls_back_root_only();
        status != 0) {
        return status;
    }
    if (const int status = test_ordered_direct_dependencies(); status != 0) return status;
    if (const int status = test_repeated_and_alias_names_reuse_identity();
        status != 0) {
        return status;
    }
    if (const int status = test_identity_image_mismatch_rolls_back_all();
        status != 0) {
        return status;
    }
    if (const int status = test_exec_dependency_rejected_and_rolled_back();
        status != 0) {
        return status;
    }
    if (const int status = test_provider_and_resource_failures_are_transactional();
        status != 0) {
        return status;
    }
    if (const int status = test_transitive_dependency_loading(); status != 0) return status;
    if (const int status = test_cycle_reuses_loading_root(); status != 0) return status;
    if (const int status = test_shared_transitive_dependency_reused(); status != 0) return status;
    if (const int status = test_depth_limit_rolls_back_graph(); status != 0) return status;
    if (const int status = test_transitive_load_failure_rolls_back_graph(); status != 0) return status;
    if (const int status = test_recursive_occurrence_and_image_budgets(); status != 0) return status;
    return 0;
}
