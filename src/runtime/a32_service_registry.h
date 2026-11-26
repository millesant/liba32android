#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "runtime/a32_service_dispatch.h"

namespace liba32android::runtime {

struct A32HostServiceRegistryEntry {
    std::uint32_t svc_immediate{};
    A32HostServiceHandler* handler{};
};

class A32HostServiceRegistry final : public A32HostServiceHandler {
public:
    // The entry array and handler objects are caller-owned and must outlive the
    // registry. Lookup is exact and scans at most entries.size() elements.
    explicit A32HostServiceRegistry(
        std::span<const A32HostServiceRegistryEntry> entries) noexcept
        : entries_(entries) {}

    [[nodiscard]] A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;

    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }

private:
    std::span<const A32HostServiceRegistryEntry> entries_;
};

}  // namespace liba32android::runtime
