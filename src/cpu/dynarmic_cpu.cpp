#include "cpu/a32_cpu.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/interface/A32/arch_version.h"
#include "dynarmic/interface/A32/config.h"
#include "memory/guest_memory.h"

namespace liba32android::cpu {
namespace {

struct CodeInvalidationRange {
    std::uint32_t address{};
    std::size_t length{};
};

class Environment final : public Dynarmic::A32::UserCallbacks {
public:
    explicit Environment(memory::GuestMemory& memory) : memory_{memory} {}

    void begin_execution() noexcept {
        exception_raised_ = false;
        memory_fault_ = false;
        svc_immediate_.reset();
        code_read_callbacks_ = 0;
        data_read_callbacks_ = 0;
        data_write_callbacks_ = 0;
        pending_code_invalidation_.reset();
    }

    std::optional<std::uint32_t> MemoryReadCode(std::uint32_t vaddr) override {
        ++code_read_callbacks_;
        std::array<std::uint8_t, 4> bytes{};
        if (!memory_.read_code(vaddr, std::span<std::uint8_t>{bytes})) {
            memory_fault_ = true;
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(bytes[0]) |
               static_cast<std::uint32_t>(bytes[1]) << 8 |
               static_cast<std::uint32_t>(bytes[2]) << 16 |
               static_cast<std::uint32_t>(bytes[3]) << 24;
    }

    std::uint8_t MemoryRead8(std::uint32_t vaddr) override {
        std::array<std::uint8_t, 1> bytes{};
        if (!read_bytes(vaddr, bytes)) return 0;
        return bytes[0];
    }

    std::uint16_t MemoryRead16(std::uint32_t vaddr) override {
        std::array<std::uint8_t, 2> bytes{};
        if (!read_bytes(vaddr, bytes)) return 0;
        return static_cast<std::uint16_t>(bytes[0]) |
               static_cast<std::uint16_t>(bytes[1]) << 8;
    }

    std::uint32_t MemoryRead32(std::uint32_t vaddr) override {
        std::array<std::uint8_t, 4> bytes{};
        if (!read_bytes(vaddr, bytes)) return 0;
        return static_cast<std::uint32_t>(bytes[0]) |
               static_cast<std::uint32_t>(bytes[1]) << 8 |
               static_cast<std::uint32_t>(bytes[2]) << 16 |
               static_cast<std::uint32_t>(bytes[3]) << 24;
    }

    std::uint64_t MemoryRead64(std::uint32_t vaddr) override {
        std::array<std::uint8_t, 8> bytes{};
        if (!read_bytes(vaddr, bytes)) return 0;
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8);
        }
        return value;
    }

    void MemoryWrite8(std::uint32_t vaddr, std::uint8_t value) override {
        const std::array<std::uint8_t, 1> bytes{value};
        write_bytes(vaddr, bytes);
    }

    void MemoryWrite16(std::uint32_t vaddr, std::uint16_t value) override {
        const std::array<std::uint8_t, 2> bytes{
            static_cast<std::uint8_t>(value),
            static_cast<std::uint8_t>(value >> 8),
        };
        write_bytes(vaddr, bytes);
    }

    void MemoryWrite32(std::uint32_t vaddr, std::uint32_t value) override {
        const std::array<std::uint8_t, 4> bytes{
            static_cast<std::uint8_t>(value),
            static_cast<std::uint8_t>(value >> 8),
            static_cast<std::uint8_t>(value >> 16),
            static_cast<std::uint8_t>(value >> 24),
        };
        write_bytes(vaddr, bytes);
    }

    void MemoryWrite64(std::uint32_t vaddr, std::uint64_t value) override {
        std::array<std::uint8_t, 8> bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::uint8_t>(value >> (index * 8));
        }
        write_bytes(vaddr, bytes);
    }

    void InterpreterFallback(std::uint32_t, std::size_t) override {
        exception_raised_ = true;
    }

    void CallSVC(std::uint32_t immediate) override {
        svc_immediate_ = immediate;
        exception_raised_ = true;
    }

    void ExceptionRaised(std::uint32_t, Dynarmic::A32::Exception) override {
        exception_raised_ = true;
    }

    void AddTicks(std::uint64_t) override {}

