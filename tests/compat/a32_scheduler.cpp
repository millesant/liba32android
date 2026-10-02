#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <span>

#include "compat/a32_libc_integer.h"
#include "compat/a32_scheduler.h"
#include "memory/guest_memory.h"

namespace {

using namespace liba32android::compat;
using liba32android::memory::LinearGuestMemory;
using liba32android::runtime::A32HostServiceDisposition;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

bool write_u32(
    LinearGuestMemory& memory,
    std::uint32_t address,
    std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U),
    }};
    return memory.write(address, bytes);
}

bool read_u32(
    const LinearGuestMemory& memory,
    std::uint32_t address,
    std::uint32_t& value) {
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) return false;
    value = static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) |
            (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return true;
}

struct Fixture {
    LinearGuestMemory memory{0x10000U};
    std::array<A32PthreadThreadState, 3> threads{};
    A32LibcGuestErrnoState errno_state{0x100U};
    A32SchedulerService scheduler{
        std::span{threads},
        errno_state,
        A32SchedulerOptions{.logical_cpu_count = 1U},
    };

    Fixture() {
        threads[0].pthread_id = 1U;
        threads[0].phase = A32PthreadThreadPhase::Running;
        threads[1].pthread_id = 2U;
        threads[1].phase = A32PthreadThreadPhase::Running;
        scheduler.set_current_thread_id(1U);
        write_u32(memory, 0x100U, 0U);
    }

    A32HostServiceDisposition call(
        std::uint32_t svc,
        std::array<std::uint32_t, 16>& regs) {
        std::uint32_t cpsr{};
        return scheduler.handle(memory, svc, regs, cpsr);
    }
};

