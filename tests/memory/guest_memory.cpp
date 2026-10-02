#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>

#include "memory/guest_memory.h"

namespace {

using liba32android::memory::GuestMemory;
using liba32android::memory::LinearGuestMemory;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

class CallbackGuestMemory final : public GuestMemory {
public:
    CallbackGuestMemory(std::size_t size, std::uint32_t base)
        : memory_{size, base} {}

    bool read(
        std::uint32_t address,
        std::span<std::uint8_t> output) const override {
        return memory_.read(address, output);
    }

    bool write(
        std::uint32_t address,
        std::span<const std::uint8_t> input) override {
        return memory_.write(address, input);
    }

private:
    LinearGuestMemory memory_;
};

int test_linear_bounds() {
    LinearGuestMemory memory{16, 0x1000};

    constexpr std::array<std::uint8_t, 4> value{0x78, 0x56, 0x34, 0x12};
    if (!memory.write(0x100C, value)) {
        return fail("expected in-range write to succeed");
    }

    std::array<std::uint8_t, 4> readback{};
    if (!memory.read(0x100C, readback) || readback != value) {
        return fail("in-range readback mismatch");
    }
    if (memory.write(0x100E, value)) {
        return fail("cross-boundary write unexpectedly succeeded");
    }
    if (memory.read(0x0FFF, readback)) {
        return fail("read before guest base unexpectedly succeeded");
    }
    if (memory.base() != 0x1000 || memory.size() != 16) {
        return fail("guest memory metadata mismatch");
    }
    return 0;
}

int test_linear_bulk_operations() {
    LinearGuestMemory memory{32, 0x1000};
    constexpr std::array<std::uint8_t, 8> original{1,2,3,4,5,6,7,8};
    if (!memory.write(0x1000, original)) {
        return fail("could not stage linear bulk fixture");
    }

    const auto generation_before = memory.code_generation();
    if (!memory.copy_bytes(0x1002, 0x1000, 6)) {
        return fail("linear overlapping copy failed");
    }
    std::array<std::uint8_t, 8> observed{};
    if (!memory.read(0x1000, observed) ||
        observed != std::array<std::uint8_t, 8>{1,2,1,2,3,4,5,6}) {
        return fail("linear overlapping copy did not use move semantics");
    }
    if (!generation_before.has_value() ||
        !memory.code_generation().has_value() ||
        *memory.code_generation() <= *generation_before) {
        return fail("linear bulk write did not advance code generation");
    }

    if (!memory.fill_bytes(0x1000, 0xAA, 2)) {
        return fail("linear bulk fill failed");
    }
    std::array<std::uint8_t, 2> filled{};
    if (!memory.read(0x1000, filled) ||
        filled != std::array<std::uint8_t, 2>{0xAA, 0xAA}) {
        return fail("linear bulk fill produced wrong bytes");
    }

    constexpr std::array<std::uint8_t, 4> lhs{1,2,3,4};
    constexpr std::array<std::uint8_t, 4> rhs{1,2,4,4};
    if (!memory.write(0x1010, lhs) || !memory.write(0x1018, rhs)) {
        return fail("could not stage linear compare fixture");
    }
    std::int32_t result = 0;
    if (!memory.compare_bytes(0x1010, 0x1018, 4, result) || result >= 0) {
        return fail("linear bulk compare sign was wrong");
    }
    std::optional<std::uint32_t> found;
    if (!memory.find_byte(0x1010, 3, 4, found) ||
        found != std::optional<std::uint32_t>{0x1012}) {
        return fail("linear bulk find returned wrong address");
    }

    const std::array<std::uint8_t, 2> sentinel{0xAA, 0xAA};
    if (memory.copy_bytes(0x1000, 0x101E, 4)) {
        return fail("linear invalid bulk source unexpectedly copied");
    }
    if (!memory.read(0x1000, filled) || filled != sentinel) {
        return fail("failed linear bulk copy mutated destination");
    }
    if (!memory.copy_bytes(0xFFFFFFFFU, 0xFFFFFFFFU, 0) ||
        !memory.fill_bytes(0xFFFFFFFFU, 0, 0)) {
        return fail("zero-length linear bulk operation accessed memory");
    }
    return 0;
}

int test_generic_bulk_fallback() {
    CallbackGuestMemory memory{64, 0x2000};
    constexpr std::array<std::uint8_t, 8> original{1,2,3,4,5,6,7,8};
    if (!memory.write(0x2000, original) ||
        !memory.copy_bytes(0x2002, 0x2000, 6)) {
        return fail("generic callback copy fallback failed");
    }

    std::array<std::uint8_t, 8> observed{};
    if (!memory.read(0x2000, observed) ||
        observed != std::array<std::uint8_t, 8>{1,2,1,2,3,4,5,6}) {
        return fail("generic callback copy fallback lost overlap semantics");
    }
    if (!memory.fill_bytes(0x2010, 0x5A, 8)) {
        return fail("generic callback fill fallback failed");
    }
    std::int32_t compare = 0;
    if (!memory.compare_bytes(0x2010, 0x2010, 8, compare) || compare != 0) {
        return fail("generic callback compare fallback failed");
    }
    std::optional<std::uint32_t> found;
    if (!memory.find_byte(0x2010, 0x5A, 8, found) ||
        found != std::optional<std::uint32_t>{0x2010}) {
        return fail("generic callback find fallback failed");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_linear_bounds(); status != 0) return status;
    if (const int status = test_linear_bulk_operations(); status != 0) return status;
    if (const int status = test_generic_bulk_fallback(); status != 0) return status;
    return 0;
}
