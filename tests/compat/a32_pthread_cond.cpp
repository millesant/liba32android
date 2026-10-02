#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>

#include "compat/a32_pthread_sync.h"
#include "cpu/a32_cpu.h"
#include "memory/guest_memory.h"
#include "runtime/a32_logical_thread.h"
#include "runtime/a32_service_dispatch.h"

namespace {

using namespace liba32android::compat;
using liba32android::cpu::ExecutionRequest;
using liba32android::memory::LinearGuestMemory;
using liba32android::runtime::A32HostServiceDisposition;
using liba32android::runtime::A32LogicalThreadId;
using liba32android::runtime::execute_a32_with_services;
using liba32android::runtime::make_a32_service_resume_context;

constexpr std::uint32_t kStopPc = 0x1000U;
constexpr std::uint32_t kCond = 0x200U;
constexpr std::uint32_t kMutex = 0x240U;
constexpr std::uint32_t kTimespec = 0x300U;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

std::int32_t signed_value(std::uint32_t value) {
    return std::bit_cast<std::int32_t>(value);
}

class FakeClock final : public A32PthreadClock {
public:
    std::optional<std::int64_t> realtime_ns{0};
    std::optional<std::int64_t> monotonic_ns{0};

    std::optional<std::int64_t> now_ns(
        A32PthreadClockId clock_id) const noexcept override {
        return clock_id == A32PthreadClockId::Realtime
            ? realtime_ns
            : monotonic_ns;
    }
};

bool write_timespec(
    LinearGuestMemory& memory,
    std::uint32_t address,
    std::int32_t seconds,
    std::int32_t nanoseconds) {
    const std::uint32_t sec = std::bit_cast<std::uint32_t>(seconds);
    const std::uint32_t nsec = std::bit_cast<std::uint32_t>(nanoseconds);
    const std::array<std::uint8_t, 8> bytes{{
        static_cast<std::uint8_t>(sec),
        static_cast<std::uint8_t>(sec >> 8U),
        static_cast<std::uint8_t>(sec >> 16U),
        static_cast<std::uint8_t>(sec >> 24U),
        static_cast<std::uint8_t>(nsec),
        static_cast<std::uint8_t>(nsec >> 8U),
        static_cast<std::uint8_t>(nsec >> 16U),
        static_cast<std::uint8_t>(nsec >> 24U),
    }};
    return memory.write(address, bytes);
}

struct Fixture {
    LinearGuestMemory memory{0x4000U};
    std::array<A32PthreadMutexState, 6> mutexes{};
    std::array<A32SemaphoreState, 2> semaphores{};
    std::array<A32PthreadWaiter, 12> waiters{};
    FakeClock clock{};
    A32PthreadSyncService service{
        std::span{mutexes},
        std::span{semaphores},
        std::span{waiters},
        {},
        {},
        &clock,
    };

    Fixture() {
        service.set_current_thread_id(1U);
    }

    A32HostServiceDisposition call(
        std::uint32_t svc,
        std::array<std::uint32_t, 16>& regs) {
        std::uint32_t cpsr{};
        return service.handle(memory, svc, regs, cpsr);
    }

    bool select(std::uint32_t thread_id) {
        const auto id = A32LogicalThreadId::from_raw(thread_id);
        return id.has_value() && service.set_current_thread_id(*id);
    }

    bool lock(std::uint32_t thread_id, std::uint32_t mutex = kMutex) {
        if (!select(thread_id)) return false;
        std::array<std::uint32_t, 16> regs{};
        regs[0] = mutex;
        return call(kA32PthreadMutexLockSvcImmediate, regs) ==
                   A32HostServiceDisposition::Handled &&
               regs[0] == 0U;
    }

