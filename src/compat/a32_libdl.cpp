#include "compat/a32_libdl.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "compat/a32_libdl_close_transaction.h"
#include "compat/a32_libdl_open_transaction.h"
#include "elf/elf32_linker_strings.h"
#include "memory/guest_memory.h"

namespace liba32android::compat {
namespace {

using runtime::A32HostServiceDisposition;

constexpr std::uint16_t kShnUndef = 0U;
constexpr std::uint16_t kShnAbs = 0xfff1U;
constexpr std::uint8_t kSttFunc = 2U;

[[nodiscard]] bool is_libdl_svc(std::uint32_t svc) noexcept {
    return svc == kA32LibDlDlopenSvcImmediate ||
           svc == kA32LibDlDlsymSvcImmediate ||
           svc == kA32LibDlDlcloseSvcImmediate ||
           svc == kA32LibDlDlerrorSvcImmediate ||
           svc == kA32LibDlDladdrSvcImmediate;
}

void store_u32_le(
    std::array<std::uint8_t, 16>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

}  // namespace

A32LibDlService::A32LibDlService(
    elf::Elf32LinkMap& link_map,
    std::span<A32LibDlHandle> handles,
    A32LibDlOptions options,
    A32LibDlCloseTransaction* close_transaction,
    A32LibDlOpenTransaction* open_transaction) noexcept
    : link_map_(link_map),
      handles_(handles),
      options_(options),
      close_transaction_(close_transaction),
      open_transaction_(open_transaction) {
    for (auto& handle : handles_) {
        handle = {};
    }
}

bool A32LibDlService::configuration_valid() const noexcept {
    const std::uint64_t minimum_info_bytes =
        static_cast<std::uint64_t>(options_.max_name_bytes) * 2U + 2U;
    if (handles_.empty() ||
        options_.max_name_bytes == 0U ||
        options_.handle_base == 0U ||
        (options_.handle_base & 0x3U) != 0U ||
        options_.error_buffer_address == 0U ||
        options_.error_buffer_bytes < 16U ||
        options_.info_string_buffer_address == 0U ||
        options_.info_string_buffer_bytes < minimum_info_bytes ||
        options_.symbols.max_symbols == 0U ||
        options_.symbols.max_scope_objects == 0U ||
        link_map_.graph.objects.size() >
            options_.symbols.max_scope_objects) {
        return false;
    }

    const std::uint64_t last_handle =
        static_cast<std::uint64_t>(options_.handle_base) +
        (handles_.size() - 1U) * 4ULL;
    return last_handle <= std::numeric_limits<std::uint32_t>::max() &&
           last_handle != kA32RtldNext &&
           (open_transaction_ == nullptr ||
            (open_transaction_->handle_base() == options_.handle_base &&
             open_transaction_->max_objects() <=
                 options_.symbols.max_scope_objects));
}

bool A32LibDlService::read_guest_string(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::string& output) const {
    if (address == 0U) {
        return false;
    }
    output.clear();
    for (std::uint32_t offset = 0U;; ++offset) {
        if (offset > std::numeric_limits<std::uint32_t>::max() - address) {
            return false;
        }
        std::array<std::uint8_t, 1> byte{};
        if (!memory.read(address + offset, byte)) {
            return false;
        }
        if (byte[0] == 0U) {
            return true;
        }
        if (offset == options_.max_name_bytes) {
            return false;
        }
        output.push_back(static_cast<char>(byte[0]));
    }
}

bool A32LibDlService::write_guest_string(
    memory::GuestMemory& memory,
    std::uint32_t address,
    std::uint32_t capacity,
    std::string_view value) const {
    if (address == 0U ||
        value.size() + 1U > capacity ||
        value.size() > options_.max_name_bytes ||
        value.size() >
            std::numeric_limits<std::uint32_t>::max() - address) {
        return false;
    }
    std::vector<std::uint8_t> bytes(value.begin(), value.end());
    bytes.push_back(0U);
    return memory.write(address, bytes);
}

void A32LibDlService::set_error(std::string message) {
    pending_error_ = std::move(message);
}

std::optional<std::size_t> A32LibDlService::find_object(
    std::string_view name,
    bool& ambiguous) const noexcept {
    ambiguous = false;
    std::optional<std::size_t> result;
    for (std::size_t index = 0; index < link_map_.graph.objects.size(); ++index) {
        if (!link_map_.object_active(index)) {
            continue;
        }
        const auto& object = link_map_.graph.objects[index];
        const bool match =
            object.identity == name ||
            (object.linker_strings.soname.has_value() &&
             *object.linker_strings.soname == name);
        if (!match) {
            continue;
        }
        if (result.has_value()) {
            ambiguous = true;
            return std::nullopt;
        }
        result = index;
    }
    return result;
}

std::optional<std::size_t> A32LibDlService::find_handle(
    std::uint32_t guest_handle) const noexcept {
    for (std::size_t index = 0; index < handles_.size(); ++index) {
        if (handles_[index].refcount != 0U &&
            handles_[index].guest_handle == guest_handle) {
            return index;
        }
    }
    return std::nullopt;
}

std::uint32_t A32LibDlService::acquire_handle(
    std::size_t object_index) noexcept {
    for (auto& handle : handles_) {
        if (handle.refcount != 0U &&
            handle.object_index == object_index) {
            if (handle.refcount == std::numeric_limits<std::uint32_t>::max()) {
                return 0U;
            }
            ++handle.refcount;
            return handle.guest_handle;
        }
    }

    for (std::size_t index = 0; index < handles_.size(); ++index) {
        auto& handle = handles_[index];
        if (handle.refcount != 0U) {
            continue;
        }
        const std::uint64_t value =
            static_cast<std::uint64_t>(options_.handle_base) +
            index * 4ULL;
        if (value == 0U ||
            value > std::numeric_limits<std::uint32_t>::max() ||
            value == kA32RtldNext) {
            return 0U;
        }
        handle = A32LibDlHandle{
            .guest_handle = static_cast<std::uint32_t>(value),
            .object_index = object_index,
            .refcount = 1U,
        };
        return handle.guest_handle;
    }
    return 0U;
}

A32LibDlService::SymbolSearchResult A32LibDlService::lookup_symbol(
    const memory::GuestMemory& memory,
    std::uint32_t handle,
    std::string_view name) const {
    elf::Elf32SymbolLookupOptions options = options_.symbols;
    options.global_scope_objects = {};

    auto search_root =
        [&](std::size_t object_index) -> SymbolSearchResult {
        const auto lookup = elf::lookup_elf32_graph_symbol(
            memory, link_map_.graph, object_index, name, options);
        if (lookup) {
            return SymbolSearchResult{
                .status = SymbolSearchStatus::Found,
                .guest_value = lookup.symbol.symbol.guest_value,
            };
        }
        if (lookup.error == elf::Elf32GraphSymbolLookupError::SymbolNotFound) {
            return SymbolSearchResult{
                .status = SymbolSearchStatus::Missing,
            };
        }
        return SymbolSearchResult{
            .status = SymbolSearchStatus::Failed,
        };
    };

    if (handle == kA32RtldDefault) {
        if (link_map_.roots.empty()) {
            return SymbolSearchResult{
                .status = SymbolSearchStatus::Missing,
            };
        }
        const std::size_t main_root = link_map_.roots.front().object_index;
        auto result = search_root(main_root);
        if (result.status != SymbolSearchStatus::Missing) {
            return result;
        }

        for (const std::size_t object_index : link_map_.global_scope_objects) {
            if (object_index == main_root) {
                continue;
            }
            result = search_root(object_index);
            if (result.status != SymbolSearchStatus::Missing) {
                return result;
            }
        }
        return result;
    }

    const auto slot = find_handle(handle);
    if (!slot.has_value() ||
        !link_map_.object_active(handles_[*slot].object_index)) {
        return SymbolSearchResult{
            .status = SymbolSearchStatus::Failed,
        };
    }
    return search_root(handles_[*slot].object_index);
}

std::optional<std::size_t> A32LibDlService::object_for_address(
    std::uint32_t address) const noexcept {
    for (std::size_t object_index = 0;
         object_index < link_map_.graph.objects.size();
         ++object_index) {
        if (!link_map_.object_active(object_index)) {
            continue;
        }
        const auto& object = link_map_.graph.objects[object_index];
        for (const auto& segment : object.load.segments) {
            const std::uint64_t begin = segment.guest_address;
            const std::uint64_t end = begin + segment.memory_size;
            if (address >= begin &&
                static_cast<std::uint64_t>(address) < end) {
                return object_index;
            }
        }
    }
    return std::nullopt;
}

bool A32LibDlService::nearest_symbol(
    const memory::GuestMemory& memory,
    std::size_t object_index,
    std::uint32_t address,
    std::optional<DladdrSymbol>& result) const {
    result.reset();
    if (object_index >= link_map_.graph.objects.size() ||
        !link_map_.object_active(object_index)) {
        return false;
    }
    const auto& object = link_map_.graph.objects[object_index];
    if (!object.linker_metadata.string_table.has_value() ||
        !object.linker_metadata.symbol_table.has_value() ||
        (!object.linker_metadata.sysv_hash_table.has_value() &&
         !object.linker_metadata.gnu_hash_table.has_value())) {
        return true;
    }

    const auto index = elf::build_elf32_symbol_index(
        memory, object.linker_metadata, options_.symbols);
    if (!index) {
        return false;
    }

    bool found = false;
    std::uint32_t best_order_value = 0U;
    std::uint32_t best_guest_value = 0U;
    std::uint32_t best_name_offset = 0U;
    const std::uint32_t query = address & ~1U;

    for (std::uint32_t symbol_index = 1U;
         symbol_index < index.index.symbol_count;
         ++symbol_index) {
        const auto symbol = elf::read_elf32_symbol_entry(
            memory, object.linker_metadata, index.index, symbol_index);
        if (!symbol) {
            return false;
        }
        if (symbol.symbol.section_index == kShnUndef ||
            symbol.symbol.name_offset == 0U) {
            continue;
        }

        std::uint64_t value = symbol.symbol.value;
        if (symbol.symbol.section_index != kShnAbs) {
            value += object.load.load_bias;
        }
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            continue;
        }
        const std::uint32_t guest_value =
            static_cast<std::uint32_t>(value);
        const std::uint32_t order_value =
            symbol.symbol.type == kSttFunc
                ? guest_value & ~1U
                : guest_value;
        if (order_value > query ||
            (found && order_value < best_order_value)) {
            continue;
        }
        found = true;
        best_order_value = order_value;
        best_guest_value = guest_value;
        best_name_offset = symbol.symbol.name_offset;
    }

