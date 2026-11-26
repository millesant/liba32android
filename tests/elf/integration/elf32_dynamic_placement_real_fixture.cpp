#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "support/fixture_io.h"

#include "elf/elf32_dynamic_placement.h"
#include "elf/elf32_load_plan.h"
#include "elf/elf32_loader.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32ImageType;
using liba32android::elf::Elf32LoadOptions;
using liba32android::elf::load_elf32;
using liba32android::elf::place_elf32_dynamic;
using liba32android::elf::plan_elf32_load;
using liba32android::memory::MappedGuestMemory;

constexpr std::uint64_t kExpectedFixtureAlignment = 0x4000;

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return fail("expected path to generated ARM32 fixture");
    }

    const std::vector<std::uint8_t> image = liba32android::test_support::read_binary_file(argv[1]);
    if (image.empty()) {
        return fail("generated ARM32 fixture is missing or empty");
    }

    MappedGuestMemory memory;

    const auto plan_result = plan_elf32_load(memory, image);
    if (!plan_result) {
        return fail(
            std::string("real ARM32 fixture load planning failed: ") +
            liba32android::elf::to_string(plan_result.error));
    }

    const auto& plan = plan_result.plan;
    if (plan.type != Elf32ImageType::Dynamic) {
        return fail("real ARM32 fixture load plan is not ET_DYN");
    }
    if (plan.required_load_bias_alignment != kExpectedFixtureAlignment) {
        return fail("real ARM32 fixture did not preserve the expected 0x4000 load-bias alignment");
    }
    if (plan.maximum_page_end <= plan.minimum_page || plan.segments.empty()) {
        return fail("real ARM32 fixture load plan has an invalid mapped extent");
    }

    const auto placement = place_elf32_dynamic(memory, image);
    if (!placement) {
        return fail(
            std::string("real ARM32 fixture automatic placement failed: ") +
            liba32android::elf::to_string(placement.error));
    }

    if (placement.dynamic_base < plan.minimum_page) {
        return fail("real ARM32 fixture placement produced a base below the minimum load page");
    }

    const std::uint64_t load_bias =
        static_cast<std::uint64_t>(placement.dynamic_base) - plan.minimum_page;
    if ((load_bias % kExpectedFixtureAlignment) != 0) {
        return fail("real ARM32 fixture automatic placement violated p_align=0x4000");
    }

    const std::uint64_t page_size = memory.page_size();
    const std::uint64_t span = plan.maximum_page_end - plan.minimum_page;
    if ((placement.dynamic_base % page_size) != 0 ||
        (span % page_size) != 0) {
        return fail("real ARM32 fixture placement is not host-page compatible");
    }

    for (std::uint64_t offset = 0; offset < span; offset += page_size) {
        const std::uint64_t guest =
            static_cast<std::uint64_t>(placement.dynamic_base) + offset;
        if (guest > std::numeric_limits<std::uint32_t>::max()) {
            return fail("real ARM32 fixture placement span overflowed the 32-bit guest address space");
        }
        if (memory.is_mapped(static_cast<std::uint32_t>(guest))) {
            return fail("real ARM32 fixture placement mutated guest memory before load");
        }
    }

    Elf32LoadOptions options;
    options.dynamic_base = placement.dynamic_base;
    const auto load_result = load_elf32(memory, image, options);
    if (!load_result) {
        return fail(
            std::string("automatically placed real ARM32 fixture load failed: ") +
            liba32android::elf::to_string(load_result.error));
    }

    if (load_result.load_bias != load_bias ||
        load_result.segments.size() != plan.segments.size()) {
        return fail("automatically placed real ARM32 fixture returned unexpected load metadata");
    }
    if (!load_result.dynamic_segment.has_value()) {
        return fail("automatically placed real ARM32 fixture omitted PT_DYNAMIC metadata");
    }

    for (const auto& segment : load_result.segments) {
        if (!memory.is_mapped(segment.mapping_start) ||
            memory.permissions(segment.mapping_start) != segment.permissions) {
            return fail("automatically placed real ARM32 fixture did not apply final segment mappings");
        }
    }

    std::cout << "fixture.auto_placement.dynamic_base=0x" << std::hex
              << placement.dynamic_base << '\n'
              << "fixture.auto_placement.load_bias=0x" << load_result.load_bias << '\n'
              << "fixture.auto_placement.required_alignment=0x"
              << plan.required_load_bias_alignment << std::dec << '\n'
              << "fixture.auto_placement.segment_count=" << load_result.segments.size() << '\n'
              << "fixture.auto_placement.host_page_size=" << page_size << '\n'
              << "fixture.auto_placement.status=PASS\n";
    return 0;
}
