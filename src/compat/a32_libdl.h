#pragma once

#define LIBA32ANDROID_A32_LIBDL_DLOPEN_SVC 0xBC
#define LIBA32ANDROID_A32_LIBDL_DLSYM_SVC 0xBD
#define LIBA32ANDROID_A32_LIBDL_DLCLOSE_SVC 0xBE
#define LIBA32ANDROID_A32_LIBDL_DLERROR_SVC 0xBF
#define LIBA32ANDROID_A32_LIBDL_DLADDR_SVC 0xC0

#ifdef __cplusplus

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "elf/elf32_dependency_resolver.h"
#include "elf/elf32_link_map.h"
#include "elf/elf32_symbol_lookup.h"
#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

class A32LibDlCloseTransaction;
class A32LibDlOpenTransaction;

inline constexpr std::uint32_t kA32LibDlDlopenSvcImmediate =
    LIBA32ANDROID_A32_LIBDL_DLOPEN_SVC;
inline constexpr std::uint32_t kA32LibDlDlsymSvcImmediate =
    LIBA32ANDROID_A32_LIBDL_DLSYM_SVC;
inline constexpr std::uint32_t kA32LibDlDlcloseSvcImmediate =
    LIBA32ANDROID_A32_LIBDL_DLCLOSE_SVC;
inline constexpr std::uint32_t kA32LibDlDlerrorSvcImmediate =
    LIBA32ANDROID_A32_LIBDL_DLERROR_SVC;
inline constexpr std::uint32_t kA32LibDlDladdrSvcImmediate =
    LIBA32ANDROID_A32_LIBDL_DLADDR_SVC;

inline constexpr std::uint32_t kA32RtldLazy = 0x1U;
inline constexpr std::uint32_t kA32RtldNow = 0x2U;
inline constexpr std::uint32_t kA32RtldDefault = 0U;
inline constexpr std::uint32_t kA32RtldNext = 0xffffffffU;

inline constexpr std::string_view kA32LibDlShimSoname = "libdl.so";
inline constexpr std::string_view kA32LibDlShimIdentity =
    "liba32android-compat-libdl";

struct A32LibDlHandle {
    std::uint32_t guest_handle{};
    std::size_t object_index{};
    std::uint32_t refcount{};
};

struct A32LibDlOptions {
    std::uint32_t max_name_bytes{};
    std::uint32_t handle_base{};
    std::uint32_t error_buffer_address{};
    std::uint32_t error_buffer_bytes{};
    std::uint32_t info_string_buffer_address{};
    std::uint32_t info_string_buffer_bytes{};
    elf::Elf32SymbolLookupOptions symbols{};
};

class A32LibDlService final : public runtime::A32HostServiceHandler {
public:
    A32LibDlService(
        elf::Elf32LinkMap& link_map,
        std::span<A32LibDlHandle> handles,
        A32LibDlOptions options,
        A32LibDlCloseTransaction* close_transaction = nullptr,
        A32LibDlOpenTransaction* open_transaction = nullptr) noexcept;

    A32LibDlService(const A32LibDlService&) = delete;
    A32LibDlService& operator=(const A32LibDlService&) = delete;
    A32LibDlService(A32LibDlService&&) = delete;
    A32LibDlService& operator=(A32LibDlService&&) = delete;

    [[nodiscard]] runtime::A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;

private:
    enum class SymbolSearchStatus : std::uint8_t {
        Found = 0,
        Missing,
        Failed,
    };

    struct SymbolSearchResult {
        SymbolSearchStatus status{SymbolSearchStatus::Missing};
        std::uint32_t guest_value{};
    };

    struct DladdrSymbol {
        std::string name;
        std::uint32_t guest_value{};
    };

    [[nodiscard]] bool configuration_valid() const noexcept;
    [[nodiscard]] bool read_guest_string(
        const memory::GuestMemory& memory,
        std::uint32_t address,
        std::string& output) const;
    [[nodiscard]] bool write_guest_string(
        memory::GuestMemory& memory,
        std::uint32_t address,
        std::uint32_t capacity,
        std::string_view value) const;
    void set_error(std::string message);
    [[nodiscard]] std::optional<std::size_t> find_object(
        std::string_view name,
        bool& ambiguous) const noexcept;
    [[nodiscard]] std::optional<std::size_t> find_handle(
        std::uint32_t guest_handle) const noexcept;
    [[nodiscard]] std::uint32_t acquire_handle(
        std::size_t object_index) noexcept;
    [[nodiscard]] SymbolSearchResult lookup_symbol(
        const memory::GuestMemory& memory,
        std::uint32_t handle,
        std::string_view name) const;
    [[nodiscard]] std::optional<std::size_t> object_for_address(
        std::uint32_t address) const noexcept;
    [[nodiscard]] bool nearest_symbol(
        const memory::GuestMemory& memory,
        std::size_t object_index,
        std::uint32_t address,
        std::optional<DladdrSymbol>& result) const;
    [[nodiscard]] bool write_dl_info(
        memory::GuestMemory& memory,
        std::uint32_t info_address,
        std::size_t object_index,
        const std::optional<DladdrSymbol>& symbol) const;

    elf::Elf32LinkMap& link_map_;
    std::span<A32LibDlHandle> handles_;
    A32LibDlOptions options_;
    A32LibDlCloseTransaction* close_transaction_{};
    A32LibDlOpenTransaction* open_transaction_{};
    std::optional<std::string> pending_error_;
};

[[nodiscard]] inline elf::Elf32DependencyCatalogEntry
make_a32_libdl_shim_catalog_entry(
    std::span<const std::uint8_t> image) noexcept {
    return elf::Elf32DependencyCatalogEntry{
        .requested_name = kA32LibDlShimSoname,
        .identity = kA32LibDlShimIdentity,
        .image = image,
    };
}

}  // namespace liba32android::compat

#endif
