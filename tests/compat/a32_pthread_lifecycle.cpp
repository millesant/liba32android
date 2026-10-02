#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <span>

#include "compat/a32_pthread_lifecycle.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"

namespace {

using liba32android::compat::A32PthreadAttrState;
using liba32android::compat::A32PthreadLifecycleOptions;
using liba32android::compat::A32PthreadLifecycleService;
using liba32android::compat::A32PthreadThreadPhase;
using liba32android::compat::A32PthreadThreadState;
using liba32android::compat::kA32AndroidEagain;
using liba32android::compat::kA32AndroidEinval;
using liba32android::compat::kA32PthreadAttrDestroySvcImmediate;
using liba32android::compat::kA32PthreadAttrGetdetachstateSvcImmediate;
using liba32android::compat::kA32PthreadAttrGetstacksizeSvcImmediate;
using liba32android::compat::kA32PthreadAttrInitSvcImmediate;
using liba32android::compat::kA32PthreadAttrSetdetachstateSvcImmediate;
using liba32android::compat::kA32PthreadAttrSetstacksizeSvcImmediate;
using liba32android::compat::kA32PthreadCreateDetached;
using liba32android::compat::kA32PthreadCreateJoinable;
using liba32android::compat::kA32PthreadCreateSvcImmediate;
using liba32android::compat::kA32PthreadEqualSvcImmediate;
using liba32android::compat::kA32PthreadExitSvcImmediate;
using liba32android::compat::kA32PthreadSelfSvcImmediate;
using liba32android::memory::LinearGuestMemory;
using liba32android::runtime::A32HostServiceDisposition;
using liba32android::runtime::A32LogicalThreadId;
using liba32android::runtime::execute_a32_with_services;

constexpr std::uint32_t kStartRoutine = 0x1000U;
constexpr std::uint32_t kExitStub = 0x2000U;
constexpr std::uint32_t kAttrAddress = 0x300U;
constexpr std::uint32_t kThreadOut = 0x400U;

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
    LinearGuestMemory memory{0x20000U};
    std::array<A32PthreadAttrState, 4> attrs{};
    std::array<A32PthreadThreadState, 4> threads{};
    A32PthreadLifecycleService service{
        A32PthreadLifecycleOptions{
            .stack_arena_base = 0x8000U,
            .stack_arena_size = 0x8000U,
            .page_size = 0x1000U,
            .default_stack_size = 0x2000U,
            .exit_trampoline = kExitStub,
            .thread_instruction_budget = 16U,
            .first_thread_id = 2U,
        },
        std::span{attrs},
        std::span{threads},
    };

    Fixture() {
        constexpr std::array<std::uint8_t, 4> return_code{
            0x1e, 0xff, 0x2f, 0xe1,  // bx lr
        };
        constexpr std::array<std::uint8_t, 8> exit_code{
            0x0d, 0x01, 0x00, 0xef,  // svc #0x10d
            0x1e, 0xff, 0x2f, 0xe1,  // bx lr
        };
        memory.write(kStartRoutine, return_code);
        memory.write(kExitStub, exit_code);
        const auto root = A32LogicalThreadId::from_raw(1U);
        if (root.has_value()) {
            service.register_initial_thread(*root);
            service.set_current_thread_id(*root);
        }
    }
};

A32HostServiceDisposition call(
    Fixture& fixture,
    std::uint32_t svc,
    std::array<std::uint32_t, 16>& regs) {
    std::uint32_t cpsr{};
    return fixture.service.handle(
        fixture.memory,
        svc,
        regs,
        cpsr);
}

