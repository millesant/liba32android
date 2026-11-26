#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "elf/elf32_dynamic.h"
#include "elf/elf32_linker_metadata.h"
#include "elf/elf32_linker_strings.h"
#include "elf/elf32_load_types.h"

namespace liba32android::elf {

struct Elf32DependencyEdge {
    std::string requested_name;
    std::size_t target_object{};
};

struct Elf32LoadedDependencyObject {
    std::string identity;
    std::vector<std::uint8_t> image;
    Elf32LoadResult load;
    std::vector<Elf32DynamicEntry> dynamic_entries;
    Elf32LinkerMetadata linker_metadata;
    Elf32LinkerStrings linker_strings;
    std::vector<Elf32DependencyEdge> dependencies;
};

struct Elf32DependencyGraph {
    std::vector<Elf32LoadedDependencyObject> objects;
};

}  // namespace liba32android::elf
