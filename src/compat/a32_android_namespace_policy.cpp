#include "compat/a32_android_namespace_policy.h"

#include <string_view>

namespace liba32android::compat {

A32AndroidPlatformAccessDecision A32AndroidNamespaceAccessPolicy::decide(
    std::string_view requester_identity,
    std::string_view requested_name) {
    if (platform_namespace_.empty()) {
        return A32AndroidPlatformAccessDecision::Failed;
    }
    if (requester_identity.empty() || requested_name.empty()) {
        return A32AndroidPlatformAccessDecision::NotFound;
    }

    const A32AndroidNamespaceBinding* binding = nullptr;
    for (const A32AndroidNamespaceBinding& candidate : bindings_) {
        if (candidate.requester_identity != requester_identity) {
            continue;
        }
        if (candidate.namespace_name.empty() || binding != nullptr) {
            return A32AndroidPlatformAccessDecision::Failed;
        }
        binding = &candidate;
    }

    if (binding == nullptr) {
        return A32AndroidPlatformAccessDecision::NotFound;
    }

    if (binding->namespace_name == platform_namespace_) {
        return A32AndroidPlatformAccessDecision::Allow;
    }

    const A32AndroidNamespaceLink* link = nullptr;
    for (const A32AndroidNamespaceLink& candidate : links_) {
        if (candidate.from_namespace != binding->namespace_name ||
            candidate.to_namespace != platform_namespace_) {
            continue;
        }
        if (candidate.from_namespace.empty() ||
            candidate.to_namespace.empty() ||
            link != nullptr) {
            return A32AndroidPlatformAccessDecision::Failed;
        }
        link = &candidate;
    }

    if (link == nullptr) {
        return A32AndroidPlatformAccessDecision::NotFound;
    }

    if (link->allow_all_shared_libs) {
        if (!link->shared_libs.empty()) {
            return A32AndroidPlatformAccessDecision::Failed;
        }
        return A32AndroidPlatformAccessDecision::Allow;
    }

    if (link->shared_libs.empty()) {
        return A32AndroidPlatformAccessDecision::Failed;
    }

    bool accessible = false;
    for (const std::string_view shared_lib : link->shared_libs) {
        if (shared_lib.empty()) {
            return A32AndroidPlatformAccessDecision::Failed;
        }
        if (shared_lib == requested_name) {
            accessible = true;
        }
    }

    return accessible
        ? A32AndroidPlatformAccessDecision::Allow
        : A32AndroidPlatformAccessDecision::NotFound;
}

}  // namespace liba32android::compat
