#include <array>
#include <cstdint>
#include <iostream>
#include <span>

#include "compat/a32_pthread_sync.h"
#include "cpu/a32_cpu.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"

namespace {

using liba32android::compat::A32PthreadKeyState;
using liba32android::compat::A32PthreadMutexState;
using liba32android::compat::A32PthreadSyncService;
using liba32android::compat::A32PthreadTlsValue;
using liba32android::compat::A32PthreadWaitKind;
using liba32android::compat::A32PthreadWaiter;
using liba32android::compat::A32SemaphoreState;
using liba32android::compat::kA32AndroidEagain;
using liba32android::compat::kA32AndroidEnomem;
using liba32android::compat::kA32AndroidEinval;
using liba32android::compat::kA32AndroidEbusy;
using liba32android::compat::kA32PthreadKeyCreateSvcImmediate;
using liba32android::compat::kA32PthreadKeyDeleteSvcImmediate;
using liba32android::compat::kA32PthreadGetspecificSvcImmediate;
using liba32android::compat::kA32PthreadSetspecificSvcImmediate;
using liba32android::compat::kA32PthreadMutexDestroySvcImmediate;
using liba32android::compat::kA32PthreadMutexInitSvcImmediate;
using liba32android::compat::kA32PthreadMutexLockSvcImmediate;
using liba32android::compat::kA32PthreadMutexTrylockSvcImmediate;
using liba32android::compat::kA32PthreadMutexUnlockSvcImmediate;
using liba32android::compat::kA32SemDestroySvcImmediate;
using liba32android::compat::kA32SemInitSvcImmediate;
using liba32android::compat::kA32SemPostSvcImmediate;
using liba32android::compat::kA32SemWaitSvcImmediate;
using liba32android::cpu::ExecutionRequest;
using liba32android::memory::LinearGuestMemory;
using liba32android::runtime::A32HostServiceDisposition;
using liba32android::runtime::execute_a32_with_services;
using liba32android::runtime::make_a32_service_resume_request;

constexpr std::uint32_t kStopPc = 4096U;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

struct Fixture {
    LinearGuestMemory memory{4096};
    std::array<A32PthreadMutexState, 4> mutexes{};
    std::array<A32SemaphoreState, 4> semaphores{};
    std::array<A32PthreadWaiter, 8> waiters{};
    std::array<A32PthreadKeyState, 4> keys{};
    std::array<A32PthreadTlsValue, 8> tls_values{};
    A32PthreadSyncService service{
        std::span{mutexes},
        std::span{semaphores},
        std::span{waiters},
        std::span{keys},
        std::span{tls_values},
    };

    Fixture() {
        service.set_current_thread_id(1U);
    }
};

int test_mutex_basic_and_static_initializer() {
    Fixture fixture;
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    regs[0] = 0x100U;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadMutexInitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("pthread_mutex_init failed");
    }

    regs = {};
    regs[0] = 0x100U;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadMutexLockSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled) {
        return fail("uncontended pthread_mutex_lock failed");
    }

    regs = {};
    regs[0] = 0x100U;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadMutexTrylockSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        static_cast<std::int32_t>(regs[0]) != kA32AndroidEbusy) {
        return fail("pthread_mutex_trylock did not return EBUSY");
    }

    regs = {};
    regs[0] = 0x100U;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadMutexUnlockSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled) {
        return fail("pthread_mutex_unlock failed");
    }

    regs = {};
    regs[0] = 0x100U;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadMutexDestroySvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled) {
        return fail("pthread_mutex_destroy failed");
    }

    // Unknown mutex addresses are treated as default static initializers for
    // lock/trylock, without reading or exposing host object layout.
    regs = {};
    regs[0] = 0x140U;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadMutexLockSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled) {
        return fail("lazy static mutex lock failed");
    }
    regs = {};
    regs[0] = 0x140U;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadMutexUnlockSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled) {
        return fail("lazy static mutex unlock failed");
    }
    return 0;
}

