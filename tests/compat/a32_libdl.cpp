#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "compat/a32_libdl.h"
#include "elf/elf32_link_map.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"

namespace {

using liba32android::compat::A32LibDlHandle;
using liba32android::compat::A32LibDlOptions;
using liba32android::compat::A32LibDlService;
using liba32android::compat::kA32LibDlDladdrSvcImmediate;
using liba32android::compat::kA32LibDlDlcloseSvcImmediate;
using liba32android::compat::kA32LibDlDlerrorSvcImmediate;
using liba32android::compat::kA32LibDlDlopenSvcImmediate;
using liba32android::compat::kA32LibDlDlsymSvcImmediate;
using liba32android::compat::kA32RtldDefault;
using liba32android::compat::kA32RtldGlobal;
using liba32android::compat::kA32RtldLazy;
using liba32android::compat::kA32RtldNoLoad;
using liba32android::compat::kA32RtldNodelete;
using liba32android::compat::kA32RtldNow;
using liba32android::compat::kA32RtldNext;
using liba32android::elf::Elf32LinkMap;
using liba32android::elf::Elf32LinkMapRoot;
using liba32android::elf::Elf32LinkMapRootPolicy;
using liba32android::elf::Elf32LoadedDependencyObject;
using liba32android::elf::Elf32LoadedSegment;
using liba32android::memory::LinearGuestMemory;
using liba32android::runtime::A32HostServiceDisposition;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

std::uint32_t read_u32(
    const LinearGuestMemory& memory,
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

std::string read_c_string(
    const LinearGuestMemory& memory,
    std::uint32_t address,
    std::size_t limit) {
    std::string result;
    for (std::size_t i = 0; i <= limit; ++i) {
        std::array<std::uint8_t, 1> byte{};
        if (!memory.read(address + static_cast<std::uint32_t>(i), byte)) {
            return {};
        }
        if (byte[0] == 0U) {
            return result;
        }
        result.push_back(static_cast<char>(byte[0]));
    }
    return {};
}

A32LibDlOptions options() {
    A32LibDlOptions result;
    result.max_name_bytes = 64U;
    result.handle_base = 0x70000000U;
    result.error_buffer_address = 0x900U;
    result.error_buffer_bytes = 128U;
    result.info_string_buffer_address = 0xA00U;
    result.info_string_buffer_bytes = 256U;
    result.symbols.max_symbols = 64U;
    result.symbols.max_hash_buckets = 64U;
    result.symbols.max_gnu_bloom_words = 32U;
    result.symbols.max_scope_objects = 8U;
    result.symbols.max_name_bytes = 64U;
    result.symbols.max_version_records = 64U;
    return result;
}

int test_arm32_policy_flags_and_resident_noload() {
    if (kA32RtldNow != 0x00000U ||
        kA32RtldLazy != 0x00001U ||
        kA32RtldGlobal != 0x00002U ||
        kA32RtldNoLoad != 0x00004U ||
        kA32RtldNodelete != 0x01000U ||
        kA32RtldDefault != 0xffffffffU ||
        kA32RtldNext != 0xfffffffeU) {
        return fail("ARM32 bionic libdl constants do not match LP32 ABI");
    }

    LinearGuestMemory memory{0x3000U};
    Elf32LinkMap link_map;
    link_map.graph.objects.resize(2U);
    link_map.graph.objects[0].identity = "main";
    link_map.graph.objects[1].identity = "resident-dependency";
    link_map.graph.objects[1].linker_strings.soname = "libdep.so";
    link_map.roots.push_back({
        .object_index = 0U,
        .policy = Elf32LinkMapRootPolicy::Local,
    });
    link_map.object_states.assign(
        2U, liba32android::elf::Elf32LinkMapObjectState::Active);

    constexpr std::array<std::uint8_t, 10> dep_name{{
        'l','i','b','d','e','p','.','s','o',0U,
    }};
    constexpr std::array<std::uint8_t, 11> missing_name{{
        'm','i','s','s','i','n','g','.','s','o',0U,
    }};
    constexpr std::array<std::uint8_t, 5> missing_symbol{{
        'n','o','p','e',0U,
    }};
    if (!memory.write(0x100U, dep_name) ||
        !memory.write(0x140U, missing_name) ||
        !memory.write(0x180U, missing_symbol)) {
        return fail("could not stage policy guest names");
    }

    std::array<A32LibDlHandle, 2> handles{};
    A32LibDlService service{link_map, std::span{handles}, options()};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    regs[0] = 0x100U;
    regs[1] =
        kA32RtldNoLoad | kA32RtldGlobal | kA32RtldNodelete;
    if (service.handle(
            memory, kA32LibDlDlopenSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0x70000000U ||
        link_map.roots.size() != 2U ||
        link_map.roots[1].object_index != 1U ||
        link_map.roots[1].policy != Elf32LinkMapRootPolicy::Global ||
        !link_map.roots[1].nodelete ||
        link_map.global_scope_objects !=
            std::vector<std::size_t>{1U}) {
        return fail("resident NOLOAD did not add/promote exact root policy");
    }
    const std::uint32_t handle = regs[0];

    regs = {};
    regs[0] = 0x100U;
    regs[1] = kA32RtldNow;
    if (service.handle(
            memory, kA32LibDlDlopenSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != handle ||
        handles[0].refcount != 2U ||
        link_map.roots[1].policy != Elf32LinkMapRootPolicy::Global ||
        !link_map.roots[1].nodelete) {
        return fail("local reopen demoted resident global/nodelete policy");
    }

    regs = {};
    regs[0] = 0x140U;
    regs[1] = kA32RtldNoLoad;
    if (service.handle(
            memory, kA32LibDlDlopenSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        link_map.roots.size() != 2U) {
        return fail("missing resident NOLOAD unexpectedly acquired object");
    }

    regs = {};
    regs[0] = kA32RtldDefault;
    regs[1] = 0x180U;
    if (service.handle(
            memory, kA32LibDlDlsymSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("ARM32 RTLD_DEFAULT sentinel was not accepted");
    }
    regs = {};
    if (service.handle(
            memory, kA32LibDlDlerrorSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        read_c_string(memory, regs[0], 127U) !=
            "dlsym: symbol not found") {
        return fail("RTLD_DEFAULT miss was treated as invalid handle");
    }

    regs = {};
    regs[0] = kA32RtldNext;
    if (service.handle(
            memory, kA32LibDlDlsymSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("ARM32 RTLD_NEXT sentinel was not rejected");
    }
    regs = {};
    if (service.handle(
            memory, kA32LibDlDlerrorSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        read_c_string(memory, regs[0], 127U) !=
            "dlsym: RTLD_NEXT unsupported") {
        return fail("RTLD_NEXT rejection did not publish expected dlerror");
    }

    return 0;
}

int test_resident_open_close_error_and_dladdr() {
    LinearGuestMemory memory{0x2000};

    Elf32LinkMap link_map;
    Elf32LoadedDependencyObject object;
    object.identity = "resident-id";
    object.linker_strings.soname = "libresident.so";
    object.load.load_bias = 0x300U;
    object.load.segments.push_back(Elf32LoadedSegment{
        .guest_address = 0x300U,
        .file_size = 0x40U,
        .memory_size = 0x80U,
        .mapping_start = 0x300U,
        .mapping_size = 0x80U,
    });
    link_map.graph.objects.push_back(std::move(object));
    link_map.roots.push_back(Elf32LinkMapRoot{
        .object_index = 0U,
        .policy = Elf32LinkMapRootPolicy::Local,
    });

    constexpr std::array<std::uint8_t, 15> resident_name{
        'l','i','b','r','e','s','i','d','e','n','t','.','s','o',0,
    };
    constexpr std::array<std::uint8_t, 11> missing_name{
        'm','i','s','s','i','n','g','.','s','o',0,
    };
    constexpr std::array<std::uint8_t, 5> symbol_name{
        'n','o','p','e',0,
    };
    if (!memory.write(0x100U, resident_name) ||
        !memory.write(0x140U, missing_name) ||
        !memory.write(0x180U, symbol_name)) {
        return fail("could not stage libdl guest strings");
    }

    std::array<A32LibDlHandle, 2> handles{};
    A32LibDlService service{link_map, std::span{handles}, options()};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    regs[0] = 0x100U;
    regs[1] = kA32RtldNow;
    if (service.handle(memory, kA32LibDlDlopenSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0x70000000U) {
        return fail("resident dlopen did not return first synthetic handle");
    }
    const std::uint32_t handle = regs[0];

    regs = {};
    regs[0] = 0x100U;
    regs[1] = kA32RtldNow;
    if (service.handle(memory, kA32LibDlDlopenSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != handle ||
        handles[0].refcount != 2U) {
        return fail("repeated resident dlopen did not refcount same handle");
    }

    regs = {};
    regs[0] = 0x320U;
    regs[1] = 0x600U;
    if (service.handle(memory, kA32LibDlDladdrSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 1U ||
        read_u32(memory, 0x600U) != 0xA00U ||
        read_u32(memory, 0x604U) != 0x300U ||
        read_u32(memory, 0x608U) != 0U ||
        read_u32(memory, 0x60CU) != 0U ||
        read_c_string(memory, 0xA00U, 64U) != "libresident.so") {
        return fail("dladdr did not describe resident object");
    }

    regs = {};
    regs[0] = handle;
    regs[1] = 0x180U;
    if (service.handle(memory, kA32LibDlDlsymSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("missing dlsym did not return null");
    }

    regs = {};
    if (service.handle(memory, kA32LibDlDlerrorSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0x900U ||
        read_c_string(memory, 0x900U, 127U).empty()) {
        return fail("dlerror did not publish pending lookup error");
    }
    regs = {};
    if (service.handle(memory, kA32LibDlDlerrorSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("dlerror did not clear error after one read");
    }

    for (int i = 0; i < 2; ++i) {
        regs = {};
        regs[0] = handle;
        if (service.handle(memory, kA32LibDlDlcloseSvcImmediate, regs, cpsr) !=
                A32HostServiceDisposition::Handled ||
            regs[0] != 0U) {
            return fail("dlclose failed for live resident handle");
        }
    }
    if (handles[0].refcount != 0U) {
        return fail("final dlclose did not release synthetic handle");
    }

    regs = {};
    regs[0] = handle;
    if (service.handle(memory, kA32LibDlDlcloseSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0xffffffffU) {
        return fail("invalid dlclose did not fail with nonzero result");
    }

    regs = {};
    regs[0] = 0x140U;
    regs[1] = kA32RtldNow;
    if (service.handle(memory, kA32LibDlDlopenSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("missing resident dlopen did not return null");
    }

    regs = {};
    if (service.handle(memory, 0xD0U, regs, cpsr) !=
        A32HostServiceDisposition::Unhandled) {
        return fail("unknown libdl SVC was not unhandled");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_arm32_policy_flags_and_resident_noload();
        status != 0) {
        return status;
    }
    return test_resident_open_close_error_and_dladdr();
}
