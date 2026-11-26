#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

#include "compat/a32_android_namespace_policy.h"
#include "compat/a32_android_platform_provider.h"
#include "elf/elf32_dependency_resolver.h"

namespace {

using liba32android::compat::A32AndroidNamespaceAccessPolicy;
using liba32android::compat::A32AndroidNamespaceBinding;
using liba32android::compat::A32AndroidNamespaceLink;
using liba32android::compat::A32AndroidPlatformAccessDecision;
using liba32android::compat::A32AndroidPlatformAccessPolicy;
using liba32android::compat::A32AndroidPlatformCatalogProvider;
using liba32android::elf::Elf32DependencyCatalogEntry;
using liba32android::elf::Elf32DependencyProviderError;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

class RecordingPolicy final : public A32AndroidPlatformAccessPolicy {
public:
    A32AndroidPlatformAccessDecision decision{
        A32AndroidPlatformAccessDecision::Allow};
    std::size_t calls{};
    std::string requester;
    std::string requested;

    A32AndroidPlatformAccessDecision decide(
        std::string_view requester_identity,
        std::string_view requested_name) override {
        ++calls;
        requester.assign(requester_identity.data(), requester_identity.size());
        requested.assign(requested_name.data(), requested_name.size());
        return decision;
    }
};

int test_exact_catalog_and_policy() {
    constexpr std::array<std::uint8_t, 3> log_image{{1, 2, 3}};
    constexpr std::array<std::uint8_t, 4> libc_image{{4, 5, 6, 7}};
    const std::array<Elf32DependencyCatalogEntry, 2> entries{{
        {
            .requested_name = "liblog.so",
            .identity = "platform-log",
            .image = std::span{log_image},
        },
        {
            .requested_name = "libc.so",
            .identity = "platform-libc",
            .image = std::span{libc_image},
        },
    }};

    RecordingPolicy policy;
    A32AndroidPlatformCatalogProvider provider{
        std::span{entries}, policy};

    constexpr char requester_bytes[] = {
        'a','p','p','\0','r','o','o','t',
    };
    const std::string_view requester{
        requester_bytes, sizeof(requester_bytes)};

    const auto result = provider.resolve_for(
        requester, "libc.so", libc_image.size());
    if (!result ||
        result.source.identity != "platform-libc" ||
        result.source.image.size() != libc_image.size() ||
        policy.calls != 1 ||
        policy.requester != requester ||
        policy.requested != "libc.so" ||
        provider.size() != 2) {
        return fail("platform catalog did not preserve exact request/policy");
    }
    return 0;
}

int test_unknown_and_policy_results() {
    constexpr std::array<std::uint8_t, 2> image{{1, 2}};
    const std::array<Elf32DependencyCatalogEntry, 1> entries{{
        {
            .requested_name = "libc.so",
            .identity = "platform-libc",
            .image = std::span{image},
        },
    }};
    RecordingPolicy policy;
    A32AndroidPlatformCatalogProvider provider{
        std::span{entries}, policy};

    auto result = provider.resolve_for(
        "root", "libm.so", image.size());
    if (result.error != Elf32DependencyProviderError::NotFound ||
        policy.calls != 0) {
        return fail("unknown platform catalog name reached policy");
    }

    policy.decision = A32AndroidPlatformAccessDecision::NotFound;
    result = provider.resolve_for(
        "root", "libc.so", image.size());
    if (result.error != Elf32DependencyProviderError::NotFound ||
        policy.calls != 1) {
        return fail("platform catalog did not preserve policy NotFound");
    }

    policy.decision = A32AndroidPlatformAccessDecision::Failed;
    result = provider.resolve_for(
        "root", "libc.so", image.size());
    if (result.error != Elf32DependencyProviderError::Failed ||
        policy.calls != 2) {
        return fail("platform catalog did not preserve policy Failed");
    }
    return 0;
}

int test_duplicate_and_catalog_validation() {
    constexpr std::array<std::uint8_t, 2> image{{1, 2}};
    const std::array<Elf32DependencyCatalogEntry, 2> duplicates{{
        {
            .requested_name = "libc.so",
            .identity = "a",
            .image = std::span{image},
        },
        {
            .requested_name = "libc.so",
            .identity = "b",
            .image = std::span{image},
        },
    }};
    RecordingPolicy duplicate_policy;
    A32AndroidPlatformCatalogProvider duplicate_provider{
        std::span{duplicates}, duplicate_policy};
    auto result = duplicate_provider.resolve_for(
        "root", "libc.so", image.size());
    if (result.error != Elf32DependencyProviderError::Failed ||
        duplicate_policy.calls != 0) {
        return fail("duplicate exact catalog names were not rejected pre-policy");
    }

    const std::array<Elf32DependencyCatalogEntry, 1> invalid{{
        {
            .requested_name = "libc.so",
            .identity = "",
            .image = std::span{image},
        },
    }};
    RecordingPolicy invalid_policy;
    A32AndroidPlatformCatalogProvider invalid_provider{
        std::span{invalid}, invalid_policy};
    result = invalid_provider.resolve_for(
        "root", "libc.so", image.size());
    if (result.error != Elf32DependencyProviderError::Failed ||
        invalid_policy.calls != 1) {
        return fail("catalog resource validation was not retained after Allow");
    }
    return 0;
}

int test_namespace_policy_composition() {
    constexpr std::array<std::uint8_t, 3> log_image{{1, 2, 3}};
    constexpr std::array<std::uint8_t, 3> libc_image{{4, 5, 6}};
    const std::array<Elf32DependencyCatalogEntry, 2> entries{{
        {
            .requested_name = "liblog.so",
            .identity = "platform-log",
            .image = std::span{log_image},
        },
        {
            .requested_name = "libc.so",
            .identity = "platform-libc",
            .image = std::span{libc_image},
        },
    }};

    const std::array<A32AndroidNamespaceBinding, 1> bindings{{
        {"consumer", "app"},
    }};
    const std::array<std::string_view, 2> shared{{
        "liblog.so",
        "libc.so",
    }};
    const std::array<A32AndroidNamespaceLink, 1> links{{
        {"app", "platform", false, std::span{shared}},
    }};
    A32AndroidNamespaceAccessPolicy namespace_policy{
        std::span{bindings}, std::span{links}, "platform"};
    A32AndroidPlatformCatalogProvider provider{
        std::span{entries}, namespace_policy};

    const auto result = provider.resolve_for(
        "consumer", "libc.so", libc_image.size());
    if (!result || result.source.identity != "platform-libc") {
        return fail("namespace policy did not allow catalog libc entry");
    }

    const auto denied = provider.resolve_for(
        "unbound", "libc.so", libc_image.size());
    if (denied.error != Elf32DependencyProviderError::NotFound) {
        return fail("unbound requester unexpectedly accessed platform catalog");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_exact_catalog_and_policy(); status != 0) {
        return status;
    }
    if (const int status = test_unknown_and_policy_results(); status != 0) {
        return status;
    }
    if (const int status = test_duplicate_and_catalog_validation(); status != 0) {
        return status;
    }
    if (const int status = test_namespace_policy_composition(); status != 0) {
        return status;
    }
    return 0;
}
