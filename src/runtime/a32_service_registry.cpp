#include "runtime/a32_service_registry.h"

#include <array>
#include <cstdint>

#include "memory/guest_memory.h"

namespace liba32android::runtime {

A32HostServiceDisposition A32HostServiceRegistry::handle(
    memory::GuestMemory& memory,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t& cpsr) {
    A32HostServiceHandler* match = nullptr;

    for (const A32HostServiceRegistryEntry& entry : entries_) {
        if (entry.svc_immediate != svc_immediate) {
            continue;
        }

        // A matching null entry or more than one matching entry is ambiguous
        // configuration. Fail before invoking any child handler.
        if (entry.handler == nullptr || match != nullptr ||
            entry.handler == this) {
            return A32HostServiceDisposition::Failed;
        }
        match = entry.handler;
    }

    if (match == nullptr) {
        return A32HostServiceDisposition::Unhandled;
    }

    return match->handle(memory, svc_immediate, regs, cpsr);
}

}  // namespace liba32android::runtime
