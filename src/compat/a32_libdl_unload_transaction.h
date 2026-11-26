#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "compat/a32_libdl.h"
#include "compat/a32_libdl_close_transaction.h"
#include "elf/elf32_link_map.h"
#include "elf/elf32_link_map_reclamation.h"
#include "elf/elf32_lifecycle.h"
#include "memory/guest_memory.h"

namespace liba32android::compat {

struct A32LibDlUnloadTransactionOptions {
    elf::Elf32LinkMapReclamationOptions planning;
    elf::Elf32LinkMapReleaseOptions reclamation;
};

enum class A32LibDlUnloadTransactionError : std::uint8_t {
    None = 0,
    InvalidOptions,
    InvalidHandle,
    InvalidObject,
    InvalidOwnershipState,
    OwnershipPlanFailed,
    PreexistingUnownedObjects,
    RootReleasePlanFailed,
    FinalizationFailed,
    ReclamationFailed,
};

enum class A32LibDlUnloadTransactionOutcome : std::uint8_t {
    None = 0,
    RefcountDecremented,
    RootReleased,
    ObjectsUnloaded,
    LoadPolicyRetained,
};

struct A32LibDlUnloadTransactionResult {
    A32LibDlUnloadTransactionError error{
        A32LibDlUnloadTransactionError::None};
    A32LibDlUnloadTransactionOutcome outcome{
        A32LibDlUnloadTransactionOutcome::None};
    std::optional<std::size_t> object_index;
    std::optional<std::size_t> failing_object;
    std::vector<std::size_t> teardown_objects;
    std::size_t objects_completed{};
    std::size_t mappings_unmapped{};

    elf::Elf32LinkMapReclamationError planning_error{
        elf::Elf32LinkMapReclamationError::None};
    A32LibDlCloseTransactionError finalization_error{
        A32LibDlCloseTransactionError::None};
    elf::Elf32LinkMapReleaseError reclamation_error{
        elf::Elf32LinkMapReleaseError::None};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == A32LibDlUnloadTransactionError::None;
    }
};

// Final-handle ownership transaction for dynamically rooted objects.
//
// The transaction first proves there are no pre-existing unowned Active
// objects, then computes the exact post-root-release teardown set while
// preserving every other persistent root and live synthetic handle. Lifecycle
// teardown happens before ownership mutation. Physical root release/reclamation
// is attempted only after all selected objects have Complete destructor state.
// The final synthetic handle is cleared only after that physical transaction
// succeeds.
class A32LibDlUnloadTransaction final {
public:
    A32LibDlUnloadTransaction(
        elf::Elf32LinkMap& link_map,
        std::span<A32LibDlHandle> handles,
        elf::Elf32LifecycleState& lifecycle,
        A32LibDlCloseTransaction& finalizer,
        A32LibDlUnloadTransactionOptions options) noexcept
        : link_map_(link_map),
          handles_(handles),
          lifecycle_(lifecycle),
          finalizer_(finalizer),
          options_(options) {}

    [[nodiscard]] A32LibDlUnloadTransactionResult close(
        memory::MappedGuestMemory& memory,
        std::uint32_t guest_handle,
        std::optional<std::uint32_t> nested_stack_top = std::nullopt);

private:
    [[nodiscard]] bool options_valid() const noexcept;
    [[nodiscard]] std::optional<std::size_t> find_handle(
        std::uint32_t guest_handle) const noexcept;
    [[nodiscard]] bool collect_other_live_anchors(
        std::size_t closing_slot,
        std::vector<std::size_t>& anchors,
        std::optional<std::size_t>& failing_object) const;

    elf::Elf32LinkMap& link_map_;
    std::span<A32LibDlHandle> handles_;
    elf::Elf32LifecycleState& lifecycle_;
    A32LibDlCloseTransaction& finalizer_;
    A32LibDlUnloadTransactionOptions options_;
};

[[nodiscard]] const char* to_string(
    A32LibDlUnloadTransactionError error) noexcept;

}  // namespace liba32android::compat
