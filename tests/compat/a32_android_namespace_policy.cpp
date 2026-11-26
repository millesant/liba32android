#include <array>
#include <iostream>
#include <span>
#include <string_view>

#include "compat/a32_android_namespace_policy.h"

namespace {

using liba32android::compat::A32AndroidNamespaceAccessPolicy;
using liba32android::compat::A32AndroidNamespaceBinding;
using liba32android::compat::A32AndroidNamespaceLink;
using liba32android::compat::A32AndroidPlatformAccessDecision;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

int test_explicit_link_and_exact_bytes() {
    constexpr char requester_bytes[] = {
        'a', 'p', 'p', '\0', 'o', 'n', 'e',
    };
    const std::string_view requester{
        requester_bytes, sizeof(requester_bytes)};

    const std::array<std::string_view, 2> shared{{
        "liblog.so",
        "libm.so",
    }};
    const std::array<A32AndroidNamespaceBinding, 2> bindings{{
        {requester, "app"},
        {"unrelated", ""},
    }};
    const std::array<A32AndroidNamespaceLink, 2> links{{
        {"app", "platform", false, std::span{shared}},
        {"other", "unused", false, std::span<const std::string_view>{}},
    }};

    A32AndroidNamespaceAccessPolicy policy{
        std::span{bindings}, std::span{links}, "platform"};

    if (policy.decide(requester, "liblog.so") !=
            A32AndroidPlatformAccessDecision::Allow ||
        policy.decide(requester, "libc.so") !=
            A32AndroidPlatformAccessDecision::NotFound ||
        policy.binding_count() != 2 ||
        policy.link_count() != 2) {
        return fail("explicit namespace link did not use exact requester/SONAME bytes");
    }
    return 0;
}

int test_same_namespace_and_allow_all() {
    const std::array<A32AndroidNamespaceBinding, 1> same_binding{{
        {"platform-object", "platform"},
    }};
    A32AndroidNamespaceAccessPolicy same_policy{
        std::span{same_binding},
        std::span<const A32AndroidNamespaceLink>{},
        "platform"};
    if (same_policy.decide("platform-object", "libanything.so") !=
        A32AndroidPlatformAccessDecision::Allow) {
        return fail("same-namespace platform access was not allowed");
    }

    const std::array<A32AndroidNamespaceBinding, 1> app_binding{{
        {"app-object", "app"},
    }};
    const std::array<A32AndroidNamespaceLink, 1> allow_all_links{{
        {"app", "platform", true, std::span<const std::string_view>{}},
    }};
    A32AndroidNamespaceAccessPolicy allow_all_policy{
        std::span{app_binding}, std::span{allow_all_links}, "platform"};
    if (allow_all_policy.decide("app-object", "libfuture.so") !=
        A32AndroidPlatformAccessDecision::Allow) {
        return fail("allow-all namespace link did not allow arbitrary SONAME");
    }
    return 0;
}

int test_binding_failures() {
    const std::array<A32AndroidNamespaceBinding, 2> duplicate_bindings{{
        {"app-object", "app-a"},
        {"app-object", "app-b"},
    }};
    A32AndroidNamespaceAccessPolicy duplicate_policy{
        std::span{duplicate_bindings},
        std::span<const A32AndroidNamespaceLink>{},
        "platform"};
    if (duplicate_policy.decide("app-object", "liblog.so") !=
        A32AndroidPlatformAccessDecision::Failed) {
        return fail("duplicate requester namespace bindings were not rejected");
    }

    const std::array<A32AndroidNamespaceBinding, 1> empty_binding{{
        {"app-object", ""},
    }};
    A32AndroidNamespaceAccessPolicy empty_binding_policy{
        std::span{empty_binding},
        std::span<const A32AndroidNamespaceLink>{},
        "platform"};
    if (empty_binding_policy.decide("app-object", "liblog.so") !=
        A32AndroidPlatformAccessDecision::Failed) {
        return fail("empty matching namespace binding was not rejected");
    }

    const std::array<A32AndroidNamespaceBinding, 1> normal_binding{{
        {"known", "app"},
    }};
    A32AndroidNamespaceAccessPolicy normal_policy{
        std::span{normal_binding},
        std::span<const A32AndroidNamespaceLink>{},
        "platform"};
    if (normal_policy.decide("missing", "liblog.so") !=
            A32AndroidPlatformAccessDecision::NotFound ||
        normal_policy.decide({}, "liblog.so") !=
            A32AndroidPlatformAccessDecision::NotFound) {
        return fail("missing/context-free requester did not remain inaccessible");
    }

    A32AndroidNamespaceAccessPolicy invalid_target{
        std::span{normal_binding},
        std::span<const A32AndroidNamespaceLink>{},
        {}};
    if (invalid_target.decide("known", "liblog.so") !=
        A32AndroidPlatformAccessDecision::Failed) {
        return fail("empty platform namespace was not rejected");
    }
    return 0;
}

int test_link_failures() {
    const std::array<A32AndroidNamespaceBinding, 1> bindings{{
        {"app-object", "app"},
    }};
    const std::array<std::string_view, 1> log_only{{"liblog.so"}};

    const std::array<A32AndroidNamespaceLink, 1> both_modes{{
        {"app", "platform", true, std::span{log_only}},
    }};
    A32AndroidNamespaceAccessPolicy both_policy{
        std::span{bindings}, std::span{both_modes}, "platform"};
    if (both_policy.decide("app-object", "liblog.so") !=
        A32AndroidPlatformAccessDecision::Failed) {
        return fail("allow-all plus explicit SONAMEs was not rejected");
    }

    const std::array<A32AndroidNamespaceLink, 1> empty_explicit{{
        {"app", "platform", false, std::span<const std::string_view>{}},
    }};
    A32AndroidNamespaceAccessPolicy empty_policy{
        std::span{bindings}, std::span{empty_explicit}, "platform"};
    if (empty_policy.decide("app-object", "liblog.so") !=
        A32AndroidPlatformAccessDecision::Failed) {
        return fail("empty explicit namespace link was not rejected");
    }

    const std::array<A32AndroidNamespaceLink, 2> duplicate_links{{
        {"app", "platform", false, std::span{log_only}},
        {"app", "platform", false, std::span{log_only}},
    }};
    A32AndroidNamespaceAccessPolicy duplicate_policy{
        std::span{bindings}, std::span{duplicate_links}, "platform"};
    if (duplicate_policy.decide("app-object", "liblog.so") !=
        A32AndroidPlatformAccessDecision::Failed) {
        return fail("duplicate direct namespace links were not rejected");
    }

    const std::array<std::string_view, 2> malformed_list{{
        "liblog.so",
        "",
    }};
    const std::array<A32AndroidNamespaceLink, 1> malformed_links{{
        {"app", "platform", false, std::span{malformed_list}},
    }};
    A32AndroidNamespaceAccessPolicy malformed_policy{
        std::span{bindings}, std::span{malformed_links}, "platform"};
    if (malformed_policy.decide("app-object", "liblog.so") !=
        A32AndroidPlatformAccessDecision::Failed) {
        return fail("empty SONAME in matching namespace link was not rejected");
    }

    A32AndroidNamespaceAccessPolicy no_link_policy{
        std::span{bindings},
        std::span<const A32AndroidNamespaceLink>{},
        "platform"};
    if (no_link_policy.decide("app-object", "liblog.so") !=
        A32AndroidPlatformAccessDecision::NotFound) {
        return fail("missing direct namespace link did not remain inaccessible");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_explicit_link_and_exact_bytes(); status != 0) {
        return status;
    }
    if (const int status = test_same_namespace_and_allow_all(); status != 0) {
        return status;
    }
    if (const int status = test_binding_failures(); status != 0) {
        return status;
    }
    if (const int status = test_link_failures(); status != 0) {
        return status;
    }
    return 0;
}
