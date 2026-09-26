#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "compat/a32_android_platform_provider.h"

namespace liba32android::compat {

struct A32AndroidNamespaceBinding {
    std::string_view requester_identity;
    std::string_view namespace_name;
};

struct A32AndroidNamespaceLink {
    std::string_view from_namespace;
    std::string_view to_namespace;
    bool allow_all_shared_libs{};
    std::span<const std::string_view> shared_libs;
};

class A32AndroidNamespaceAccessPolicy final
    : public A32AndroidPlatformAccessPolicy {
public:
    // All spans/views are caller-owned and must outlive this policy.
    // platform_namespace names the namespace in which the concrete platform
    // provider's libraries are considered to live.
    A32AndroidNamespaceAccessPolicy(
        std::span<const A32AndroidNamespaceBinding> bindings,
        std::span<const A32AndroidNamespaceLink> links,
        std::string_view platform_namespace) noexcept
        : bindings_(bindings),
          links_(links),
          platform_namespace_(platform_namespace) {}

    [[nodiscard]] A32AndroidPlatformAccessDecision decide(
        std::string_view requester_identity,
        std::string_view requested_name) override;

    [[nodiscard]] std::size_t binding_count() const noexcept {
        return bindings_.size();
    }

    [[nodiscard]] std::size_t link_count() const noexcept {
        return links_.size();
    }

private:
    std::span<const A32AndroidNamespaceBinding> bindings_;
    std::span<const A32AndroidNamespaceLink> links_;
    std::string_view platform_namespace_;
};

}  // namespace liba32android::compat
