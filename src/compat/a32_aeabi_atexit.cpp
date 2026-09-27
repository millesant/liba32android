#include "compat/a32_aeabi_atexit.h"

#include <array>
#include <cstdint>

#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

runtime::A32HostServiceDisposition A32AeabiAtexitService::handle(
    memory::GuestMemory&,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    if (svc_immediate != kA32AeabiAtexitSvcImmediate) {
        return runtime::A32HostServiceDisposition::Unhandled;
    }

    if (record_count_ >= records_.size()) {
        regs[0] = 0xffffffffU;
        return runtime::A32HostServiceDisposition::Handled;
    }

    records_[record_count_] = A32AeabiAtexitRecord{
        .object = regs[0],
        .destructor = regs[1],
        .dso_handle = regs[2],
    };
    ++record_count_;
    regs[0] = 0U;
    return runtime::A32HostServiceDisposition::Handled;
}

}  // namespace liba32android::compat
