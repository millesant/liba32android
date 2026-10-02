#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace liba32android::memory {

enum class MemoryPermission : std::uint8_t {
    None = 0,
    Read = 1U << 0,
    Write = 1U << 1,
    Execute = 1U << 2,
};

[[nodiscard]] constexpr MemoryPermission operator|(MemoryPermission lhs, MemoryPermission rhs) noexcept {
    return static_cast<MemoryPermission>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr MemoryPermission operator&(MemoryPermission lhs, MemoryPermission rhs) noexcept {
    return static_cast<MemoryPermission>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr bool has_permission(MemoryPermission permissions, MemoryPermission permission) noexcept {
    return (permissions & permission) != MemoryPermission::None;
}

class GuestMemory {
public:
    virtual ~GuestMemory() = default;

    [[nodiscard]] virtual bool read(std::uint32_t address, std::span<std::uint8_t> output) const = 0;
    [[nodiscard]] virtual bool write(std::uint32_t address, std::span<const std::uint8_t> input) = 0;

    // Instruction fetch is separate from data reads so mapped backends can
    // enforce guest execute permission without leaking engine-specific types.
    [[nodiscard]] virtual bool read_code(std::uint32_t address, std::span<std::uint8_t> output) const {
        return read(address, output);
    }

    // Internal acceleration capability. The returned host pointer, when
    // present, is the beginning of a contiguous 4 GiB host reservation whose
    // byte offsets correspond to logical AArch32 guest virtual addresses.
    // Higher runtime layers must continue to traffic only in guest VAs.
    [[nodiscard]] virtual std::optional<std::uintptr_t> fastmem_base() const noexcept {
        return std::nullopt;
    }

    // Monotonic token for changes that can affect executable bytes or execute
    // eligibility. Backends that cannot provide a token return nullopt; a
    // persistent CPU session then falls back to conservative cache clearing.
    [[nodiscard]] virtual std::optional<std::uint64_t> code_generation() const noexcept {
        return std::nullopt;
    }

    // True when guest writes can reach executable bytes without passing through
    // write(), for example fastmem writes to a writable+executable mapping.
    [[nodiscard]] virtual bool direct_executable_writes_possible() const noexcept {
        return false;
    }
};

// Minimal contiguous guest-address-space implementation used for correctness
// tests. It intentionally makes no assumptions about Android mappings or
// Dynarmic fastmem.
class LinearGuestMemory final : public GuestMemory {
public:
    explicit LinearGuestMemory(std::size_t size, std::uint32_t base = 0);

    [[nodiscard]] bool read(std::uint32_t address, std::span<std::uint8_t> output) const override;
    [[nodiscard]] bool write(std::uint32_t address, std::span<const std::uint8_t> input) override;
    [[nodiscard]] std::optional<std::uint64_t> code_generation() const noexcept override;

    [[nodiscard]] std::uint32_t base() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    [[nodiscard]] bool translate(std::uint32_t address, std::size_t length, std::size_t& offset) const noexcept;

    std::uint32_t base_{};
    std::vector<std::uint8_t> bytes_;
    std::uint64_t code_generation_{};
};

// Sparse 32-bit guest address space backed by one contiguous 4 GiB host
// reservation. Guest pages start inaccessible and are made accessible through
// explicit page-aligned map/protect/unmap operations. The reservation may live
// anywhere in the 64-bit host address space; guest pointer == host pointer is
// never required.
class MappedGuestMemory final : public GuestMemory {
public:
    static constexpr std::uint64_t kAddressSpaceSize = std::uint64_t{1} << 32;

    MappedGuestMemory();
    ~MappedGuestMemory() override;

    MappedGuestMemory(const MappedGuestMemory&) = delete;
    MappedGuestMemory& operator=(const MappedGuestMemory&) = delete;
    MappedGuestMemory(MappedGuestMemory&&) = delete;
    MappedGuestMemory& operator=(MappedGuestMemory&&) = delete;

    [[nodiscard]] bool map(std::uint32_t address, std::size_t length, MemoryPermission permissions);
    [[nodiscard]] bool protect(std::uint32_t address, std::size_t length, MemoryPermission permissions);
    [[nodiscard]] bool unmap(std::uint32_t address, std::size_t length);

    [[nodiscard]] bool read(std::uint32_t address, std::span<std::uint8_t> output) const override;
    [[nodiscard]] bool read_code(std::uint32_t address, std::span<std::uint8_t> output) const override;
    [[nodiscard]] bool write(std::uint32_t address, std::span<const std::uint8_t> input) override;
    [[nodiscard]] std::optional<std::uintptr_t> fastmem_base() const noexcept override;
    [[nodiscard]] std::optional<std::uint64_t> code_generation() const noexcept override;
    [[nodiscard]] bool direct_executable_writes_possible() const noexcept override;

    [[nodiscard]] std::size_t page_size() const noexcept;
    [[nodiscard]] bool is_mapped(std::uint32_t address) const noexcept;
    [[nodiscard]] MemoryPermission permissions(std::uint32_t address) const noexcept;

private:
    [[nodiscard]] bool valid_page_range(std::uint32_t address, std::size_t length) const noexcept;
    [[nodiscard]] bool valid_access(std::uint32_t address, std::size_t length,
                                    MemoryPermission required) const noexcept;
    [[nodiscard]] bool range_has_execute(std::uint32_t address, std::size_t length) const noexcept;
    [[nodiscard]] bool valid_permissions(MemoryPermission permissions) const noexcept;
    [[nodiscard]] int host_protection(MemoryPermission permissions) const noexcept;
    [[nodiscard]] std::size_t page_index(std::uint32_t address) const noexcept;
    [[nodiscard]] void* host_address(std::uint32_t address) const noexcept;

    void* reservation_{};
    std::size_t page_size_{};
    std::vector<std::uint8_t> page_state_;
    std::uint64_t code_generation_{};
    std::size_t writable_executable_pages_{};
};

}  // namespace liba32android::memory