    std::uint64_t GetTicksRemaining() override {
        return 1;
    }

    [[nodiscard]] bool exception_raised() const noexcept { return exception_raised_; }
    [[nodiscard]] bool memory_fault() const noexcept { return memory_fault_; }
    [[nodiscard]] std::optional<std::uint32_t> svc_immediate() const noexcept { return svc_immediate_; }
    [[nodiscard]] std::size_t code_read_callbacks() const noexcept { return code_read_callbacks_; }
    [[nodiscard]] std::size_t data_read_callbacks() const noexcept { return data_read_callbacks_; }
    [[nodiscard]] std::size_t data_write_callbacks() const noexcept { return data_write_callbacks_; }

    [[nodiscard]] std::optional<CodeInvalidationRange> take_code_invalidation() noexcept {
        auto result = pending_code_invalidation_;
        pending_code_invalidation_.reset();
        return result;
    }

private:
    template <std::size_t Size>
    [[nodiscard]] bool read_bytes(
        std::uint32_t vaddr,
        std::array<std::uint8_t, Size>& bytes) {
        ++data_read_callbacks_;
        if (!memory_.read(vaddr, std::span<std::uint8_t>{bytes})) {
            memory_fault_ = true;
            return false;
        }
        return true;
    }

    template <std::size_t Size>
    void write_bytes(
        std::uint32_t vaddr,
        const std::array<std::uint8_t, Size>& bytes) {
        ++data_write_callbacks_;
        const auto generation_before = memory_.code_generation();
        if (!memory_.write(vaddr, std::span<const std::uint8_t>{bytes})) {
            memory_fault_ = true;
            return;
        }
        const auto generation_after = memory_.code_generation();
        if (!generation_before.has_value() ||
            !generation_after.has_value() ||
            *generation_before != *generation_after) {
            record_code_invalidation(vaddr, Size);
        }
    }

    void record_code_invalidation(std::uint32_t address, std::size_t length) noexcept {
        if (!pending_code_invalidation_.has_value()) {
            pending_code_invalidation_ = CodeInvalidationRange{.address = address, .length = length};
            return;
        }
        const std::uint64_t old_start = pending_code_invalidation_->address;
        const std::uint64_t old_end = old_start + pending_code_invalidation_->length;
        const std::uint64_t new_start = address;
        const std::uint64_t new_end = new_start + length;
        const std::uint64_t merged_start = std::min(old_start, new_start);
        const std::uint64_t merged_end = std::max(old_end, new_end);
        pending_code_invalidation_->address = static_cast<std::uint32_t>(merged_start);
        pending_code_invalidation_->length =
            static_cast<std::size_t>(merged_end - merged_start);
    }

    memory::GuestMemory& memory_;
    bool exception_raised_ = false;
    bool memory_fault_ = false;
    std::optional<std::uint32_t> svc_immediate_;
    std::size_t code_read_callbacks_ = 0;
    std::size_t data_read_callbacks_ = 0;
    std::size_t data_write_callbacks_ = 0;
    std::optional<CodeInvalidationRange> pending_code_invalidation_;
};

[[nodiscard]] Dynarmic::A32::UserConfig make_config(
    Environment& environment,
    memory::GuestMemory& memory) {
    Dynarmic::A32::UserConfig config{};
    config.callbacks = &environment;
    config.arch_version = Dynarmic::A32::ArchVersion::v7;
    config.code_cache_size = 8 * 1024 * 1024;
    config.always_little_endian = true;

    const auto fastmem = memory.fastmem_base();
    if (fastmem.has_value()) {
        config.fastmem_pointer = *fastmem;
        config.recompile_on_fastmem_failure = true;
    }
    return config;
}

}  // namespace

struct A32Executor::Impl {
    explicit Impl(memory::GuestMemory& memory)
        : memory_{memory},
          environment_{memory},
          fastmem_enabled_{memory.fastmem_base().has_value()},
          jit_{make_config(environment_, memory_)},
          observed_code_generation_{memory_.code_generation()} {}

