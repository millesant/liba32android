#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <span>

#include "compat/a32_libc_integer.h"
#include "compat/a32_signal.h"
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
    std::array<A32SignalActionState, kA32SignalSetBits> actions{};
    std::array<A32SignalWaiter, 2> waiters{};
    A32LibcGuestErrnoState errno_state{0x100U};
    A32SignalService signals{
        std::span{threads},
        std::span{actions},
        std::span{waiters},
        errno_state,
    };

    Fixture() {
        threads[0].pthread_id = 1U;
        threads[0].phase = A32PthreadThreadPhase::Running;
        threads[1].pthread_id = 2U;
        threads[1].phase = A32PthreadThreadPhase::Running;
        threads[2].pthread_id = 3U;
        threads[2].phase = A32PthreadThreadPhase::Running;
        signals.set_current_thread_id(1U);
        write_u32(memory, 0x100U, 0U);
    }

    A32HostServiceDisposition call(
        std::uint32_t svc,
        std::array<std::uint32_t, 16>& regs) {
        std::uint32_t cpsr{};
        return signals.handle(memory, svc, regs, cpsr);
    }
};

int test_sigaction_abi_and_errors() {
    Fixture fixture;
    constexpr std::uint32_t kAction = 0x200U;
    constexpr std::uint32_t kOld = 0x240U;
    constexpr std::uint32_t kQuery = 0x280U;
    constexpr std::uint32_t mask =
        a32_signal_bit(kA32SigPipe) | a32_signal_bit(kA32SigKill);

    if (!write_u32(fixture.memory, kAction + 0U, 0x12345678U) ||
        !write_u32(fixture.memory, kAction + 4U, mask) ||
        !write_u32(fixture.memory, kAction + 8U, 0x40000000U) ||
        !write_u32(fixture.memory, kAction + 12U, 0x87654321U)) {
        return fail("could not stage ARM32 sigaction");
    }

    std::array<std::uint32_t, 16> regs{};
    regs[0] = kA32SigPipe;
    regs[1] = kAction;
    regs[2] = kOld;
    if (fixture.call(kA32SigactionSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("sigaction install failed");
    }

    std::uint32_t value{};
    for (std::uint32_t offset : {0U, 4U, 8U, 12U}) {
        if (!read_u32(fixture.memory, kOld + offset, value) || value != 0U) {
            return fail("sigaction old action was not default zero state");
        }
    }

    regs = {};
    regs[0] = kA32SigPipe;
    regs[2] = kQuery;
    if (fixture.call(kA32SigactionSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        !read_u32(fixture.memory, kQuery + 0U, value) ||
        value != 0x12345678U ||
        !read_u32(fixture.memory, kQuery + 4U, value) ||
        value != a32_signal_bit(kA32SigPipe) ||
        !read_u32(fixture.memory, kQuery + 8U, value) ||
        value != 0x40000000U ||
        !read_u32(fixture.memory, kQuery + 12U, value) ||
        value != 0x87654321U) {
        return fail("sigaction ARM32 16-byte layout roundtrip failed");
    }

    regs = {};
    regs[0] = kA32SigKill;
    regs[1] = kAction;
    if (fixture.call(kA32SigactionSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        std::bit_cast<std::int32_t>(regs[0]) != -1 ||
        !read_u32(fixture.memory, 0x100U, value) ||
        std::bit_cast<std::int32_t>(value) != kA32AndroidEinval) {
        return fail("sigaction SIGKILL did not publish EINVAL");
    }
    return 0;
}

int test_sigpipe_pending_and_wait() {
    Fixture fixture;
    constexpr std::uint32_t kSet = 0x300U;
    constexpr std::uint32_t kOld = 0x304U;
    constexpr std::uint32_t kPending = 0x308U;
    constexpr std::uint32_t kSignalOut = 0x30cU;
    const std::uint32_t pipe_bit = a32_signal_bit(kA32SigPipe);

    if (!write_u32(
            fixture.memory,
            kSet,
            pipe_bit | a32_signal_bit(kA32SigKill)) ||
        !write_u32(fixture.memory, kSignalOut, 0U)) {
        return fail("could not stage signal mask");
    }

    std::array<std::uint32_t, 16> regs{};
    regs[0] = kA32SigBlock;
    regs[1] = kSet;
    regs[2] = kOld;
    if (fixture.call(kA32PthreadSigmaskSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        fixture.threads[0].signal_mask != pipe_bit) {
        return fail("pthread_sigmask block/filter failed");
    }

    regs = {};
    regs[0] = kA32SigPipe;
    if (fixture.call(kA32RaiseSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        fixture.threads[0].signal_pending != pipe_bit) {
        return fail("blocked raise(SIGPIPE) did not become pending");
    }

    regs = {};
    regs[0] = kPending;
    if (fixture.call(kA32SigpendingSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("sigpending failed");
    }
    std::uint32_t value{};
    if (!read_u32(fixture.memory, kPending, value) || value != pipe_bit) {
        return fail("sigpending did not expose blocked SIGPIPE");
    }

    regs = {};
    regs[0] = kSet;
    regs[1] = kSignalOut;
    if (fixture.call(kA32SigwaitSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        !read_u32(fixture.memory, kSignalOut, value) ||
        value != kA32SigPipe ||
        fixture.threads[0].signal_pending != 0U) {
        return fail("sigwait did not consume pending SIGPIPE");
    }

    regs = {};
    regs[0] = kA32SigSetmask;
    regs[1] = kOld;
    if (fixture.call(kA32PthreadSigmaskSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        fixture.threads[0].signal_mask != 0U) {
        return fail("pthread_sigmask restore failed");
    }

    regs = {};
    regs[0] = 99U;
    regs[2] = kOld;
    if (fixture.call(kA32PthreadSigmaskSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("pthread_sigmask NULL set did not ignore how");
    }

    regs = {};
    regs[0] = 99U;
    regs[1] = kSet;
    if (fixture.call(kA32PthreadSigmaskSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        std::bit_cast<std::int32_t>(regs[0]) != kA32AndroidEinval) {
        return fail("pthread_sigmask invalid how did not return EINVAL");
    }
    return 0;
}

int test_cooperative_sigwait_wake() {
    Fixture fixture;
    constexpr std::uint32_t kSet = 0x340U;
    constexpr std::uint32_t kOut = 0x344U;
    const std::uint32_t usr1_bit = a32_signal_bit(10U);
    if (!write_u32(fixture.memory, kSet, usr1_bit) ||
        !write_u32(fixture.memory, kOut, 0U)) {
        return fail("could not stage cooperative sigwait");
    }

    fixture.signals.set_current_thread_id(2U);
    std::array<std::uint32_t, 16> regs{};
    regs[0] = kSet;
    regs[1] = kOut;
    if (fixture.call(kA32SigwaitSvcImmediate, regs) !=
            A32HostServiceDisposition::Suspended) {
        return fail("empty sigwait did not suspend cooperatively");
    }

    if (!fixture.signals.queue_process_signal(fixture.memory, 10U)) {
        return fail("could not queue process signal for waiter");
    }
    const auto wake = fixture.signals.pop_ready();
    std::uint32_t value{};
    if (!wake.has_value() ||
        wake->thread_id != 2U ||
        wake->signal != 10U ||
        wake->result_value != 0U ||
        !read_u32(fixture.memory, kOut, value) ||
        value != 10U) {
        return fail("sigwait wake record was incorrect");
    }
    return 0;
}


int test_pthread_signal_mask_inheritance() {
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
        return fail("could not initialize lifecycle for signal inheritance");
    }

    const std::uint32_t inherited =
        a32_signal_bit(kA32SigPipe) | a32_signal_bit(10U);
    threads[0].signal_mask = inherited;

    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    regs[0] = 0x400U;
    regs[2] = 0x1000U;
    if (lifecycle.handle(
            memory,
            kA32PthreadCreateSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        threads[1].signal_mask != inherited ||
        threads[1].signal_pending != 0U) {
        return fail("pthread_create did not inherit logical signal mask");
    }
    return 0;
}

int test_delivery_boundaries_and_capacity() {
    Fixture fixture;
    std::array<std::uint32_t, 16> regs{};

    regs[0] = kA32SigChld;
    if (fixture.call(kA32RaiseSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("default-ignored SIGCHLD did not return success");
    }

    regs = {};
    regs[0] = kA32SigFpe;
    if (fixture.call(kA32RaiseSvcImmediate, regs) !=
            A32HostServiceDisposition::Failed ||
        fixture.signals.last_boundary_error() !=
            A32SignalBoundaryError::DefaultFatal ||
        fixture.signals.last_boundary_signal() != kA32SigFpe) {
        return fail("raise(SIGFPE) did not expose default-fatal boundary");
    }

    constexpr std::uint32_t kAction = 0x380U;
    if (!write_u32(fixture.memory, kAction + 0U, 0x2000U) ||
        !write_u32(fixture.memory, kAction + 4U, 0U) ||
        !write_u32(fixture.memory, kAction + 8U, 0U) ||
        !write_u32(fixture.memory, kAction + 12U, 0U)) {
        return fail("could not stage custom handler");
    }
    regs = {};
    regs[0] = 10U;
    regs[1] = kAction;
    if (fixture.call(kA32SigactionSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled) {
        return fail("could not install custom logical signal handler");
    }
    regs = {};
    regs[0] = 10U;
    if (fixture.call(kA32RaiseSvcImmediate, regs) !=
            A32HostServiceDisposition::Failed ||
        fixture.signals.last_boundary_error() !=
            A32SignalBoundaryError::CustomHandlerUnsupported) {
        return fail("custom signal handler was silently treated as delivered");
    }

    std::array<A32SignalWaiter, 1> one_waiter{};
    std::array<A32SignalActionState, kA32SignalSetBits> actions{};
    A32LibcGuestErrnoState errno_state{0x100U};
    A32SignalService limited{
        std::span{fixture.threads},
        std::span{actions},
        std::span{one_waiter},
        errno_state,
    };
    constexpr std::uint32_t kSet = 0x3c0U;
    constexpr std::uint32_t kOutOne = 0x3c4U;
    constexpr std::uint32_t kOutTwo = 0x3c8U;
    if (!write_u32(fixture.memory, kSet, a32_signal_bit(12U)) ||
        !write_u32(fixture.memory, kOutOne, 0U) ||
        !write_u32(fixture.memory, kOutTwo, 0U)) {
        return fail("could not stage bounded waiter test");
    }
    limited.set_current_thread_id(1U);
    std::uint32_t cpsr{};
    regs = {};
    regs[0] = kSet;
    regs[1] = kOutOne;
    if (limited.handle(
            fixture.memory,
            kA32SigwaitSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Suspended) {
        return fail("bounded waiter first sigwait did not suspend");
    }
    limited.set_current_thread_id(2U);
    regs = {};
    regs[0] = kSet;
    regs[1] = kOutTwo;
    if (limited.handle(
            fixture.memory,
            kA32SigwaitSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed ||
        limited.last_boundary_error() !=
            A32SignalBoundaryError::WaiterCapacityExceeded) {
        return fail("bounded signal waiter capacity was not enforced");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_sigaction_abi_and_errors(); status != 0) {
        return status;
    }
    if (const int status = test_sigpipe_pending_and_wait(); status != 0) {
        return status;
    }
    if (const int status = test_cooperative_sigwait_wake(); status != 0) {
        return status;
    }
    if (const int status = test_pthread_signal_mask_inheritance(); status != 0) {
        return status;
    }
    if (const int status = test_delivery_boundaries_and_capacity(); status != 0) {
        return status;
    }
    return 0;
}
