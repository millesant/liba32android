#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>

#include "compat/a32_pthread_lifecycle.h"
#include "compat/a32_pthread_sync.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"

namespace {

using namespace liba32android::compat;
using liba32android::memory::LinearGuestMemory;
using liba32android::runtime::A32HostServiceDisposition;
using liba32android::runtime::A32LogicalThreadId;
using liba32android::runtime::execute_a32_with_services;

constexpr std::uint32_t kStartRoutine = 0x1000U;
constexpr std::uint32_t kDestructor = 0x1200U;
constexpr std::uint32_t kExitStub = 0x2000U;
constexpr std::uint32_t kDestructorReturn = 0x3000U;
constexpr std::uint32_t kCleanupRoutine = 0x3020U;
constexpr std::uint32_t kThreadOut = 0x400U;
constexpr std::uint32_t kJoinResult = 0x500U;
constexpr std::uint32_t kKeyOut = 0x600U;
constexpr std::uint32_t kCleanupRecord = 0x740U;
constexpr std::uint32_t kCleanupOrderMarker = 0x760U;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

std::int32_t signed_r0(std::uint32_t value) {
    return std::bit_cast<std::int32_t>(value);
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
    LinearGuestMemory memory{0x40000U};
    std::array<A32PthreadMutexState, 2> mutexes{};
    std::array<A32SemaphoreState, 2> semaphores{};
    std::array<A32PthreadWaiter, 4> waiters{};
    std::array<A32PthreadKeyState, 4> keys{};
    std::array<A32PthreadTlsValue, 16> tls_values{};
    A32PthreadSyncService sync{
        std::span{mutexes},
        std::span{semaphores},
        std::span{waiters},
        std::span{keys},
        std::span{tls_values},
    };
    std::array<A32PthreadAttrState, 4> attrs{};
    std::array<A32PthreadThreadState, 6> threads{};
    A32PthreadLifecycleService lifecycle{
        A32PthreadLifecycleOptions{
            .stack_arena_base = 0x8000U,
            .stack_arena_size = 0x10000U,
            .page_size = 0x1000U,
            .default_stack_size = 0x2000U,
            .exit_trampoline = kExitStub,
            .thread_instruction_budget = 32U,
            .first_thread_id = 2U,
            .tls_destructor_return_pc = kDestructorReturn,
            .tls_destructor_instruction_budget = 32U,
            .tls_destructor_service_limit = 4U,
        },
        std::span{attrs},
        std::span{threads},
        &sync,
    };

    Fixture() {
        constexpr std::array<std::uint8_t, 4> return_code{{
            0x1e, 0xff, 0x2f, 0xe1,  // bx lr
        }};
        constexpr std::array<std::uint8_t, 8> exit_code{{
            0x0d, 0x01, 0x00, 0xef,  // svc #0x10d
            0x1e, 0xff, 0x2f, 0xe1,  // bx lr
        }};
        // Cleanup writes 1. The TLS destructor then increments the same marker
        // and repopulates its key on each pass. Four destructor rounds therefore
        // leave 5 only when cleanup handlers ran before TLS destructors.
        constexpr std::array<std::uint8_t, 12> cleanup_code{{
            0x01, 0x10, 0xa0, 0xe3,  // mov r1, #1
            0x00, 0x10, 0x80, 0xe5,  // str r1, [r0]
            0x1e, 0xff, 0x2f, 0xe1,  // bx lr
        }};
        constexpr std::array<std::uint8_t, 28> destructor_code{{
            0x00, 0x20, 0x90, 0xe5,  // ldr r2, [r0]
            0x01, 0x20, 0x82, 0xe2,  // add r2, r2, #1
            0x00, 0x20, 0x80, 0xe5,  // str r2, [r0]
            0x00, 0x10, 0xa0, 0xe1,  // mov r1, r0
            0x01, 0x00, 0xa0, 0xe3,  // mov r0, #1
            0x03, 0x01, 0x00, 0xef,  // svc #0x103
            0x1e, 0xff, 0x2f, 0xe1,  // bx lr
        }};
        memory.write(kStartRoutine, return_code);
        memory.write(kExitStub, exit_code);
        memory.write(kDestructor, destructor_code);
        memory.write(kCleanupRoutine, cleanup_code);

        const auto root = A32LogicalThreadId::from_raw(1U);
        if (root.has_value()) {
            lifecycle.register_initial_thread(*root);
            lifecycle.set_current_thread_id(*root);
            sync.set_current_thread_id(*root);
        }
    }

    A32HostServiceDisposition call_lifecycle(
        std::uint32_t svc,
        std::array<std::uint32_t, 16>& regs) {
        std::uint32_t cpsr{};
        return lifecycle.handle(memory, svc, regs, cpsr);
    }

    bool select(std::uint32_t thread_id) {
        const auto logical = A32LogicalThreadId::from_raw(thread_id);
        return logical.has_value() &&
               lifecycle.set_current_thread_id(*logical) &&
               sync.set_current_thread_id(*logical);
    }

    std::optional<A32PthreadCreatedThread> create(
        std::uint32_t argument,
        std::uint32_t out_address = kThreadOut) {
        std::array<std::uint32_t, 16> regs{};
        regs[0] = out_address;
        regs[2] = kStartRoutine;
        regs[3] = argument;
        if (call_lifecycle(kA32PthreadCreateSvcImmediate, regs) !=
                A32HostServiceDisposition::Handled ||
            regs[0] != 0U) {
            return std::nullopt;
        }
        return lifecycle.pop_created_thread();
    }
};

int test_join_errors_and_wakeup_reclamation() {
    Fixture fixture;

    std::array<std::uint32_t, 16> regs{};
    regs[0] = 1U;
    if (fixture.call_lifecycle(kA32PthreadJoinSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != kA32AndroidEdeadlk) {
        return fail("pthread_join self did not return EDEADLK");
    }

    regs = {};
    regs[0] = 99U;
    if (fixture.call_lifecycle(kA32PthreadJoinSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != kA32AndroidEsrch) {
        return fail("pthread_join unknown target did not return ESRCH");
    }

    auto target = fixture.create(0x11223344U, kThreadOut);
    auto other = fixture.create(0x55667788U, kThreadOut + 4U);
    if (!target.has_value() || !other.has_value() ||
        target->pthread_id != 2U || other->pthread_id != 3U) {
        return fail("could not create join target and second joiner");
    }

    regs = {};
    regs[0] = target->pthread_id;
    regs[1] = kJoinResult;
    if (fixture.call_lifecycle(kA32PthreadJoinSvcImmediate, regs) !=
            A32HostServiceDisposition::Suspended ||
        regs[0] != 0U) {
        return fail("live pthread_join did not suspend");
    }

    if (!fixture.select(other->pthread_id)) {
        return fail("could not select duplicate joiner thread");
    }
    regs = {};
    regs[0] = target->pthread_id;
    if (fixture.call_lifecycle(kA32PthreadJoinSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != kA32AndroidEinval) {
        return fail("duplicate pthread_join did not return EINVAL");
    }

    if (!fixture.lifecycle.set_current_thread_context(target->context) ||
        !fixture.sync.set_current_thread_context(target->context)) {
        return fail("could not select join target context");
    }
    const auto exited = execute_a32_with_services(
        fixture.memory,
        target->context.request,
        fixture.lifecycle,
        1U);
    if (!exited || !exited.service_suspended) {
        return fail("join target did not exit through pthread_exit");
    }

    std::uint32_t joined_value{};
    if (!read_u32(fixture.memory, kJoinResult, joined_value) ||
        joined_value != 0x11223344U) {
        return fail("blocked join did not publish target return value");
    }
    const auto wake = fixture.lifecycle.pop_ready_join();
    if (!wake.has_value() ||
        wake->thread_id != 1U ||
        wake->target_thread_id != target->pthread_id) {
        return fail("pthread_join wake record was wrong");
    }
    for (const auto& thread : fixture.threads) {
        if (thread.pthread_id == target->pthread_id) {
            return fail("joined target metadata was not reclaimed");
        }
    }

    if (!fixture.select(1U)) {
        return fail("could not restore root after join wake");
    }
    auto reused = fixture.create(0x99U, kThreadOut + 8U);
    if (!reused.has_value() ||
        reused->context.request.regs[13] != target->context.request.regs[13]) {
        return fail("joined target stack capacity was not reused first-fit");
    }

    regs = {};
    regs[0] = reused->pthread_id;
    regs[1] = 0x50000U;
    if (fixture.call_lifecycle(kA32PthreadJoinSvcImmediate, regs) !=
            A32HostServiceDisposition::Failed) {
        return fail("pthread_join accepted invalid result memory");
    }

    return 0;
}

int test_detach_and_exited_join() {
    Fixture fixture;
    auto detached = fixture.create(0xaabbccddU);
    if (!detached.has_value()) {
        return fail("could not create detach target");
    }

    std::array<std::uint32_t, 16> regs{};
    regs[0] = detached->pthread_id;
    if (fixture.call_lifecycle(kA32PthreadDetachSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("pthread_detach running target failed");
    }
    regs = {};
    regs[0] = detached->pthread_id;
    if (fixture.call_lifecycle(kA32PthreadDetachSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != kA32AndroidEinval) {
        return fail("repeated pthread_detach did not return EINVAL");
    }

    if (!fixture.lifecycle.set_current_thread_context(detached->context) ||
        !fixture.sync.set_current_thread_context(detached->context)) {
        return fail("could not select detached thread");
    }
    const auto exit_result = execute_a32_with_services(
        fixture.memory,
        detached->context.request,
        fixture.lifecycle,
        1U);
    if (!exit_result || !exit_result.service_suspended) {
        return fail("detached thread did not exit");
    }
    for (const auto& thread : fixture.threads) {
        if (thread.pthread_id == detached->pthread_id) {
            return fail("detached exited target was not reclaimed");
        }
    }

    if (!fixture.select(1U)) {
        return fail("could not restore root after detached exit");
    }
    auto joinable = fixture.create(0x1234U);
    if (!joinable.has_value() ||
        !fixture.lifecycle.set_current_thread_context(joinable->context) ||
        !fixture.sync.set_current_thread_context(joinable->context)) {
        return fail("could not create/select exited joinable target");
    }
    const auto joinable_exit = execute_a32_with_services(
        fixture.memory,
        joinable->context.request,
        fixture.lifecycle,
        1U);
    if (!joinable_exit || !joinable_exit.service_suspended) {
        return fail("joinable thread did not exit");
    }

    if (!fixture.select(1U)) {
        return fail("could not restore root for exited detach");
    }
    regs = {};
    regs[0] = joinable->pthread_id;
    if (fixture.call_lifecycle(kA32PthreadDetachSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("pthread_detach exited target failed");
    }
    for (const auto& thread : fixture.threads) {
        if (thread.pthread_id == joinable->pthread_id) {
            return fail("detach of exited target did not reclaim");
        }
    }

    regs = {};
    regs[0] = 77U;
    if (fixture.call_lifecycle(kA32PthreadDetachSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != kA32AndroidEsrch) {
        return fail("pthread_detach unknown target did not return ESRCH");
    }

    return 0;
}

int test_tls_destructor_iteration_and_join_after_exit() {
    Fixture fixture;
    constexpr std::uint32_t kReturnValue = 0x0badf00dU;
    constexpr std::uint32_t kTlsValue = kCleanupOrderMarker;

    auto target = fixture.create(kReturnValue);
    if (!target.has_value() ||
        !fixture.lifecycle.set_current_thread_context(target->context) ||
        !fixture.sync.set_current_thread_context(target->context)) {
        return fail("could not create/select TLS cleanup target");
    }

    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    regs[0] = kCleanupRecord;
    regs[1] = kCleanupRoutine;
    regs[2] = kCleanupOrderMarker;
    if (fixture.call_lifecycle(kA32PthreadCleanupPushSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled) {
        return fail("could not push cleanup handler before TLS destructors");
    }

    regs = {};
    regs[0] = kKeyOut;
    regs[1] = kDestructor;
    if (fixture.sync.handle(
            fixture.memory,
            kA32PthreadKeyCreateSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("could not create TLS key for exit cleanup");
    }
    std::uint32_t key{};
    if (!read_u32(fixture.memory, kKeyOut, key) || key != 1U) {
        return fail("TLS cleanup key identity was unexpected");
    }

    regs = {};
    regs[0] = key;
    regs[1] = kTlsValue;
    if (fixture.sync.handle(
            fixture.memory,
            kA32PthreadSetspecificSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("could not seed TLS value for exit cleanup");
    }

    const auto exited = execute_a32_with_services(
        fixture.memory,
        target->context.request,
        fixture.lifecycle,
        1U);
    if (!exited || !exited.service_suspended) {
        return fail("TLS cleanup target did not exit");
    }

    const auto& cleanup = fixture.lifecycle.last_exit_cleanup_result();
    if (!cleanup.has_value() || !*cleanup ||
        cleanup->cleanup_callbacks_completed != 1U ||
        cleanup->callbacks_completed != kA32PthreadDestructorIterations ||
        cleanup->iterations_completed != kA32PthreadDestructorIterations) {
        return fail("cleanup/TLS callback counts were incorrect");
    }
    std::uint32_t cleanup_marker{};
    if (!read_u32(fixture.memory, kCleanupOrderMarker, cleanup_marker) ||
        cleanup_marker != 5U) {
        return fail("cleanup handlers did not run before TLS destructors");
    }
    for (const auto& value : fixture.tls_values) {
        if (value.active && value.thread_id == target->pthread_id) {
            return fail("TLS values survived completed thread cleanup");
        }
    }

    if (!fixture.select(1U)) {
        return fail("could not restore root for exited join");
    }
    regs = {};
    regs[0] = target->pthread_id;
    regs[1] = kJoinResult;
    if (fixture.call_lifecycle(kA32PthreadJoinSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("join of already-exited thread failed");
    }
    std::uint32_t joined_value{};
    if (!read_u32(fixture.memory, kJoinResult, joined_value) ||
        joined_value != kReturnValue) {
        return fail("join of exited thread returned wrong value");
    }

    return 0;
}

int test_cleanup_failure_latches_without_replay() {
    Fixture fixture;
    auto target = fixture.create(0x4444U);
    if (!target.has_value() ||
        !fixture.lifecycle.set_current_thread_context(target->context) ||
        !fixture.sync.set_current_thread_context(target->context)) {
        return fail("could not create/select cleanup failure target");
    }

    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    regs[0] = kKeyOut;
    regs[1] = 0xffffffffU;
    if (fixture.sync.handle(
            fixture.memory,
            kA32PthreadKeyCreateSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("could not create invalid-destructor TLS key");
    }
    std::uint32_t key{};
    if (!read_u32(fixture.memory, kKeyOut, key)) {
        return fail("could not read invalid-destructor TLS key");
    }

    regs = {};
    regs[0] = key;
    regs[1] = 0x7777U;
    if (fixture.sync.handle(
            fixture.memory,
            kA32PthreadSetspecificSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("could not seed cleanup failure TLS value");
    }

    const auto failed = execute_a32_with_services(
        fixture.memory,
        target->context.request,
        fixture.lifecycle,
        1U);
    if (failed ||
        failed.error !=
            liba32android::runtime::A32ServiceDispatchError::ServiceFailed) {
        return fail("invalid TLS destructor did not fail pthread_exit");
    }
    const auto& cleanup = fixture.lifecycle.last_exit_cleanup_result();
    if (!cleanup.has_value() ||
        cleanup->error !=
            A32PthreadExitCleanupError::InvalidDestructorAddress ||
        cleanup->callbacks_completed != 0U) {
        return fail("cleanup failure diagnostics were incorrect");
    }

    bool found_failed = false;
    for (const auto& thread : fixture.threads) {
        if (thread.pthread_id == target->pthread_id) {
            found_failed =
                thread.phase == A32PthreadThreadPhase::CleanupFailed;
        }
    }
    if (!found_failed) {
        return fail("cleanup failure did not latch thread state");
    }

    regs = {};
    regs[0] = 0x9999U;
    if (fixture.lifecycle.handle(
            fixture.memory,
            kA32PthreadExitSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Failed) {
        return fail("failed exit cleanup was replayed");
    }
    const auto& after_retry =
        fixture.lifecycle.last_exit_cleanup_result();
    if (!after_retry.has_value() ||
        after_retry->error !=
            A32PthreadExitCleanupError::InvalidDestructorAddress ||
        after_retry->callbacks_completed != 0U) {
        return fail("retry mutated latched cleanup failure");
    }

    return 0;
}

}  // namespace

int main() {
    if (const int status = test_join_errors_and_wakeup_reclamation();
        status != 0) {
        return status;
    }
    if (const int status = test_detach_and_exited_join(); status != 0) {
        return status;
    }
    if (const int status =
            test_tls_destructor_iteration_and_join_after_exit();
        status != 0) {
        return status;
    }
    if (const int status =
            test_cleanup_failure_latches_without_replay();
        status != 0) {
        return status;
    }
    return 0;
}
