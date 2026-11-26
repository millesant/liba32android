#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "compat/a32_android_log_shim.h"
#include "elf/elf32_dependency_resolver.h"

namespace liba32android::compat {

enum class A32AndroidPlatformAccessDecision : std::uint8_t {
    Allow = 0,
    NotFound,
    Failed,
};

class A32AndroidPlatformAccessPolicy {
public:
    virtual ~A32AndroidPlatformAccessPolicy() = default;

    // Both views are borrowed for this synchronous call and are forwarded
    // byte-for-byte from the requester-aware dependency-provider seam.
    [[nodiscard]] virtual A32AndroidPlatformAccessDecision decide(
        std::string_view requester_identity,
        std::string_view requested_name) = 0;
};

class A32AndroidPlatformProvider final
    : public elf::Elf32DependencyProvider {
public:
    // liblog_image and policy are caller-owned and must outlive this provider.
    // The provider is intentionally non-copyable/non-movable because its
    // internal catalog stores a span into its own entry array.
    A32AndroidPlatformProvider(
        std::span<const std::uint8_t> liblog_image,
        A32AndroidPlatformAccessPolicy& policy) noexcept
        : entries_{{make_a32_android_log_shim_catalog_entry(liblog_image)}},
          catalog_(std::span<const elf::Elf32DependencyCatalogEntry>{entries_}),
          policy_(policy) {}

    A32AndroidPlatformProvider(const A32AndroidPlatformProvider&) = delete;
    A32AndroidPlatformProvider& operator=(const A32AndroidPlatformProvider&) = delete;
    A32AndroidPlatformProvider(A32AndroidPlatformProvider&&) = delete;
    A32AndroidPlatformProvider& operator=(A32AndroidPlatformProvider&&) = delete;

    [[nodiscard]] elf::Elf32DependencyProviderResult resolve(
        std::string_view requested_name,
        std::uint64_t max_image_bytes) override;

    [[nodiscard]] elf::Elf32DependencyProviderResult resolve_for(
        std::string_view requester_identity,
        std::string_view requested_name,
        std::uint64_t max_image_bytes) override;

    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }

private:
    std::array<elf::Elf32DependencyCatalogEntry, 1> entries_;
    elf::Elf32DependencyCatalogProvider catalog_;
    A32AndroidPlatformAccessPolicy& policy_;
};

// Generic finite platform catalog for compatibility libraries beyond the
// feature-028 one-slot liblog convenience provider. The entry span, nested
// name/identity/image storage, and policy are all caller-owned for the full
// provider lifetime.
class A32AndroidPlatformCatalogProvider final
    : public elf::Elf32DependencyProvider {
public:
    A32AndroidPlatformCatalogProvider(
        std::span<const elf::Elf32DependencyCatalogEntry> entries,
        A32AndroidPlatformAccessPolicy& policy) noexcept
        : entries_(entries),
          catalog_(entries),
          policy_(policy) {}

    A32AndroidPlatformCatalogProvider(
        const A32AndroidPlatformCatalogProvider&) = delete;
    A32AndroidPlatformCatalogProvider& operator=(
        const A32AndroidPlatformCatalogProvider&) = delete;
    A32AndroidPlatformCatalogProvider(
        A32AndroidPlatformCatalogProvider&&) = delete;
    A32AndroidPlatformCatalogProvider& operator=(
        A32AndroidPlatformCatalogProvider&&) = delete;

    [[nodiscard]] elf::Elf32DependencyProviderResult resolve(
        std::string_view requested_name,
        std::uint64_t max_image_bytes) override;

    [[nodiscard]] elf::Elf32DependencyProviderResult resolve_for(
        std::string_view requester_identity,
        std::string_view requested_name,
        std::uint64_t max_image_bytes) override;

    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }

private:
    std::span<const elf::Elf32DependencyCatalogEntry> entries_;
    elf::Elf32DependencyCatalogProvider catalog_;
    A32AndroidPlatformAccessPolicy& policy_;
};

}  // namespace liba32android::compat
