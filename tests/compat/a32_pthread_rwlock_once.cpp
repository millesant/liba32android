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
using liba32android::runtime::A32ServiceDispatchError;
using liba32android::runtime::execute_a32_with_services;
using liba32android::runtime::make_a32_service_resume_context;

constexpr std::uint32_t kOnceComplete = 0x1000U;
constexpr std::uint32_t kOnceProgram = 0x0000U;
constexpr std::uint32_t kOnceInitializer = 0x0200U;
constexpr std::uint32_t kStopPc = 0x5000U;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

std::int32_t signed_value(std::uint32_t value) {
    return std::bit_cast<std::int32_t>(value);
}

struct Fixture {
    LinearGuestMemory memory{0x5000U};
    std::array<A32PthreadMutexState, 8> mutexes{};
    std::array<A32SemaphoreState, 4> semaphores{};
    std::array<A32PthreadWaiter, 16> waiters{};
    std::array<A32PthreadMutexAttrState, 4> mutex_attrs{};
    std::array<A32PthreadRwlockState, 4> rwlocks{};
    std::array<A32PthreadOnceState, 4> once_controls{};
    A32PthreadSyncService service{
        std::span{mutexes},
        std::span{semaphores},
        std::span{waiters},
        {},
        {},
        nullptr,
        std::span{mutex_attrs},
        std::span{rwlocks},
        std::span{once_controls},
        A32PthreadSyncOptions{
            .once_completion_trampoline = kOnceComplete,
        },
    };

    Fixture() {
        constexpr std::array<std::uint8_t, 8> complete_code{{
            0x1a, 0x01, 0x00, 0xef,  // svc #0x11a
            0x1e, 0xff, 0x2f, 0xe1,  // bx lr (completion redirects before this)
        }};
        memory.write(kOnceComplete, complete_code);
        service.set_current_thread_id(1U);
    }

    A32HostServiceDisposition call(
        std::uint32_t svc,
        std::array<std::uint32_t, 16>& regs) {
        std::uint32_t cpsr{0x10U};
        return service.handle(memory, svc, regs, cpsr);
    }

    bool select(std::uint32_t thread_id) {
        const auto id = A32LogicalThreadId::from_raw(thread_id);
        return id.has_value() && service.set_current_thread_id(*id);
    }
};

