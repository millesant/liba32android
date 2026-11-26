#include "compat/a32_android_platform_provider.h"

#include <cstdint>
#include <string_view>

namespace liba32android::compat {
namespace {

[[nodiscard]] elf::Elf32DependencyProviderResult failure(
    elf::Elf32DependencyProviderError error) {
    elf::Elf32DependencyProviderResult result;
    result.error = error;
    return result;
}

[[nodiscard]] bool has_exact_catalog_name(
    std::span<const elf::Elf32DependencyCatalogEntry> entries,
    std::string_view requested_name,
    bool& duplicate) noexcept {
    bool found = false;
    duplicate = false;
    for (const elf::Elf32DependencyCatalogEntry& entry : entries) {
        if (entry.requested_name != requested_name) {
            continue;
        }
        if (found) {
            duplicate = true;
            return true;
        }
        found = true;
    }
    return found;
}

[[nodiscard]] elf::Elf32DependencyProviderResult apply_policy(
    A32AndroidPlatformAccessPolicy& policy,
    elf::Elf32DependencyCatalogProvider& catalog,
    std::string_view requester_identity,
    std::string_view requested_name,
    std::uint64_t max_image_bytes) {
    switch (policy.decide(requester_identity, requested_name)) {
    case A32AndroidPlatformAccessDecision::Allow:
        return catalog.resolve(requested_name, max_image_bytes);
    case A32AndroidPlatformAccessDecision::NotFound:
        return failure(elf::Elf32DependencyProviderError::NotFound);
    case A32AndroidPlatformAccessDecision::Failed:
        return failure(elf::Elf32DependencyProviderError::Failed);
    }
    return failure(elf::Elf32DependencyProviderError::Failed);
}

}  // namespace

elf::Elf32DependencyProviderResult A32AndroidPlatformProvider::resolve(
    std::string_view requested_name,
    std::uint64_t max_image_bytes) {
    return resolve_for({}, requested_name, max_image_bytes);
}

elf::Elf32DependencyProviderResult A32AndroidPlatformProvider::resolve_for(
    std::string_view requester_identity,
    std::string_view requested_name,
    std::uint64_t max_image_bytes) {
    // Feature 028 has one concrete platform-library slot. Unknown names remain
    // NotFound so an enclosing provider chain can continue normally.
    if (requested_name != kA32AndroidLogShimSoname) {
        return failure(elf::Elf32DependencyProviderError::NotFound);
    }
    return apply_policy(
        policy_,
        catalog_,
        requester_identity,
        requested_name,
        max_image_bytes);
}

elf::Elf32DependencyProviderResult A32AndroidPlatformCatalogProvider::resolve(
    std::string_view requested_name,
    std::uint64_t max_image_bytes) {
    return resolve_for({}, requested_name, max_image_bytes);
}

elf::Elf32DependencyProviderResult
A32AndroidPlatformCatalogProvider::resolve_for(
    std::string_view requester_identity,
    std::string_view requested_name,
    std::uint64_t max_image_bytes) {
    if (requested_name.empty()) {
        return failure(elf::Elf32DependencyProviderError::NotFound);
    }

    bool duplicate = false;
    if (!has_exact_catalog_name(entries_, requested_name, duplicate)) {
        return failure(elf::Elf32DependencyProviderError::NotFound);
    }
    if (duplicate) {
        return failure(elf::Elf32DependencyProviderError::Failed);
    }

    return apply_policy(
        policy_,
        catalog_,
        requester_identity,
        requested_name,
        max_image_bytes);
}

}  // namespace liba32android::compat
