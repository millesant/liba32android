#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "compat/a32_aeabi_atexit.h"
#include "compat/a32_libdl.h"
#include "elf/elf32_lifecycle.h"
#include "elf/elf32_link_map.h"

namespace liba32android::compat {

struct A32LibDlObjectLifecycleBinding {
    std::size_t object_index{};
    std::uint32_t dso_handle{};
};

struct A32LibDlCloseTransactionOptions {
    std::uint32_t max_fini_array_entries{};
    elf::Elf32FiniExecutionOptions execution;
};

enum class A32LibDlCloseTransactionError : std::uint8_t {
    None = 0,
    InvalidOptions,
    InvalidHandle,
    InvalidObject,
    InvalidBinding,
    InvalidLifecycleState,
    EntryLimitExceeded,
    DecodeFailed,
    FiniArrayExecutionFailed,
    RegisteredFinalizationIncomplete,
    DtFiniExecutionFailed,
};

enum class A32LibDlCloseTransactionOutcome : std::uint8_t {
    None = 0,
    RefcountDecremented,
    ObjectFinalized,
    NodeleteRetained,
};

struct A32LibDlCloseTransactionResult {
    A32LibDlCloseTransactionError error{
        A32LibDlCloseTransactionError::None};
    A32LibDlCloseTransactionOutcome outcome{
        A32LibDlCloseTransactionOutcome::None};
    std::optional<std::size_t> object_index;
    std::size_t fini_calls_completed{};
    elf::Elf32FunctionArrayDecodeError decode_error{
        elf::Elf32FunctionArrayDecodeError::None};
    elf::Elf32FiniExecutionError execution_error{
        elf::Elf32FiniExecutionError::None};
    std::optional<std::uint32_t> failing_svc_immediate;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == A32LibDlCloseTransactionError::None;
    }
};

// When invoked synchronously from a trapped guest dlclose SVC, callers should
// pass the trapped guest r13 as nested_stack_top. This keeps FINI callbacks
// below the active dlclose caller frame instead of restarting at the outer
// harness stack top.
class A32LibDlCloseTransaction final {
public:
    A32LibDlCloseTransaction(
        elf::Elf32LinkMap& link_map,
        std::span<A32LibDlHandle> handles,
        elf::Elf32LifecycleState& lifecycle,
        A32AeabiAtexitService& registrations,
        std::span<const A32LibDlObjectLifecycleBinding> bindings,
        A32LibDlCloseTransactionOptions options) noexcept
        : link_map_(link_map),
          handles_(handles),
          lifecycle_(lifecycle),
          registrations_(registrations),
          bindings_(bindings),
          options_(options) {}

    [[nodiscard]] A32LibDlCloseTransactionResult close(
        memory::GuestMemory& memory,
        std::uint32_t guest_handle,
        std::optional<std::uint32_t> nested_stack_top = std::nullopt);

    // Finalize one exact object without changing synthetic-handle ownership.
    // Complete destructor state is an idempotent success so a higher ownership
    // transaction may safely retry after a later object or reclamation failed.
    [[nodiscard]] A32LibDlCloseTransactionResult finalize_object(
        memory::GuestMemory& memory,
        std::size_t object_index,
        std::optional<std::uint32_t> nested_stack_top = std::nullopt);

    [[nodiscard]] bool matches_state(
        const elf::Elf32LinkMap& link_map,
        std::span<const A32LibDlHandle> handles,
        const elf::Elf32LifecycleState& lifecycle) const noexcept;

    void forget_learned_binding(std::size_t object_index) noexcept {
        registrations_.forget_binding_for_object(object_index);
    }

private:
    [[nodiscard]] bool options_valid() const noexcept;
    [[nodiscard]] std::optional<std::size_t> find_handle(
        std::uint32_t guest_handle) const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> dso_for_object(
        std::size_t object_index,
        bool& ambiguous) const noexcept;
    [[nodiscard]] bool registrations_complete(
        std::uint32_t dso_handle) const noexcept;

    elf::Elf32LinkMap& link_map_;
    std::span<A32LibDlHandle> handles_;
    elf::Elf32LifecycleState& lifecycle_;
    A32AeabiAtexitService& registrations_;
    std::span<const A32LibDlObjectLifecycleBinding> bindings_;
    A32LibDlCloseTransactionOptions options_;
};

[[nodiscard]] const char* to_string(
    A32LibDlCloseTransactionError error) noexcept;

}  // namespace liba32android::compat