    [[nodiscard]] ExecutionResult execute(const ExecutionRequest& request) {
        const bool jit_instance_reused = execution_count_ != 0;

        const auto current_generation = memory_.code_generation();
        if (execution_count_ != 0 &&
            (!current_generation.has_value() ||
             !observed_code_generation_.has_value() ||
             *current_generation != *observed_code_generation_)) {
            jit_.ClearCache();
            ++code_cache_clears_;
        }
        observed_code_generation_ = current_generation;

        environment_.begin_execution();
        // Keep translated code but discard per-core exclusives from the prior
        // bounded slice/context, matching the old fresh-JIT behavior.
        jit_.ClearExclusiveState();
        jit_.Regs() = request.regs;
        jit_.ExtRegs().fill(0);
        jit_.Regs()[15] = request.entry_pc;

        const std::uint32_t default_cpsr =
            request.instruction_set == InstructionSet::Thumb ? 0x30u : 0x10u;
        jit_.SetCpsr(request.initial_cpsr.value_or(default_cpsr));

        std::size_t executed = 0;
        bool stop_pc_reached =
            request.stop_pc.has_value() && jit_.Regs()[15] == *request.stop_pc;

        while (executed < request.instruction_count &&
               !stop_pc_reached &&
               !environment_.exception_raised() &&
               !environment_.memory_fault()) {
            static_cast<void>(jit_.Step());
            ++executed;

            if (const auto changed = environment_.take_code_invalidation();
                changed.has_value()) {
                jit_.InvalidateCacheRange(changed->address, changed->length);
                ++code_cache_invalidations_;
                observed_code_generation_ = memory_.code_generation();
            }

            // Fastmem stores do not call GuestMemory::write(). If any mapping
            // is simultaneously writable and executable, conservatively clear
            // between exact steps so self-modifying code cannot stay stale.
            if (memory_.direct_executable_writes_possible()) {
                jit_.ClearCache();
                ++code_cache_clears_;
                observed_code_generation_ = memory_.code_generation();
            }

            stop_pc_reached =
                !environment_.exception_raised() &&
                !environment_.memory_fault() &&
                request.stop_pc.has_value() &&
                jit_.Regs()[15] == *request.stop_pc;
        }

        ++execution_count_;
        return ExecutionResult{
            .regs = jit_.Regs(),
            .cpsr = jit_.Cpsr(),
            .instructions_executed = executed,
            .exception_raised = environment_.exception_raised(),
            .memory_fault = environment_.memory_fault(),
            .stop_pc_reached = stop_pc_reached,
            .svc_immediate = environment_.svc_immediate(),
            .fastmem_enabled = fastmem_enabled_,
            .code_read_callbacks = environment_.code_read_callbacks(),
            .data_read_callbacks = environment_.data_read_callbacks(),
            .data_write_callbacks = environment_.data_write_callbacks(),
            .jit_instance_reused = jit_instance_reused,
            .code_cache_clears = code_cache_clears_,
            .code_cache_invalidations = code_cache_invalidations_,
        };
    }

    memory::GuestMemory& memory_;
    Environment environment_;
    bool fastmem_enabled_{};
    Dynarmic::A32::Jit jit_;
    std::optional<std::uint64_t> observed_code_generation_;
    std::size_t execution_count_{};
    std::size_t code_cache_clears_{};
    std::size_t code_cache_invalidations_{};
};

A32Executor::A32Executor(memory::GuestMemory& memory)
    : impl_{std::make_unique<Impl>(memory)} {}
A32Executor::~A32Executor() = default;

ExecutionResult A32Executor::execute(const ExecutionRequest& request) {
    return impl_->execute(request);
}
memory::GuestMemory& A32Executor::memory() noexcept { return impl_->memory_; }
const memory::GuestMemory& A32Executor::memory() const noexcept { return impl_->memory_; }
std::size_t A32Executor::execution_count() const noexcept { return impl_->execution_count_; }
std::size_t A32Executor::code_cache_clear_count() const noexcept {
    return impl_->code_cache_clears_;
}
std::size_t A32Executor::code_cache_invalidation_count() const noexcept {
    return impl_->code_cache_invalidations_;
}

ExecutionResult execute(memory::GuestMemory& memory, const ExecutionRequest& request) {
    A32Executor executor{memory};
    return executor.execute(request);
}

}  // namespace liba32android::cpu