    if (!found) {
        return true;
    }

    const auto name = elf::read_elf32_string_table_entry(
        memory,
        *object.linker_metadata.string_table,
        best_name_offset,
        elf::Elf32LinkerStringOptions{
            .max_string_bytes = options_.max_name_bytes,
        });
    if (!name || name.value.empty()) {
        return false;
    }
    result = DladdrSymbol{
        .name = name.value,
        .guest_value = best_guest_value,
    };
    return true;
}

bool A32LibDlService::write_dl_info(
    memory::GuestMemory& memory,
    std::uint32_t info_address,
    std::size_t object_index,
    const std::optional<DladdrSymbol>& symbol) const {
    if (info_address == 0U ||
        object_index >= link_map_.graph.objects.size() ||
        info_address >
            std::numeric_limits<std::uint32_t>::max() - 15U) {
        return false;
    }

    const auto& object = link_map_.graph.objects[object_index];
    const std::string_view filename =
        object.linker_strings.soname.has_value()
            ? std::string_view{*object.linker_strings.soname}
            : std::string_view{object.identity};
    if (filename.empty() ||
        filename.size() > options_.max_name_bytes) {
        return false;
    }

    const std::uint32_t fname_address =
        options_.info_string_buffer_address;
    if (!write_guest_string(
            memory,
            fname_address,
            options_.info_string_buffer_bytes,
            filename)) {
        return false;
    }

    std::uint32_t sname_address = 0U;
    std::uint32_t saddr = 0U;
    if (symbol.has_value()) {
        const std::uint64_t next =
            static_cast<std::uint64_t>(fname_address) +
            filename.size() + 1U;
        const std::uint64_t used =
            filename.size() + 1U;
        if (next > std::numeric_limits<std::uint32_t>::max() ||
            used > options_.info_string_buffer_bytes) {
            return false;
        }
        const std::uint32_t remaining =
            options_.info_string_buffer_bytes -
            static_cast<std::uint32_t>(used);
        sname_address = static_cast<std::uint32_t>(next);
        if (!write_guest_string(
                memory, sname_address, remaining, symbol->name)) {
            return false;
        }
        saddr = symbol->guest_value;
    }

    std::array<std::uint8_t, 16> info{};
    store_u32_le(info, 0U, fname_address);
    store_u32_le(info, 4U, object.load.load_bias);
    store_u32_le(info, 8U, sname_address);
    store_u32_le(info, 12U, saddr);
    return memory.write(info_address, info);
}

runtime::A32HostServiceDisposition A32LibDlService::handle(
    memory::GuestMemory& memory,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    if (!is_libdl_svc(svc_immediate)) {
        return A32HostServiceDisposition::Unhandled;
    }
    if (!configuration_valid()) {
        return A32HostServiceDisposition::Failed;
    }

    if (svc_immediate == kA32LibDlDlopenSvcImmediate) {
        const std::uint32_t mode = regs[1];
        const bool lazy = (mode & kA32RtldLazy) != 0U;
        const bool now = (mode & kA32RtldNow) != 0U;
        if (lazy == now ||
            (mode & ~(kA32RtldLazy | kA32RtldNow)) != 0U) {
            set_error("dlopen: unsupported flags");
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }

        if (regs[0] == 0U) {
            if (link_map_.roots.empty()) {
                set_error("dlopen: no main object");
                regs[0] = 0U;
                return A32HostServiceDisposition::Handled;
            }
            const std::size_t object_index =
                link_map_.roots.front().object_index;
            if (!link_map_.object_active(object_index)) {
                return A32HostServiceDisposition::Failed;
            }
            regs[0] = acquire_handle(object_index);
            if (regs[0] == 0U) {
                set_error("dlopen: handle table exhausted");
            }
            return A32HostServiceDisposition::Handled;
        }

        std::string name;
        if (!read_guest_string(memory, regs[0], name) || name.empty()) {
            return A32HostServiceDisposition::Failed;
        }

        if (open_transaction_ != nullptr) {
            const auto opened = open_transaction_->open(name, regs[13]);
            if (!opened) {
                set_error(
                    std::string{"dlopen: acquisition failed: "} +
                    to_string(opened.error));
                regs[0] = 0U;
                return A32HostServiceDisposition::Handled;
            }
            regs[0] = opened.guest_handle;
            return A32HostServiceDisposition::Handled;
        }

        bool ambiguous = false;
        const auto object_index = find_object(name, ambiguous);
        if (ambiguous) {
            set_error("dlopen: ambiguous resident object");
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }
        if (!object_index.has_value()) {
            set_error("dlopen: object not resident");
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }

        regs[0] = acquire_handle(*object_index);
        if (regs[0] == 0U) {
            set_error("dlopen: handle table exhausted");
        }
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32LibDlDlsymSvcImmediate) {
        if (regs[0] == kA32RtldNext) {
            set_error("dlsym: RTLD_NEXT unsupported");
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }
        std::string name;
        if (!read_guest_string(memory, regs[1], name) || name.empty()) {
            return A32HostServiceDisposition::Failed;
        }
        const auto result = lookup_symbol(memory, regs[0], name);
        if (result.status == SymbolSearchStatus::Found) {
            regs[0] = result.guest_value;
            return A32HostServiceDisposition::Handled;
        }
        set_error(
            result.status == SymbolSearchStatus::Missing
                ? "dlsym: symbol not found"
                : "dlsym: invalid handle or lookup failure");
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32LibDlDlcloseSvcImmediate) {
        if (close_transaction_ != nullptr) {
            const auto closed = close_transaction_->close(
                memory,
                regs[0],
                regs[13]);
            if (!closed) {
                set_error(
                    closed.error == A32LibDlCloseTransactionError::InvalidHandle
                        ? "dlclose: invalid handle"
                        : std::string{"dlclose: lifecycle transaction failed: "} +
                              to_string(closed.error));
                regs[0] = 0xffffffffU;
                return A32HostServiceDisposition::Handled;
            }
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }

        const auto slot = find_handle(regs[0]);
        if (!slot.has_value()) {
            set_error("dlclose: invalid handle");
            regs[0] = 0xffffffffU;
            return A32HostServiceDisposition::Handled;
        }
        auto& handle = handles_[*slot];
        --handle.refcount;
        if (handle.refcount == 0U) {
            handle = {};
        }
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32LibDlDlerrorSvcImmediate) {
        if (!pending_error_.has_value()) {
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }
        if (!write_guest_string(
                memory,
                options_.error_buffer_address,
                options_.error_buffer_bytes,
                *pending_error_)) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = options_.error_buffer_address;
        pending_error_.reset();
        return A32HostServiceDisposition::Handled;
    }

    const auto object_index = object_for_address(regs[0]);
    if (!object_index.has_value()) {
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }
    std::optional<DladdrSymbol> symbol;
    if (!nearest_symbol(memory, *object_index, regs[0], symbol) ||
        !write_dl_info(memory, regs[1], *object_index, symbol)) {
        return A32HostServiceDisposition::Failed;
    }
    regs[0] = 1U;
    return A32HostServiceDisposition::Handled;
}

}  // namespace liba32android::compat
