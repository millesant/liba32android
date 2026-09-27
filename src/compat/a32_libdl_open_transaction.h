#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "compat/a32_libdl.h"
#include "elf/elf32_dependency_loader.h"
#include "elf/elf32_dependency_resolver.h"
#include "elf/elf32_lifecycle.h"
#include "elf/elf32_link_map_reclamation.h"
#include "elf/elf32_relro.h"
#include "elf/elf32_relocation.h"
#include "memory/guest_memory.h"

namespace liba32android::compat {

struct A32LibDlOpenTransactionOptions {
    std::uint32_t handle_base{};
    elf::Elf32DependencyLoadOptions load;
    elf::Elf32RelocationOptions relocation;
    elf::Elf32RelroOptions relro;
    elf::Elf32PersistentLifecycleOptions lifecycle;
    elf::Elf32LinkMapReleaseOptions reclamation;
};

enum class A32LibDlOpenTransactionError : std::uint8_t {
    None = 0,
    InvalidOptions,
    InvalidName,
    AmbiguousResidentObject,
    InvalidResidentLifecycle,
    HandleTableExhausted,
    HandleRefcountOverflow,
    DependencyNotFound,
    ProviderFailed,
    InvalidProviderResult,
    AppendFailed,
    RelocationFailed,
    RelroFailed,
    ConstructorFailed,
    CleanupFailed,
};

struct A32LibDlOpenTransactionResult {
    A32LibDlOpenTransactionError error{
        A32LibDlOpenTransactionError::None};
    std::uint32_t guest_handle{};
    std::optional<std::size_t> object_index;
    bool root_added{};
    std::size_t objects_appended{};
    bool retained_after_failure{};

    elf::Elf32DependencyProviderError provider_error{
        elf::Elf32DependencyProviderError::None};
    elf::Elf32DependencyLoadError load_error{
        elf::Elf32DependencyLoadError::None};
    elf::Elf32RelocationApplyError relocation_error{
        elf::Elf32RelocationApplyError::None};
    elf::Elf32RelroError relro_error{elf::Elf32RelroError::None};
    elf::Elf32PersistentLifecycleError lifecycle_error{
        elf::Elf32PersistentLifecycleError::None};
    elf::Elf32LinkMapReleaseError cleanup_error{
        elf::Elf32LinkMapReleaseError::None};
    std::optional<std::size_t> failing_object;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == A32LibDlOpenTransactionError::None;
    }
};

// Bounded named-dlopen initialization above the accepted persistent ELF
// primitives. The transaction owns no provider/path policy and publishes a
// synthetic guest handle only after initialization succeeds.
class A32LibDlOpenTransaction final {
public:
    A32LibDlOpenTransaction(
        memory::MappedGuestMemory& memory,
        elf::Elf32LinkMap& link_map,
        elf::Elf32DependencyProvider& provider,
        std::span<A32LibDlHandle> handles,
        elf::Elf32LifecycleState& lifecycle,
        A32LibDlOpenTransactionOptions options) noexcept
        : memory_(memory),
          link_map_(link_map),
          provider_(provider),
          handles_(handles),
          lifecycle_(lifecycle),
          options_(options) {}

    [[nodiscard]] A32LibDlOpenTransactionResult open(
        std::string_view requested_name,
        std::optional<std::uint32_t> nested_stack_top = std::nullopt);

    [[nodiscard]] std::uint32_t handle_base() const noexcept {
        return options_.handle_base;
    }

    [[nodiscard]] std::uint32_t max_objects() const noexcept {
        return options_.load.max_objects;
    }

private:
    [[nodiscard]] bool options_valid() const noexcept;
    [[nodiscard]] std::optional<std::size_t> find_active_object(
        std::string_view name,
        bool& ambiguous) const noexcept;
    [[nodiscard]] std::optional<std::size_t> find_active_identity(
        std::string_view identity) const noexcept;
    [[nodiscard]] bool root_record_exists(
        std::size_t object_index) const noexcept;
    [[nodiscard]] bool can_acquire_handle(
        std::optional<std::size_t> object_index,
        A32LibDlOpenTransactionError& error) const noexcept;
    [[nodiscard]] std::uint32_t acquire_handle(
        std::size_t object_index) noexcept;
    [[nodiscard]] std::vector<std::size_t> live_handle_anchors() const;
    [[nodiscard]] A32LibDlOpenTransactionResult cleanup_failure(
        A32LibDlOpenTransactionResult result,
        std::size_t root_object_index);

    memory::MappedGuestMemory& memory_;
    elf::Elf32LinkMap& link_map_;
    elf::Elf32DependencyProvider& provider_;
    std::span<A32LibDlHandle> handles_;
    elf::Elf32LifecycleState& lifecycle_;
    A32LibDlOpenTransactionOptions options_;
};

[[nodiscard]] const char* to_string(
    A32LibDlOpenTransactionError error) noexcept;

}  // namespace liba32android::compat