    bool unlock(std::uint32_t thread_id, std::uint32_t mutex = kMutex) {
        if (!select(thread_id)) return false;
        std::array<std::uint32_t, 16> regs{};
        regs[0] = mutex;
        return call(kA32PthreadMutexUnlockSvcImmediate, regs) ==
                   A32HostServiceDisposition::Handled &&
               regs[0] == 0U;
    }
};

int test_wait_signal_reacquires_existing_mutex_queue() {
    Fixture fixture;
    constexpr std::array<std::uint8_t, 12> wait_code{{
        0x12, 0x01, 0x00, 0xef,  // svc #0x112
        0x01, 0x40, 0x84, 0xe2,  // add r4,r4,#1
        0x1e, 0xff, 0x2f, 0xe1,  // bx lr
    }};
    if (!fixture.memory.write(0U, wait_code)) {
        return fail("could not stage condition wait code");
    }

    if (!fixture.lock(1U)) {
        return fail("thread 1 could not acquire condition mutex");
    }

    fixture.select(2U);
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    regs[0] = kMutex;
    if (fixture.service.handle(
            fixture.memory,
            kA32PthreadMutexLockSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Suspended) {
        return fail("thread 2 did not queue on condition mutex");
    }

    fixture.select(1U);
    ExecutionRequest request{};
    request.regs[0] = kCond;
    request.regs[1] = kMutex;
    request.regs[4] = 7U;
    request.regs[14] = kStopPc;
    request.instruction_count = 3U;
    request.stop_pc = kStopPc;
    const auto suspended =
        execute_a32_with_services(fixture.memory, request, fixture.service, 1U);
    if (!suspended || !suspended.service_suspended ||
        !suspended.suspended_svc_immediate.has_value() ||
        *suspended.suspended_svc_immediate != kA32PthreadCondWaitSvcImmediate) {
        return fail("pthread_cond_wait did not suspend");
    }

    const auto mutex_wake = fixture.service.pop_ready();
    if (!mutex_wake.has_value() ||
        mutex_wake->kind != A32PthreadWaitKind::Mutex ||
        mutex_wake->thread_id != 2U) {
        return fail("cond wait did not atomically release mutex to prior waiter");
    }

    fixture.select(2U);
    regs = {};
    regs[0] = kCond;
    if (fixture.call(kA32PthreadCondSignalSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("pthread_cond_signal failed");
    }
    if (fixture.service.pop_ready().has_value()) {
        return fail("cond waiter became runnable before mutex reacquisition");
    }

    if (!fixture.unlock(2U)) {
        return fail("thread 2 could not release condition mutex");
    }
    const auto cond_wake = fixture.service.pop_ready();
    if (!cond_wake.has_value() ||
        cond_wake->kind != A32PthreadWaitKind::Condition ||
        cond_wake->object_address != kCond ||
        cond_wake->mutex_address != kMutex ||
        cond_wake->thread_id != 1U ||
        cond_wake->result_value != 0U) {
        return fail("condition signal did not publish reacquired waiter");
    }

    const auto thread_one = A32LogicalThreadId::from_raw(1U);
    auto resumed = thread_one.has_value()
        ? make_a32_service_resume_context(
              *thread_one, suspended, 2U, kStopPc)
        : std::nullopt;
    if (!resumed.has_value() ||
        !fixture.service.set_current_thread_context(*resumed)) {
        return fail("could not resume signaled condition waiter");
    }
    resumed->request.regs[0] = cond_wake->result_value;
    const auto completed = execute_a32_with_services(
        fixture.memory, resumed->request, fixture.service, 1U);
    if (!completed || !completed.stop_pc_reached ||
        completed.regs[4] != 8U ||
        fixture.mutexes[0].owner_thread_id != 1U) {
        return fail("condition waiter resumed before owning mutex");
    }
    return 0;
}

int test_broadcast_fifo_reacquisition() {
    Fixture fixture;
    std::array<std::uint32_t, 16> regs{};

    for (std::uint32_t thread = 1U; thread <= 2U; ++thread) {
        if (!fixture.lock(thread)) {
            return fail("broadcast waiter could not lock mutex");
        }
        regs = {};
        regs[0] = kCond;
        regs[1] = kMutex;
        if (fixture.call(kA32PthreadCondWaitSvcImmediate, regs) !=
                A32HostServiceDisposition::Suspended) {
            return fail("broadcast waiter did not suspend");
        }
    }

    if (!fixture.lock(3U)) {
        return fail("broadcaster could not lock mutex");
    }
    regs = {};
    regs[0] = kCond;
    if (fixture.call(kA32PthreadCondBroadcastSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled) {
        return fail("pthread_cond_broadcast failed");
    }
    if (fixture.service.pop_ready().has_value()) {
        return fail("broadcast bypassed mutex reacquisition");
    }

    if (!fixture.unlock(3U)) {
        return fail("broadcaster could not unlock mutex");
    }
    const auto first = fixture.service.pop_ready();
    if (!first.has_value() ||
        first->kind != A32PthreadWaitKind::Condition ||
        first->thread_id != 1U) {
        return fail("broadcast did not reacquire oldest waiter first");
    }

    if (!fixture.unlock(1U)) {
        return fail("first broadcast waiter did not own mutex");
    }
    const auto second = fixture.service.pop_ready();
    if (!second.has_value() ||
        second->kind != A32PthreadWaitKind::Condition ||
        second->thread_id != 2U) {
        return fail("broadcast did not reacquire second waiter");
    }

    return 0;
}

int test_timedwait_deadline_validation_and_race() {
    Fixture fixture;
    constexpr std::array<std::uint8_t, 12> wait_code{{
        0x13, 0x01, 0x00, 0xef,  // svc #0x113
        0x01, 0x50, 0x85, 0xe2,  // add r5,r5,#1
        0x1e, 0xff, 0x2f, 0xe1,  // bx lr
    }};
    if (!fixture.memory.write(0U, wait_code)) {
        return fail("could not stage timed condition wait code");
    }

    if (!fixture.lock(1U) ||
        !write_timespec(fixture.memory, kTimespec, 10, 0)) {
        return fail("could not prepare timed wait");
    }
    fixture.clock.realtime_ns = 9'000'000'000LL;

    ExecutionRequest request{};
    request.regs[0] = kCond;
    request.regs[1] = kMutex;
    request.regs[2] = kTimespec;
    request.regs[5] = 11U;
    request.regs[14] = kStopPc;
    request.instruction_count = 3U;
    request.stop_pc = kStopPc;
    const auto suspended =
        execute_a32_with_services(fixture.memory, request, fixture.service, 1U);
    if (!suspended || !suspended.service_suspended ||
        fixture.service.next_condition_deadline_ns() !=
            std::optional<std::int64_t>{10'000'000'000LL}) {
        return fail("future pthread_cond_timedwait did not suspend with deadline");
    }

    fixture.clock.realtime_ns = 10'000'000'000LL;
    if (!fixture.service.poll_condition_timeouts()) {
        return fail("deadline poll failed");
    }
    const auto wake = fixture.service.pop_ready();
    if (!wake.has_value() ||
        wake->kind != A32PthreadWaitKind::Condition ||
        wake->thread_id != 1U ||
        signed_value(wake->result_value) != kA32AndroidEtimedout) {
        return fail("expired timed wait did not publish ETIMEDOUT");
    }

    const auto thread_one = A32LogicalThreadId::from_raw(1U);
    auto resumed = thread_one.has_value()
        ? make_a32_service_resume_context(
              *thread_one, suspended, 2U, kStopPc)
        : std::nullopt;
    if (!resumed.has_value() ||
        !fixture.service.set_current_thread_context(*resumed)) {
        return fail("could not resume timed condition waiter");
    }
    resumed->request.regs[0] = wake->result_value;
    const auto completed = execute_a32_with_services(
        fixture.memory, resumed->request, fixture.service, 1U);
    if (!completed || !completed.stop_pc_reached ||
        completed.regs[5] != 12U ||
        signed_value(completed.regs[0]) != kA32AndroidEtimedout) {
        return fail("timed waiter resumed with wrong return value");
    }

    if (!fixture.unlock(1U) || !fixture.lock(1U) ||
        !write_timespec(fixture.memory, kTimespec, 20, 0)) {
        return fail("could not prepare timeout/signal race");
    }
    fixture.clock.realtime_ns = 19'000'000'000LL;
    regs = {};
    regs[0] = kCond;
    regs[1] = kMutex;
    regs[2] = kTimespec;
    if (fixture.call(kA32PthreadCondTimedwaitSvcImmediate, regs) !=
            A32HostServiceDisposition::Suspended) {
        return fail("race timed wait did not suspend");
    }

    fixture.clock.realtime_ns = 20'000'000'000LL;
    fixture.select(2U);
    regs = {};
    regs[0] = kCond;
    if (fixture.call(kA32PthreadCondSignalSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled) {
        return fail("signal at deadline failed");
    }
    const auto raced = fixture.service.pop_ready();
    if (!raced.has_value() ||
        signed_value(raced->result_value) != kA32AndroidEtimedout) {
        return fail("deadline-vs-signal race did not resolve exactly once");
    }
    if (fixture.service.pop_ready().has_value()) {
        return fail("deadline-vs-signal race produced duplicate wake");
    }

    fixture.select(1U);
    regs = {};
    regs[0] = kMutex;
    if (fixture.call(kA32PthreadMutexUnlockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled) {
        return fail("race waiter did not reacquire mutex");
    }

    if (!fixture.lock(1U) ||
        !write_timespec(fixture.memory, kTimespec, 1, 1'000'000'000)) {
        return fail("could not prepare invalid timespec");
    }
    regs = {};
    regs[0] = kCond;
    regs[1] = kMutex;
    regs[2] = kTimespec;
    if (fixture.call(kA32PthreadCondTimedwaitSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_value(regs[0]) != kA32AndroidEinval ||
        fixture.mutexes[0].owner_thread_id != 1U) {
        return fail("invalid timespec mutated mutex ownership");
    }

    if (!write_timespec(fixture.memory, kTimespec, -1, 0)) {
        return fail("could not prepare negative timespec");
    }
    regs = {};
    regs[0] = kCond;
    regs[1] = kMutex;
    regs[2] = kTimespec;
    if (fixture.call(kA32PthreadCondTimedwaitSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_value(regs[0]) != kA32AndroidEtimedout ||
        fixture.mutexes[0].owner_thread_id != 1U) {
        return fail("negative absolute timeout did not return ETIMEDOUT");
    }

    return 0;
}

int test_destroy_active_waiter_and_capacity_failure() {
    LinearGuestMemory memory{0x2000U};
    std::array<A32PthreadMutexState, 2> mutexes{};
    std::array<A32SemaphoreState, 1> semaphores{};
    std::array<A32PthreadWaiter, 1> waiters{};
    FakeClock clock{};
    A32PthreadSyncService service{
        std::span{mutexes},
        std::span{semaphores},
        std::span{waiters},
        {},
        {},
        &clock,
    };
    service.set_current_thread_id(1U);

    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    regs[0] = kMutex;
    if (service.handle(
            memory,
            kA32PthreadMutexLockSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled) {
        return fail("capacity fixture could not lock first mutex");
    }

    regs = {};
    regs[0] = kCond;
    regs[1] = kMutex;
    if (service.handle(
            memory,
            kA32PthreadCondWaitSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Suspended) {
        return fail("capacity fixture could not fill waiter slot");
    }

    regs = {};
    regs[0] = kCond;
    if (service.handle(
            memory,
            kA32PthreadCondDestroySvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("cond_destroy with active waiter did not match Bionic return");
    }

    service.set_current_thread_id(2U);
    regs = {};
    regs[0] = kMutex + 4U;
    if (service.handle(
            memory,
            kA32PthreadMutexLockSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled) {
        return fail("capacity fixture could not lock second mutex");
    }
    regs = {};
    regs[0] = kCond + 4U;
    regs[1] = kMutex + 4U;
    if (service.handle(
            memory,
            kA32PthreadCondWaitSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("condition waiter capacity exhaustion did not fail");
    }
    if (mutexes[1].owner_thread_id != 2U) {
        return fail("failed condition enqueue released caller mutex");
    }

    return 0;
}

}  // namespace

int main() {
    if (const int status =
            test_wait_signal_reacquires_existing_mutex_queue();
        status != 0) {
        return status;
    }
    if (const int status = test_broadcast_fifo_reacquisition();
        status != 0) {
        return status;
    }
    if (const int status =
            test_timedwait_deadline_validation_and_race();
        status != 0) {
        return status;
    }
    if (const int status =
            test_destroy_active_waiter_and_capacity_failure();
        status != 0) {
        return status;
    }
    return 0;
}