int test_mutex_types() {
    Fixture fixture;
    constexpr std::uint32_t kAttr = 0x400U;
    constexpr std::uint32_t kRecursiveMutex = 0x500U;
    constexpr std::uint32_t kErrorcheckMutex = 0x540U;
    constexpr std::uint32_t kDefaultMutex = 0x580U;

    std::array<std::uint32_t, 16> regs{};
    regs[0] = kAttr;
    if (fixture.call(kA32PthreadMutexattrInitSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("pthread_mutexattr_init failed");
    }

    regs = {};
    regs[0] = kAttr;
    regs[1] = 99U;
    if (fixture.call(kA32PthreadMutexattrSettypeSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_value(regs[0]) != kA32AndroidEinval) {
        return fail("pthread_mutexattr_settype accepted invalid type");
    }

    regs = {};
    regs[0] = kAttr;
    regs[1] = kA32PthreadMutexRecursive;
    if (fixture.call(kA32PthreadMutexattrSettypeSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("could not select recursive mutex type");
    }

    regs = {};
    regs[0] = kRecursiveMutex;
    regs[1] = kAttr;
    if (fixture.call(kA32PthreadMutexInitSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("recursive pthread_mutex_init failed");
    }

    for (int i = 0; i < 2; ++i) {
        regs = {};
        regs[0] = kRecursiveMutex;
        if (fixture.call(kA32PthreadMutexLockSvcImmediate, regs) !=
                A32HostServiceDisposition::Handled ||
            regs[0] != 0U) {
            return fail("recursive pthread_mutex_lock failed");
        }
    }
    regs = {};
    regs[0] = kRecursiveMutex;
    if (fixture.call(kA32PthreadMutexTrylockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        fixture.mutexes[0].recursion_depth != 3U) {
        return fail("recursive pthread_mutex_trylock did not increment depth");
    }

    fixture.select(2U);
    regs = {};
    regs[0] = kRecursiveMutex;
    if (fixture.call(kA32PthreadMutexUnlockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_value(regs[0]) != kA32AndroidEperm) {
        return fail("non-owner recursive unlock did not return EPERM");
    }

    fixture.select(1U);
    for (int i = 0; i < 3; ++i) {
        regs = {};
        regs[0] = kRecursiveMutex;
        if (fixture.call(kA32PthreadMutexUnlockSvcImmediate, regs) !=
                A32HostServiceDisposition::Handled ||
            regs[0] != 0U) {
            return fail("recursive pthread_mutex_unlock failed");
        }
    }
    if (fixture.mutexes[0].owner_thread_id != 0U ||
        fixture.mutexes[0].recursion_depth != 0U) {
        return fail("recursive mutex did not fully release at depth zero");
    }

    regs = {};
    regs[0] = kRecursiveMutex;
    if (fixture.call(kA32PthreadMutexDestroySvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("recursive mutex destroy failed");
    }

    regs = {};
    regs[0] = kAttr;
    regs[1] = kA32PthreadMutexErrorcheck;
    if (fixture.call(kA32PthreadMutexattrSettypeSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("could not select error-check mutex type");
    }
    regs = {};
    regs[0] = kErrorcheckMutex;
    regs[1] = kAttr;
    if (fixture.call(kA32PthreadMutexInitSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("error-check pthread_mutex_init failed");
    }
    regs = {};
    regs[0] = kErrorcheckMutex;
    if (fixture.call(kA32PthreadMutexLockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("error-check first lock failed");
    }
    regs = {};
    regs[0] = kErrorcheckMutex;
    if (fixture.call(kA32PthreadMutexLockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_value(regs[0]) != kA32AndroidEdeadlk) {
        return fail("error-check relock did not return EDEADLK");
    }
    regs = {};
    regs[0] = kErrorcheckMutex;
    if (fixture.call(kA32PthreadMutexTrylockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_value(regs[0]) != kA32AndroidEbusy) {
        return fail("error-check trylock relock did not return EBUSY");
    }

    fixture.select(2U);
    regs = {};
    regs[0] = kErrorcheckMutex;
    if (fixture.call(kA32PthreadMutexUnlockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_value(regs[0]) != kA32AndroidEperm) {
        return fail("non-owner error-check unlock did not return EPERM");
    }
    fixture.select(1U);
    regs = {};
    regs[0] = kErrorcheckMutex;
    if (fixture.call(kA32PthreadMutexUnlockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("owner error-check unlock failed");
    }

    regs = {};
    regs[0] = kDefaultMutex;
    if (fixture.call(kA32PthreadMutexLockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("default static-initializer mutex behavior regressed");
    }
    regs = {};
    regs[0] = kDefaultMutex;
    if (fixture.call(kA32PthreadMutexTrylockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_value(regs[0]) != kA32AndroidEbusy) {
        return fail("default mutex trylock did not remain normal");
    }
    regs = {};
    regs[0] = kDefaultMutex;
    if (fixture.call(kA32PthreadMutexUnlockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled) {
        return fail("default mutex unlock regressed");
    }

    regs = {};
    regs[0] = kAttr;
    if (fixture.call(kA32PthreadMutexattrDestroySvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("pthread_mutexattr_destroy failed");
    }
    return 0;
}

int test_rwlock_reader_writer_ordering() {
    Fixture fixture;
    constexpr std::uint32_t kRw = 0x600U;

    std::array<std::uint32_t, 16> regs{};
    regs[0] = kRw;
    if (fixture.call(kA32PthreadRwlockInitSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("pthread_rwlock_init failed");
    }

    for (std::uint32_t thread : {1U, 2U}) {
        fixture.select(thread);
        regs = {};
        regs[0] = kRw;
        if (fixture.call(kA32PthreadRwlockRdlockSvcImmediate, regs) !=
                A32HostServiceDisposition::Handled ||
            regs[0] != 0U) {
            return fail("concurrent pthread_rwlock_rdlock failed");
        }
    }
    if (fixture.rwlocks[0].reader_count != 2U) {
        return fail("rwlock reader count was incorrect");
    }

    fixture.select(3U);
    regs = {};
    regs[0] = kRw;
    if (fixture.call(kA32PthreadRwlockWrlockSvcImmediate, regs) !=
            A32HostServiceDisposition::Suspended ||
        regs[0] != 0U) {
        return fail("contended writer did not suspend");
    }

    // Bionic's default rwlock is reader-preferring while locked by readers:
    // a new reader may still acquire even when a writer is pending.
    fixture.select(4U);
    regs = {};
    regs[0] = kRw;
    if (fixture.call(kA32PthreadRwlockRdlockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        fixture.rwlocks[0].reader_count != 3U) {
        return fail("default reader preference did not match Bionic");
    }

    fixture.select(5U);
    regs = {};
    regs[0] = kRw;
    if (fixture.call(kA32PthreadRwlockTrywrlockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_value(regs[0]) != kA32AndroidEbusy) {
        return fail("pthread_rwlock_trywrlock did not return EBUSY");
    }

    for (std::uint32_t thread : {1U, 2U, 4U}) {
        fixture.select(thread);
        regs = {};
        regs[0] = kRw;
        if (fixture.call(kA32PthreadRwlockUnlockSvcImmediate, regs) !=
                A32HostServiceDisposition::Handled ||
            regs[0] != 0U) {
            return fail("reader unlock failed");
        }
    }
    const auto writer_wake = fixture.service.pop_ready();
    if (!writer_wake.has_value() ||
        writer_wake->kind != A32PthreadWaitKind::RwWrite ||
        writer_wake->thread_id != 3U ||
        fixture.rwlocks[0].writer_thread_id != 3U) {
        return fail("last reader did not grant pending writer");
    }

    fixture.select(3U);
    regs = {};
    regs[0] = kRw;
    if (fixture.call(kA32PthreadRwlockTryrdlockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_value(regs[0]) != kA32AndroidEbusy) {
        return fail("writer-owned tryrdlock did not return EBUSY");
    }
    regs = {};
    regs[0] = kRw;
    if (fixture.call(kA32PthreadRwlockRdlockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_value(regs[0]) != kA32AndroidEdeadlk) {
        return fail("writer-owned rdlock did not return EDEADLK");
    }
    regs = {};
    regs[0] = kRw;
    if (fixture.call(kA32PthreadRwlockWrlockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_value(regs[0]) != kA32AndroidEdeadlk) {
        return fail("writer relock did not return EDEADLK");
    }

    fixture.select(4U);
    regs = {};
    regs[0] = kRw;
    if (fixture.call(kA32PthreadRwlockRdlockSvcImmediate, regs) !=
            A32HostServiceDisposition::Suspended) {
        return fail("reader did not suspend behind active writer");
    }

    fixture.select(5U);
    regs = {};
    regs[0] = kRw;
    if (fixture.call(kA32PthreadRwlockWrlockSvcImmediate, regs) !=
            A32HostServiceDisposition::Suspended) {
        return fail("second writer did not suspend behind active writer");
    }

    fixture.select(3U);
    regs = {};
    regs[0] = kRw;
    if (fixture.call(kA32PthreadRwlockDestroySvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_value(regs[0]) != kA32AndroidEbusy) {
        return fail("busy rwlock destroy did not return EBUSY");
    }
    regs = {};
    regs[0] = kRw;
    if (fixture.call(kA32PthreadRwlockUnlockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("writer unlock failed");
    }

    const auto preferred_writer = fixture.service.pop_ready();
    if (!preferred_writer.has_value() ||
        preferred_writer->kind != A32PthreadWaitKind::RwWrite ||
        preferred_writer->thread_id != 5U ||
        fixture.rwlocks[0].writer_thread_id != 5U) {
        return fail("rwlock unlock did not prefer pending writer");
    }
    if (fixture.service.pop_ready().has_value()) {
        return fail("reader woke before preferred writer released");
    }

    fixture.select(5U);
    regs = {};
    regs[0] = kRw;
    if (fixture.call(kA32PthreadRwlockUnlockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("preferred writer unlock failed");
    }
    const auto reader_wake = fixture.service.pop_ready();
    if (!reader_wake.has_value() ||
        reader_wake->kind != A32PthreadWaitKind::RwRead ||
        reader_wake->thread_id != 4U ||
        fixture.rwlocks[0].reader_count != 1U) {
        return fail("pending reader did not wake after writer queue drained");
    }

    fixture.select(4U);
    regs = {};
    regs[0] = kRw;
    if (fixture.call(kA32PthreadRwlockUnlockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("woken reader unlock failed");
    }

    fixture.select(6U);
    regs = {};
    regs[0] = kRw;
    if (fixture.call(kA32PthreadRwlockUnlockSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_value(regs[0]) != kA32AndroidEperm) {
        return fail("unlock of unowned rwlock did not return EPERM");
    }

    regs = {};
    regs[0] = kRw;
    if (fixture.call(kA32PthreadRwlockDestroySvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("pthread_rwlock_destroy failed after release");
    }
    return 0;
}

int test_once_interleaving_and_failure_latch() {
    Fixture fixture;
    constexpr std::uint32_t kOnce = 0x700U;
    constexpr std::uint32_t kFailedOnce = 0x740U;

    constexpr std::array<std::uint8_t, 12> once_program{{
        0x19, 0x01, 0x00, 0xef,  // svc #0x119
        0x01, 0x40, 0x84, 0xe2,  // add r4,r4,#1
        0x1e, 0xff, 0x2f, 0xe1,  // bx lr
    }};
    constexpr std::array<std::uint8_t, 8> initializer{{
        0xba, 0x00, 0x00, 0xef,  // svc #0xba (sem_wait)
        0x1e, 0xff, 0x2f, 0xe1,  // bx lr
    }};
    if (!fixture.memory.write(kOnceProgram, once_program) ||
        !fixture.memory.write(kOnceInitializer, initializer)) {
        return fail("could not stage pthread_once execution");
    }

    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    regs[0] = kOnce;
    regs[1] = 0U;
    regs[2] = 0U;
    if (fixture.service.handle(
            fixture.memory,
            kA32SemInitSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled) {
        return fail("could not initialize once blocking semaphore");
    }

    fixture.select(1U);
    ExecutionRequest owner_request{};
    owner_request.entry_pc = kOnceProgram;
    owner_request.regs[0] = kOnce;
    owner_request.regs[1] = kOnceInitializer;
    owner_request.regs[4] = 10U;
    owner_request.regs[13] = 0x4000U;
    owner_request.regs[14] = kStopPc;
    owner_request.instruction_count = 12U;
    owner_request.stop_pc = kStopPc;
    const auto owner_suspended = execute_a32_with_services(
        fixture.memory, owner_request, fixture.service, 4U);
    if (!owner_suspended || !owner_suspended.service_suspended ||
        !owner_suspended.suspended_svc_immediate.has_value() ||
        *owner_suspended.suspended_svc_immediate != kA32SemWaitSvcImmediate ||
        fixture.once_controls[0].phase != A32PthreadOncePhase::Initializing ||
        fixture.once_controls[0].owner_thread_id != 1U) {
        return fail("pthread_once owner did not suspend inside initializer");
    }

    fixture.select(2U);
    ExecutionRequest waiter_request{};
    waiter_request.entry_pc = kOnceProgram;
    waiter_request.regs[0] = kOnce;
    waiter_request.regs[1] = kOnceInitializer;
    waiter_request.regs[4] = 20U;
    waiter_request.regs[13] = 0x3f00U;
    waiter_request.regs[14] = kStopPc;
    waiter_request.instruction_count = 4U;
    waiter_request.stop_pc = kStopPc;
    const auto waiter_suspended = execute_a32_with_services(
        fixture.memory, waiter_request, fixture.service, 1U);
    if (!waiter_suspended || !waiter_suspended.service_suspended ||
        !waiter_suspended.suspended_svc_immediate.has_value() ||
        *waiter_suspended.suspended_svc_immediate != kA32PthreadOnceSvcImmediate) {
        return fail("concurrent pthread_once caller did not suspend");
    }

    fixture.select(3U);
    regs = {};
    regs[0] = kOnce;
    if (fixture.service.handle(
            fixture.memory,
            kA32SemPostSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled) {
        return fail("could not release once initializer semaphore");
    }

    const auto owner_wake = fixture.service.pop_ready();
    if (!owner_wake.has_value() ||
        owner_wake->kind != A32PthreadWaitKind::Semaphore ||
        owner_wake->thread_id != 1U) {
        return fail("once initializer did not receive semaphore wake");
    }
    const auto owner_id = A32LogicalThreadId::from_raw(1U);
    const auto owner_resume = owner_id.has_value()
        ? make_a32_service_resume_context(
              *owner_id, owner_suspended, 8U, kStopPc)
        : std::nullopt;
    if (!owner_resume.has_value() ||
        !fixture.service.set_current_thread_context(*owner_resume)) {
        return fail("could not resume once initializer owner");
    }
    const auto owner_completed = execute_a32_with_services(
        fixture.memory, owner_resume->request, fixture.service, 2U);
    if (!owner_completed || !owner_completed.stop_pc_reached ||
        owner_completed.regs[4] != 11U ||
        fixture.once_controls[0].phase != A32PthreadOncePhase::Done) {
        return fail("once initializer did not complete exactly once");
    }

    const auto once_wake = fixture.service.pop_ready();
    if (!once_wake.has_value() ||
        once_wake->kind != A32PthreadWaitKind::Once ||
        once_wake->thread_id != 2U) {
        return fail("pthread_once completion did not wake blocked caller");
    }
    const auto waiter_id = A32LogicalThreadId::from_raw(2U);
    const auto waiter_resume = waiter_id.has_value()
        ? make_a32_service_resume_context(
              *waiter_id, waiter_suspended, 2U, kStopPc)
        : std::nullopt;
    if (!waiter_resume.has_value() ||
        !fixture.service.set_current_thread_context(*waiter_resume)) {
        return fail("could not resume pthread_once waiter");
    }
    const auto waiter_completed = execute_a32_with_services(
        fixture.memory, waiter_resume->request, fixture.service, 1U);
    if (!waiter_completed || !waiter_completed.stop_pc_reached ||
        waiter_completed.regs[4] != 21U) {
        return fail("pthread_once waiter resumed incorrectly");
    }

    fixture.select(4U);
    ExecutionRequest after_done{};
    after_done.entry_pc = kOnceProgram;
    after_done.regs[0] = kOnce;
    after_done.regs[1] = kOnceInitializer;
    after_done.regs[4] = 30U;
    after_done.regs[13] = 0x3e00U;
    after_done.regs[14] = kStopPc;
    after_done.instruction_count = 3U;
    after_done.stop_pc = kStopPc;
    const auto done_call = execute_a32_with_services(
        fixture.memory, after_done, fixture.service, 1U);
    if (!done_call || !done_call.stop_pc_reached ||
        done_call.regs[4] != 31U ||
        fixture.service.pop_ready().has_value()) {
        return fail("completed pthread_once executed initializer again");
    }

    fixture.select(4U);
    ExecutionRequest bad{};
    bad.entry_pc = kOnceProgram;
    bad.regs[0] = kFailedOnce;
    bad.regs[1] = 0x6000U;
    bad.regs[13] = 0x3d00U;
    bad.regs[14] = kStopPc;
    bad.instruction_count = 3U;
    bad.stop_pc = kStopPc;
    const auto faulted =
        execute_a32_with_services(fixture.memory, bad, fixture.service, 1U);
    if (faulted.error != A32ServiceDispatchError::MemoryFault ||
        fixture.once_controls[1].phase != A32PthreadOncePhase::Initializing ||
        !fixture.service.fail_once_initialization(4U) ||
        fixture.once_controls[1].phase != A32PthreadOncePhase::Failed) {
        return fail("faulted pthread_once initializer did not latch failure");
    }

    fixture.select(5U);
    regs = {};
    regs[0] = kFailedOnce;
    regs[1] = kOnceInitializer;
    regs[13] = 0x3c00U;
    regs[14] = kStopPc;
    regs[15] = 4U;
    cpsr = 0x10U;
    if (fixture.service.handle(
            fixture.memory,
            kA32PthreadOnceSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("failed pthread_once state was replayed");
    }

    return 0;
}

}  // namespace

int main() {
    if (const int status = test_mutex_types(); status != 0) {
        return status;
    }
    if (const int status = test_rwlock_reader_writer_ordering();
        status != 0) {
        return status;
    }
    if (const int status = test_once_interleaving_and_failure_latch();
        status != 0) {
        return status;
    }
    return 0;
}
