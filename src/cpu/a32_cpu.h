#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace liba32android::memory {
class GuestMemory;
}

namespace liba32android::cpu {

enum class InstructionSet {
    Arm,
    Thumb,
};

struct ExecutionRequest {
    InstructionSet instruction_set{InstructionSet::Arm};
    std::uint32_t entry_pc{};
    std::array<std::uint32_t, 16> regs{};
    std::size_t instruction_count{1};
    // Optional normalized logical guest PC. When reached, execution stops
    // before fetching/executing an instruction at this address.
    std::optional<std::uint32_t> stop_pc;
    // Optional exact initial CPSR snapshot for resumable execution. When
    // absent, execution derives ARMv7 user-mode CPSR from instruction_set.
    std::optional<std::uint32_t> initial_cpsr;
};

struct ExecutionResult {
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    std::size_t instructions_executed{};
    bool exception_raised{};
    bool memory_fault{};
    bool stop_pc_reached{};
    // Present when execution terminated through the A32 SVC callback. The
    // existing exception_raised flag remains true for backward compatibility.
    std::optional<std::uint32_t> svc_immediate;

    // Internal diagnostics used to prove which CPU/memory path was configured
    // and whether a fastmem fault fell back to callbacks. These fields are not
    // a public runtime ABI.
    bool fastmem_enabled{};
    std::size_t code_read_callbacks{};
    std::size_t data_read_callbacks{};
    std::size_t data_write_callbacks{};
    bool jit_instance_reused{};
    std::size_t code_cache_clears{};
    std::size_t code_cache_invalidations{};
};

// Reusable engine-independent A32 execution session. Dynarmic remains hidden
// behind the implementation; callers may keep this object alive across bounded
// execution slices to retain translated code without owning engine types.
class A32Executor final {
public:
    explicit A32Executor(memory::GuestMemory& memory);
    ~A32Executor();

    A32Executor(const A32Executor&) = delete;
    A32Executor& operator=(const A32Executor&) = delete;
    A32Executor(A32Executor&&) = delete;
    A32Executor& operator=(A32Executor&&) = delete;

    [[nodiscard]] ExecutionResult execute(const ExecutionRequest& request);
    [[nodiscard]] memory::GuestMemory& memory() noexcept;
    [[nodiscard]] const memory::GuestMemory& memory() const noexcept;
    [[nodiscard]] std::size_t execution_count() const noexcept;
    [[nodiscard]] std::size_t code_cache_clear_count() const noexcept;
    [[nodiscard]] std::size_t code_cache_invalidation_count() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// One-shot compatibility seam. Prefer A32Executor when multiple slices share a
// guest address space so translated code can be retained safely.
ExecutionResult execute(memory::GuestMemory& memory, const ExecutionRequest& request);

}  // namespace liba32android::cpu