int test_priority_queries() {
    Fixture fixture;
    std::array<std::uint32_t, 16> regs{};

    regs[0] = std::bit_cast<std::uint32_t>(kA32SchedOther);
    if (fixture.call(kA32SchedGetPriorityMaxSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        std::bit_cast<std::int32_t>(regs[0]) != 0) {
        return fail("SCHED_OTHER max priority mismatch");
    }

    regs = {};
    regs[0] = std::bit_cast<std::uint32_t>(kA32SchedRr);
    if (fixture.call(kA32SchedGetPriorityMaxSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        std::bit_cast<std::int32_t>(regs[0]) != 99) {
        return fail("SCHED_RR max priority mismatch");
    }

    regs = {};
    regs[0] = std::bit_cast<std::uint32_t>(kA32SchedFifo);
    if (fixture.call(kA32SchedGetPriorityMinSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        std::bit_cast<std::int32_t>(regs[0]) != 1) {
        return fail("SCHED_FIFO min priority mismatch");
    }

    regs = {};
    regs[0] = 99U;
    if (fixture.call(kA32SchedGetPriorityMinSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        std::bit_cast<std::int32_t>(regs[0]) != -1) {
        return fail("invalid scheduling policy did not fail");
    }
    std::uint32_t errno_word{};
    if (!read_u32(fixture.memory, 0x100U, errno_word) ||
        std::bit_cast<std::int32_t>(errno_word) != kA32AndroidEinval) {
        return fail("invalid priority policy did not publish EINVAL");
    }
    return 0;
}

int test_affinity_query() {
    Fixture fixture;
    constexpr std::uint32_t kMask = 0x200U;
    std::array<std::uint32_t, 16> regs{};
    regs[0] = 0U;
    regs[1] = kA32CpuSetBytes;
    regs[2] = kMask;
    if (fixture.call(kA32SchedGetaffinitySvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("sched_getaffinity current thread failed");
    }
    std::uint32_t mask{};
    if (!read_u32(fixture.memory, kMask, mask) || mask != 1U) {
        return fail("sched_getaffinity did not expose one logical CPU");
    }

    regs = {};
    regs[0] = 1U;
    regs[1] = kA32CpuSetBytes;
    regs[2] = kMask;
    if (fixture.call(kA32SchedGetaffinitySvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        std::bit_cast<std::int32_t>(regs[0]) != -1) {
        return fail("nonzero affinity pid did not fail");
    }

    regs = {};
    regs[0] = 0U;
    regs[1] = 0U;
    regs[2] = kMask;
    if (fixture.call(kA32SchedGetaffinitySvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        std::bit_cast<std::int32_t>(regs[0]) != -1) {
        return fail("short affinity mask did not fail");
    }

    regs = {};
    regs[0] = 0U;
    regs[1] = kA32CpuSetBytes;
    regs[2] = 0U;
    if (fixture.call(kA32SchedGetaffinitySvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        std::bit_cast<std::int32_t>(regs[0]) != -1) {
        return fail("null affinity mask did not fail");
    }
    return 0;
}

int test_scheduler_mutation() {
    Fixture fixture;
    constexpr std::uint32_t kParam = 0x300U;
    if (!write_u32(fixture.memory, kParam, 0U)) {
        return fail("could not stage sched_param");
    }

    std::array<std::uint32_t, 16> regs{};
    regs[0] = 0U;
    regs[1] = std::bit_cast<std::uint32_t>(kA32SchedOther);
    regs[2] = kParam;
    if (fixture.call(kA32SchedSetschedulerSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        fixture.threads[0].sched_policy != kA32SchedOther ||
        fixture.threads[0].sched_priority != 0) {
        return fail("SCHED_OTHER/0 mutation failed");
    }

    if (!write_u32(fixture.memory, kParam, 1U)) {
        return fail("could not stage realtime sched_param");
    }
    regs = {};
    regs[0] = 0U;
    regs[1] = std::bit_cast<std::uint32_t>(kA32SchedRr);
    regs[2] = kParam;
    if (fixture.call(kA32SchedSetschedulerSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        std::bit_cast<std::int32_t>(regs[0]) != -1) {
        return fail("realtime scheduler mutation did not fail");
    }
    std::uint32_t errno_word{};
    if (!read_u32(fixture.memory, 0x100U, errno_word) ||
        std::bit_cast<std::int32_t>(errno_word) != kA32AndroidEperm) {
        return fail("realtime scheduler mutation did not publish EPERM");
    }

    regs = {};
    regs[0] = 1U;
    regs[1] = std::bit_cast<std::uint32_t>(kA32SchedOther);
    regs[2] = kParam;
    if (fixture.call(kA32SchedSetschedulerSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        std::bit_cast<std::int32_t>(regs[0]) != -1) {
        return fail("nonzero scheduler pid did not fail");
    }

    regs = {};
    regs[0] = 0U;
    regs[1] = std::bit_cast<std::uint32_t>(kA32SchedOther);
    regs[2] = 0U;
    if (fixture.call(kA32SchedSetschedulerSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        std::bit_cast<std::int32_t>(regs[0]) != -1) {
        return fail("null sched_param did not fail");
    }
    return 0;
}

int test_setpriority_and_inheritance() {
    Fixture fixture;
    std::array<std::uint32_t, 16> regs{};
    regs[0] = std::bit_cast<std::uint32_t>(kA32PrioProcess);
    regs[1] = 0U;
    regs[2] = 10U;
    if (fixture.call(kA32SetprioritySvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        fixture.threads[0].nice_value != 10) {
        return fail("setpriority logical nice increase failed");
    }

    regs = {};
    regs[0] = std::bit_cast<std::uint32_t>(kA32PrioProcess);
    regs[1] = 0U;
    regs[2] = 0U;
    if (fixture.call(kA32SetprioritySvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        std::bit_cast<std::int32_t>(regs[0]) != -1 ||
        fixture.threads[0].nice_value != 10) {
        return fail("setpriority privilege boundary failed");
    }

    regs = {};
    regs[0] = std::bit_cast<std::uint32_t>(kA32PrioProcess);
    regs[1] = 0U;
    regs[2] = 99U;
    if (fixture.call(kA32SetprioritySvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        fixture.threads[0].nice_value != kA32NiceMax) {
        return fail("setpriority clamp failed");
    }

    LinearGuestMemory memory{0x10000U};
    std::array<A32PthreadAttrState, 1> attrs{};
    std::array<A32PthreadThreadState, 2> threads{};
    A32PthreadLifecycleService lifecycle{
        A32PthreadLifecycleOptions{
            .stack_arena_base = 0x8000U,
            .stack_arena_size = 0x2000U,
            .page_size = 0x1000U,
            .default_stack_size = 0x2000U,
            .exit_trampoline = 0x2000U,
            .thread_instruction_budget = 16U,
            .first_thread_id = 2U,
        },
        std::span{attrs},
        std::span{threads},
    };
    const auto root = liba32android::runtime::A32LogicalThreadId::from_raw(1U);
    if (!root.has_value() ||
        !lifecycle.register_initial_thread(*root) ||
        !lifecycle.set_current_thread_id(*root)) {
        return fail("could not initialize lifecycle nice inheritance");
    }
    threads[0].nice_value = 12;

    std::uint32_t cpsr{};
    regs = {};
    regs[0] = 0x400U;
    regs[2] = 0x3000U;
    if (lifecycle.handle(
            memory,
            kA32PthreadCreateSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        threads[1].nice_value != 12) {
        return fail("pthread_create did not inherit logical nice value");
    }
    return 0;
}

int test_cooperative_yield() {
    Fixture fixture;
    std::array<std::uint32_t, 16> regs{};
    if (fixture.call(kA32SchedYieldSvcImmediate, regs) !=
            A32HostServiceDisposition::Suspended ||
        regs[0] != 0U) {
        return fail("sched_yield did not suspend cooperatively");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_priority_queries(); status != 0) {
        return status;
    }
    if (const int status = test_affinity_query(); status != 0) {
        return status;
    }
    if (const int status = test_scheduler_mutation(); status != 0) {
        return status;
    }
    if (const int status = test_setpriority_and_inheritance(); status != 0) {
        return status;
    }
    if (const int status = test_cooperative_yield(); status != 0) {
        return status;
    }
    return 0;
}
