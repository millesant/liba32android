#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "compat/a32_android_log_shim.h"
#include "compat/a32_android_platform_provider.h"
#include "elf/elf32_dependency_resolver.h"

namespace {

using liba32android::compat::A32AndroidPlatformAccessDecision;
using liba32android::compat::A32AndroidPlatformAccessPolicy;
using liba32android::compat::A32AndroidPlatformProvider;
using liba32android::compat::kA32AndroidLogShimIdentity;
using liba32android::compat::kA32AndroidLogShimSoname;
using liba32android::elf::Elf32DependencyCatalogEntry;
using liba32android::elf::Elf32DependencyCatalogProvider;
using liba32android::elf::Elf32DependencyProvider;
using liba32android::elf::Elf32DependencyProviderChain;
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
    std::string last_requester;
    std::string last_requested;

    A32AndroidPlatformAccessDecision decide(
        std::string_view requester_identity,
        std::string_view requested_name) override {
        ++calls;
        last_requester.assign(
            requester_identity.data(), requester_identity.size());
        last_requested.assign(requested_name.data(), requested_name.size());
        return decision;
    }
};

int test_exact_requester_and_success() {
    constexpr std::array<std::uint8_t, 4> image{
        0x7f, 'E', 'L', 'F',
    };
    constexpr char requester_bytes[] = {
        'r','o','o','t','\0','i','d',
    };
    const std::string_view requester{
        requester_bytes, sizeof(requester_bytes)};

    RecordingPolicy policy;
    A32AndroidPlatformProvider provider{std::span{image}, policy};
    const auto result = provider.resolve_for(
        requester, kA32AndroidLogShimSoname, image.size());

    if (!result ||
        result.source.identity != kA32AndroidLogShimIdentity ||
        result.source.image !=
            std::vector<std::uint8_t>(image.begin(), image.end()) ||
        policy.calls != 1 ||
        policy.last_requester.size() != requester.size() ||
        policy.last_requester != requester ||
        policy.last_requested != kA32AndroidLogShimSoname ||
        provider.size() != 1) {
        return fail("platform provider did not preserve requester/name on success");
    }
    return 0;
}

int test_policy_and_resource_failures() {
    constexpr std::array<std::uint8_t, 4> image{
        1, 2, 3, 4,
    };
    RecordingPolicy policy;
    A32AndroidPlatformProvider provider{std::span{image}, policy};

    policy.decision = A32AndroidPlatformAccessDecision::NotFound;
    auto result = provider.resolve_for(
        "requester", kA32AndroidLogShimSoname, image.size());
    if (result.error != Elf32DependencyProviderError::NotFound ||
        !result.source.identity.empty() || !result.source.image.empty()) {
        return fail("platform policy NotFound was not preserved");
    }

    policy.decision = A32AndroidPlatformAccessDecision::Failed;
    result = provider.resolve_for(
        "requester", kA32AndroidLogShimSoname, image.size());
    if (result.error != Elf32DependencyProviderError::Failed ||
        !result.source.identity.empty() || !result.source.image.empty()) {
        return fail("platform policy failure was not preserved");
    }

    policy.decision = A32AndroidPlatformAccessDecision::Allow;
    result = provider.resolve_for(
        "requester", kA32AndroidLogShimSoname, image.size() - 1U);
    if (result.error != Elf32DependencyProviderError::Failed) {
        return fail("platform provider did not enforce image ceiling");
    }

    RecordingPolicy empty_policy;
    A32AndroidPlatformProvider empty_provider{
        std::span<const std::uint8_t>{}, empty_policy};
    result = empty_provider.resolve_for(
        "requester", kA32AndroidLogShimSoname, 16);
    if (result.error != Elf32DependencyProviderError::Failed) {
        return fail("platform provider accepted an empty shim image");
    }
    return 0;
}

int test_unknown_and_context_free_behavior() {
    constexpr std::array<std::uint8_t, 3> image{
        9, 8, 7,
    };
    RecordingPolicy policy;
    A32AndroidPlatformProvider provider{std::span{image}, policy};

    auto result = provider.resolve_for(
        "requester", "libdoesnotexist.so", image.size());
    if (result.error != Elf32DependencyProviderError::NotFound ||
        policy.calls != 0) {
        return fail("unknown platform name unexpectedly reached access policy");
    }

    result = provider.resolve(kA32AndroidLogShimSoname, image.size());
    if (!result || policy.calls != 1 ||
        !policy.last_requester.empty() ||
        policy.last_requested != kA32AndroidLogShimSoname) {
        return fail("context-free platform lookup did not use empty requester");
    }
    return 0;
}

int test_application_first_chain_fallback() {
    constexpr std::array<std::uint8_t, 2> app_image{1, 2};
    constexpr std::array<std::uint8_t, 3> shim_image{3, 4, 5};

    const std::array<Elf32DependencyCatalogEntry, 1> app_entries{{
        {
            .requested_name = "libapplication.so",
            .identity = "app-lib",
            .image = std::span{app_image},
        },
    }};
    Elf32DependencyCatalogProvider app_provider{std::span{app_entries}};

    RecordingPolicy policy;
    A32AndroidPlatformProvider platform_provider{
        std::span{shim_image}, policy};

    const std::array<Elf32DependencyProvider*, 2> providers{{
        &app_provider,
        &platform_provider,
    }};
    Elf32DependencyProviderChain chain{std::span{providers}};

    const auto result = chain.resolve_for(
        "root-object", kA32AndroidLogShimSoname, shim_image.size());
    if (!result ||
        result.source.identity != kA32AndroidLogShimIdentity ||
        policy.calls != 1 ||
        policy.last_requester != "root-object" ||
        policy.last_requested != kA32AndroidLogShimSoname) {
        return fail("application-first chain did not fall through to platform provider");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_exact_requester_and_success(); status != 0) {
        return status;
    }
    if (const int status = test_policy_and_resource_failures(); status != 0) {
        return status;
    }
    if (const int status = test_unknown_and_context_free_behavior(); status != 0) {
        return status;
    }
    if (const int status = test_application_first_chain_fallback(); status != 0) {
        return status;
    }
    return 0;
}