int test_mutex_suspend_wake_resume() {
    Fixture fixture;
    constexpr std::array<std::uint8_t, 12> code{
        0xB5, 0x00, 0x00, 0xEF,  // svc #0xB5
        0x01, 0x40, 0x84, 0xE2,  // add r4,r4,#1
        0x1E, 0xFF, 0x2F, 0xE1,  // bx lr
    };
    if (!fixture.memory.write(0U, code)) {
        return fail("could not stage mutex suspension program");
    }

    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    regs[0] = 0x180U;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadMutexLockSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled) {
        return fail("thread 1 could not acquire mutex");
    }

    fixture.service.set_current_thread_id(2U);
    ExecutionRequest request{};
    request.regs[0] = 0x180U;
    request.regs[4] = 7U;
    request.regs[14] = kStopPc;
    request.instruction_count = 3;
    request.stop_pc = kStopPc;

    const auto suspended =
        execute_a32_with_services(fixture.memory, request, fixture.service, 1);
    if (!suspended || !suspended.service_suspended ||
        suspended.instructions_executed != 1 ||
        suspended.services_handled != 1 ||
        suspended.regs[0] != 0U ||
        suspended.regs[4] != 7U) {
        return fail("contended mutex did not suspend after SVC");
    }

    fixture.service.set_current_thread_id(1U);
    regs = {};
    regs[0] = 0x180U;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadMutexUnlockSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled) {
        return fail("owner could not unlock contended mutex");
    }
    const auto wake = fixture.service.pop_ready();
    if (!wake.has_value() ||
        wake->kind != A32PthreadWaitKind::Mutex ||
        wake->object_address != 0x180U ||
        wake->thread_id != 2U) {
        return fail("mutex unlock did not publish waiting thread");
    }

    const auto resumed_request =
        make_a32_service_resume_request(suspended, 2, kStopPc);
    if (!resumed_request.has_value()) {
        return fail("mutex suspension did not produce resume request");
    }

    fixture.service.set_current_thread_id(2U);
    const auto resumed = execute_a32_with_services(
        fixture.memory, *resumed_request, fixture.service, 1);
    if (!resumed || resumed.service_suspended ||
        !resumed.stop_pc_reached ||
        resumed.regs[4] != 8U ||
        resumed.services_handled != 0U) {
        return fail("woken mutex waiter did not resume after lock SVC");
    }

    regs = {};
    regs[0] = 0x180U;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadMutexUnlockSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled) {
        return fail("granted mutex was not owned by woken thread");
    }
    return 0;
}

int test_semaphore_suspend_post_resume() {
    Fixture fixture;
    constexpr std::array<std::uint8_t, 12> code{
        0xBA, 0x00, 0x00, 0xEF,  // svc #0xBA
        0x01, 0x50, 0x85, 0xE2,  // add r5,r5,#1
        0x1E, 0xFF, 0x2F, 0xE1,  // bx lr
    };
    if (!fixture.memory.write(0U, code)) {
        return fail("could not stage semaphore suspension program");
    }

    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    regs[0] = 0x200U;
    regs[1] = 0U;
    regs[2] = 0U;
    if (fixture.service.handle(
            fixture.memory, kA32SemInitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled) {
        return fail("sem_init failed");
    }

    fixture.service.set_current_thread_id(2U);
    ExecutionRequest request{};
    request.regs[0] = 0x200U;
    request.regs[5] = 11U;
    request.regs[14] = kStopPc;
    request.instruction_count = 3;
    request.stop_pc = kStopPc;

    const auto suspended =
        execute_a32_with_services(fixture.memory, request, fixture.service, 1);
    if (!suspended || !suspended.service_suspended ||
        suspended.regs[0] != 0U) {
        return fail("zero-count sem_wait did not suspend");
    }

    fixture.service.set_current_thread_id(1U);
    regs = {};
    regs[0] = 0x200U;
    if (fixture.service.handle(
            fixture.memory, kA32SemPostSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled) {
        return fail("sem_post failed to grant waiter");
    }

    const auto wake = fixture.service.pop_ready();
    if (!wake.has_value() ||
        wake->kind != A32PthreadWaitKind::Semaphore ||
        wake->object_address != 0x200U ||
        wake->thread_id != 2U) {
        return fail("sem_post did not publish waiting thread");
    }

    const auto resumed_request =
        make_a32_service_resume_request(suspended, 2, kStopPc);
    fixture.service.set_current_thread_id(2U);
    if (!resumed_request.has_value()) {
        return fail("sem_wait suspension did not produce resume request");
    }
    const auto resumed = execute_a32_with_services(
        fixture.memory, *resumed_request, fixture.service, 1);
    if (!resumed || !resumed.stop_pc_reached || resumed.regs[5] != 12U) {
        return fail("woken semaphore waiter did not resume after wait SVC");
    }

    fixture.service.set_current_thread_id(1U);
    regs = {};
    regs[0] = 0x200U;
    if (fixture.service.handle(
            fixture.memory, kA32SemDestroySvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled) {
        return fail("sem_destroy failed after waiter grant");
    }
    return 0;
}

int test_tls_keys() {
    Fixture fixture;
    constexpr std::uint32_t kKeyAddress = 0x300U;
    constexpr std::uint32_t kDestructor = 0x12345678U;
    constexpr std::uint32_t kThreadOneValue = 0x44550000U;
    constexpr std::uint32_t kThreadTwoValue = 0x55660000U;
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    regs[0] = kKeyAddress;
    regs[1] = kDestructor;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadKeyCreateSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled || regs[0] != 0U) {
        return fail("pthread_key_create failed");
    }
    std::array<std::uint8_t, 4> key_bytes{};
    if (!fixture.memory.read(kKeyAddress, key_bytes)) {
        return fail("could not read pthread TLS key");
    }
    const std::uint32_t key =
        static_cast<std::uint32_t>(key_bytes[0]) |
        (static_cast<std::uint32_t>(key_bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(key_bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(key_bytes[3]) << 24U);
    if (key == 0U || !fixture.keys[0].active ||
        fixture.keys[0].destructor != kDestructor) {
        return fail("pthread_key_create stored wrong metadata");
    }

    regs = {};
    regs[0] = key;
    regs[1] = kThreadOneValue;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadSetspecificSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled || regs[0] != 0U) {
        return fail("pthread_setspecific failed for thread 1");
    }
    regs = {};
    regs[0] = key;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadGetspecificSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != kThreadOneValue) {
        return fail("pthread_getspecific returned wrong thread 1 value");
    }

    fixture.service.set_current_thread_id(2U);
    regs = {};
    regs[0] = key;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadGetspecificSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled || regs[0] != 0U) {
        return fail("pthread TLS leaked between logical threads");
    }
    regs = {};
    regs[0] = key;
    regs[1] = kThreadTwoValue;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadSetspecificSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled || regs[0] != 0U) {
        return fail("pthread_setspecific failed for thread 2");
    }

    fixture.service.set_current_thread_id(1U);
    regs = {};
    regs[0] = key;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadGetspecificSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != kThreadOneValue) {
        return fail("pthread TLS thread 1 value was overwritten");
    }
    regs = {};
    regs[0] = key;
    regs[1] = 0U;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadSetspecificSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled || regs[0] != 0U) {
        return fail("pthread_setspecific null clear failed");
    }

    for (std::uint32_t thread_id = 3U; thread_id <= 9U; ++thread_id) {
        fixture.service.set_current_thread_id(thread_id);
        regs = {};
        regs[0] = key;
        regs[1] = 0x66000000U + thread_id;
        if (fixture.service.handle(
                fixture.memory,
                kA32PthreadSetspecificSvcImmediate,
                regs,
                cpsr) != A32HostServiceDisposition::Handled ||
            regs[0] != 0U) {
            return fail("pthread TLS metadata fill failed");
        }
    }
    fixture.service.set_current_thread_id(10U);
    regs = {};
    regs[0] = key;
    regs[1] = 0x77000000U;
    if (fixture.service.handle(
            fixture.memory,
            kA32PthreadSetspecificSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        static_cast<std::int32_t>(regs[0]) != kA32AndroidEnomem) {
        return fail("pthread TLS metadata ceiling did not return ENOMEM");
    }

    fixture.service.set_current_thread_id(1U);
    regs = {};
    regs[0] = key;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadKeyDeleteSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled || regs[0] != 0U) {
        return fail("pthread_key_delete failed");
    }
    regs = {};
    regs[0] = key;
    regs[1] = kThreadOneValue;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadSetspecificSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        static_cast<std::int32_t>(regs[0]) != kA32AndroidEinval) {
        return fail("pthread_setspecific accepted deleted key");
    }

    for (std::size_t index = 0; index < fixture.keys.size(); ++index) {
        regs = {};
        regs[0] = kKeyAddress;
        if (fixture.service.handle(
                fixture.memory, kA32PthreadKeyCreateSvcImmediate, regs, cpsr) !=
                A32HostServiceDisposition::Handled || regs[0] != 0U) {
            return fail("pthread key capacity fill failed");
        }
    }
    regs = {};
    regs[0] = kKeyAddress;
    if (fixture.service.handle(
            fixture.memory, kA32PthreadKeyCreateSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        static_cast<std::int32_t>(regs[0]) != kA32AndroidEagain) {
        return fail("pthread key capacity did not return EAGAIN");
    }
    return 0;
}