int test_identity_and_attrs() {
    Fixture fixture;
    std::array<std::uint32_t, 16> regs{};

    if (call(fixture, kA32PthreadSelfSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 1U) {
        return fail("pthread_self did not return logical root identity");
    }

    regs = {};
    regs[0] = 1U;
    regs[1] = 1U;
    if (call(fixture, kA32PthreadEqualSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 1U) {
        return fail("pthread_equal did not report equal identities");
    }
    regs = {};
    regs[0] = 1U;
    regs[1] = 2U;
    if (call(fixture, kA32PthreadEqualSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("pthread_equal did not report distinct identities");
    }

    regs = {};
    regs[0] = kAttrAddress;
    if (call(fixture, kA32PthreadAttrInitSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("pthread_attr_init failed");
    }

    regs = {};
    regs[0] = kAttrAddress;
    regs[1] = kA32PthreadCreateDetached;
    if (call(fixture, kA32PthreadAttrSetdetachstateSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("pthread_attr_setdetachstate failed");
    }

    regs = {};
    regs[0] = kAttrAddress;
    regs[1] = 0x500U;
    if (call(fixture, kA32PthreadAttrGetdetachstateSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("pthread_attr_getdetachstate failed");
    }
    std::uint32_t value{};
    if (!read_u32(fixture.memory, 0x500U, value) ||
        value != kA32PthreadCreateDetached) {
        return fail("pthread_attr_getdetachstate wrote wrong state");
    }

    regs = {};
    regs[0] = kAttrAddress;
    regs[1] = 0x3000U;
    if (call(fixture, kA32PthreadAttrSetstacksizeSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("pthread_attr_setstacksize failed");
    }

    regs = {};
    regs[0] = kAttrAddress;
    regs[1] = 0x504U;
    if (call(fixture, kA32PthreadAttrGetstacksizeSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        !read_u32(fixture.memory, 0x504U, value) ||
        value != 0x3000U) {
        return fail("pthread_attr_getstacksize wrote wrong size");
    }

    regs = {};
    regs[0] = kAttrAddress;
    regs[1] = 0x1000U;
    if (call(fixture, kA32PthreadAttrSetstacksizeSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != kA32AndroidEinval) {
        return fail("pthread_attr_setstacksize accepted sub-minimum stack");
    }

    regs = {};
    regs[0] = kAttrAddress;
    regs[1] = 2U;
    if (call(fixture, kA32PthreadAttrSetdetachstateSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != kA32AndroidEinval) {
        return fail("pthread_attr_setdetachstate accepted invalid state");
    }

    regs = {};
    regs[0] = kAttrAddress;
    if (call(fixture, kA32PthreadAttrDestroySvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("pthread_attr_destroy failed");
    }

    return 0;
}

int test_create_return_and_explicit_exit() {
    Fixture fixture;
    std::array<std::uint32_t, 16> regs{};

    regs[0] = kAttrAddress;
    if (call(fixture, kA32PthreadAttrInitSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("could not initialize create attr");
    }
    regs = {};
    regs[0] = kAttrAddress;
    regs[1] = kA32PthreadCreateDetached;
    if (call(fixture, kA32PthreadAttrSetdetachstateSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("could not mark create attr detached");
    }
    regs = {};
    regs[0] = kAttrAddress;
    regs[1] = kA32PthreadCreateJoinable;
    if (call(fixture, kA32PthreadAttrSetdetachstateSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("could not restore joinable create attr");
    }

    constexpr std::uint32_t kReturnValue = 0xcafebabeU;
    regs = {};
    regs[0] = kThreadOut;
    regs[1] = kAttrAddress;
    regs[2] = kStartRoutine;
    regs[3] = kReturnValue;
    if (call(fixture, kA32PthreadCreateSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("pthread_create failed for returning start routine");
    }
    std::uint32_t first_id{};
    if (!read_u32(fixture.memory, kThreadOut, first_id) ||
        first_id != 2U) {
        return fail("pthread_create published wrong first identity");
    }
    auto created = fixture.service.pop_created_thread();
    if (!created.has_value() ||
        created->pthread_id != first_id ||
        created->detached ||
        !fixture.service.set_current_thread_context(created->context)) {
        return fail("pthread_create did not publish first logical context");
    }

    const auto returned = execute_a32_with_services(
        fixture.memory,
        created->context.request,
        fixture.service,
        1U);
    if (!returned || !returned.service_suspended ||
        !returned.suspended_svc_immediate.has_value() ||
        *returned.suspended_svc_immediate != kA32PthreadExitSvcImmediate) {
        return fail("start-routine return did not terminate through pthread_exit");
    }
    if (fixture.threads[1].phase != A32PthreadThreadPhase::Exited ||
        fixture.threads[1].return_value != kReturnValue ||
        fixture.threads[1].stack_base != 0x8000U ||
        fixture.threads[1].stack_size != 0x2000U) {
        return fail("returning logical thread stored wrong exit/stack state");
    }

    fixture.service.set_current_thread_id(1U);
    constexpr std::uint32_t kExplicitExitValue = 0x12345678U;
    regs = {};
    regs[0] = kThreadOut + 4U;
    regs[1] = kAttrAddress;
    regs[2] = kExitStub;
    regs[3] = kExplicitExitValue;
    if (call(fixture, kA32PthreadCreateSvcImmediate, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("pthread_create failed for explicit exit start");
    }
    std::uint32_t second_id{};
    if (!read_u32(fixture.memory, kThreadOut + 4U, second_id) ||
        second_id != 3U) {
        return fail("pthread_create published wrong second identity");
    }
    created = fixture.service.pop_created_thread();
    if (!created.has_value() ||
        created->pthread_id != second_id ||
        !fixture.service.set_current_thread_context(created->context)) {
        return fail("pthread_create did not publish second logical context");
    }

    const auto explicit_exit = execute_a32_with_services(
        fixture.memory,
        created->context.request,
        fixture.service,
        1U);
    if (!explicit_exit || !explicit_exit.service_suspended ||
        fixture.threads[2].phase != A32PthreadThreadPhase::Exited ||
        fixture.threads[2].return_value != kExplicitExitValue ||
        fixture.threads[2].stack_base != 0xa000U) {
        return fail("explicit pthread_exit did not finalize logical thread");
    }

    return 0;
}

int test_transactional_failure_and_capacity() {
    Fixture fixture;
    std::array<std::uint32_t, 16> regs{};

    regs[0] = 0x30000U;
    regs[1] = 0U;
    regs[2] = kStartRoutine;
    regs[3] = 1U;
    if (call(fixture, kA32PthreadCreateSvcImmediate, regs) !=
        A32HostServiceDisposition::Failed) {
        return fail("pthread_create did not fail unreadable output pointer");
    }
    if (fixture.service.pop_created_thread().has_value() ||
        fixture.threads[1].phase != A32PthreadThreadPhase::Free) {
        return fail("failed pthread_create mutated thread state");
    }

    std::array<A32PthreadAttrState, 1> attrs{};
    std::array<A32PthreadThreadState, 2> threads{};
    A32PthreadLifecycleService limited{
        A32PthreadLifecycleOptions{
            .stack_arena_base = 0x8000U,
            .stack_arena_size = 0x2000U,
            .page_size = 0x1000U,
            .default_stack_size = 0x2000U,
            .exit_trampoline = kExitStub,
            .thread_instruction_budget = 16U,
            .first_thread_id = 2U,
        },
        std::span{attrs},
        std::span{threads},
    };
    const auto root = A32LogicalThreadId::from_raw(1U);
    if (!root.has_value() ||
        !limited.register_initial_thread(*root) ||
        !limited.set_current_thread_id(*root)) {
        return fail("could not initialize limited lifecycle service");
    }

    std::uint32_t cpsr{};
    regs = {};
    regs[0] = kThreadOut;
    regs[2] = kStartRoutine;
    if (limited.handle(
            fixture.memory,
            kA32PthreadCreateSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("limited service first create failed");
    }

    constexpr std::array<std::uint8_t, 4> marker{{0xaa,0xbb,0xcc,0xdd}};
    fixture.memory.write(kThreadOut + 4U, marker);
    regs = {};
    regs[0] = kThreadOut + 4U;
    regs[2] = kStartRoutine;
    if (limited.handle(
            fixture.memory,
            kA32PthreadCreateSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        signed_r0(regs[0]) != kA32AndroidEagain) {
        return fail("thread/stack capacity did not return EAGAIN");
    }
    std::uint32_t preserved{};
    if (!read_u32(fixture.memory, kThreadOut + 4U, preserved) ||
        preserved != 0xddccbbaaU) {
        return fail("capacity failure modified pthread_create output");
    }

    return 0;
}

}  // namespace

int main() {
    if (const int status = test_identity_and_attrs(); status != 0) {
        return status;
    }
    if (const int status = test_create_return_and_explicit_exit(); status != 0) {
        return status;
    }
    if (const int status = test_transactional_failure_and_capacity(); status != 0) {
        return status;
    }
    return 0;
}