int test_bounds_and_unknown_service() {
    LinearGuestMemory memory{4096};
    std::array<A32PthreadMutexState, 1> mutexes{};
    std::array<A32SemaphoreState, 1> semaphores{};
    std::array<A32PthreadWaiter, 1> waiters{};
    A32PthreadSyncService service{
        std::span{mutexes},
        std::span{semaphores},
        std::span{waiters},
    };
    service.set_current_thread_id(1U);

    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    regs[0] = 0x100U;
    if (service.handle(memory, kA32PthreadMutexLockSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Handled) {
        return fail("single mutex slot could not be allocated");
    }

    regs = {};
    regs[0] = 0x140U;
    if (service.handle(memory, kA32PthreadMutexLockSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Failed) {
        return fail("mutex metadata ceiling was not enforced");
    }

    regs = {};
    regs[0] = 0x200U;
    regs[1] = 1U;
    if (service.handle(memory, kA32SemInitSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Failed) {
        return fail("process-shared sem_init was not rejected");
    }

    regs = {};
    if (service.handle(memory, 0xCCU, regs, cpsr) !=
        A32HostServiceDisposition::Unhandled) {
        return fail("unknown sync SVC was not unhandled");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_mutex_basic_and_static_initializer(); status != 0) {
        return status;
    }
    if (const int status = test_mutex_suspend_wake_resume(); status != 0) {
        return status;
    }
    if (const int status = test_semaphore_suspend_post_resume(); status != 0) {
        return status;
    }
    if (const int status = test_tls_keys(); status != 0) {
        return status;
    }
    if (const int status = test_bounds_and_unknown_service(); status != 0) {
        return status;
    }
    return 0;
}
