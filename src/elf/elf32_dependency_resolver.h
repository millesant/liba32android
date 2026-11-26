#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "elf/elf32_linker_strings.h"

namespace liba32android::elf {

enum class Elf32DependencyProviderError : std::uint8_t {
    None = 0,
    NotFound,
    Failed,
};

struct Elf32DependencySource {
    std::string identity;
    std::vector<std::uint8_t> image;
};

struct Elf32DependencyProviderResult {
    Elf32DependencyProviderError error{Elf32DependencyProviderError::None};
    Elf32DependencySource source;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32DependencyProviderError::None;
    }
};

class Elf32DependencyProvider {
public:
    virtual ~Elf32DependencyProvider() = default;

    [[nodiscard]] virtual Elf32DependencyProviderResult resolve(
        std::string_view requested_name,
        std::uint64_t max_image_bytes) = 0;

    // Additive requester-aware seam. Existing providers need not override it:
    // the default preserves the context-free resolve() behavior exactly.
    // requester_identity is borrowed only for this synchronous call.
    [[nodiscard]] virtual Elf32DependencyProviderResult resolve_for(
        std::string_view requester_identity,
        std::string_view requested_name,
        std::uint64_t max_image_bytes) {
        static_cast<void>(requester_identity);
        return resolve(requested_name, max_image_bytes);
    }
};

class Elf32DependencyProviderChain final : public Elf32DependencyProvider {
public:
    // The provider list and provider objects are caller-owned and must outlive
    // this chain. The list is finite and its order is lookup order.
    explicit Elf32DependencyProviderChain(
        std::span<Elf32DependencyProvider* const> providers) noexcept
        : providers_(providers) {}

    [[nodiscard]] Elf32DependencyProviderResult resolve(
        std::string_view requested_name,
        std::uint64_t max_image_bytes) override;

    [[nodiscard]] Elf32DependencyProviderResult resolve_for(
        std::string_view requester_identity,
        std::string_view requested_name,
        std::uint64_t max_image_bytes) override;

    [[nodiscard]] std::size_t size() const noexcept {
        return providers_.size();
    }

private:
    std::span<Elf32DependencyProvider* const> providers_;
};

struct Elf32DependencyCatalogEntry {
    // All views are caller-owned and must outlive the catalog provider.
    std::string_view requested_name;
    std::string_view identity;
    std::span<const std::uint8_t> image;
};

class Elf32DependencyCatalogProvider final : public Elf32DependencyProvider {
public:
    explicit Elf32DependencyCatalogProvider(
        std::span<const Elf32DependencyCatalogEntry> entries) noexcept
        : entries_(entries) {}

    // Exact byte-name lookup only. The base resolve_for() fallback makes this
    // requester-compatible without interpreting requester identity.
    [[nodiscard]] Elf32DependencyProviderResult resolve(
        std::string_view requested_name,
        std::uint64_t max_image_bytes) override;

    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }

private:
    std::span<const Elf32DependencyCatalogEntry> entries_;
};

struct Elf32DependencyResolveOptions {
    std::uint32_t max_dependencies{};
    std::uint64_t max_image_bytes{};
    std::uint64_t max_total_image_bytes{};
    // Optional opaque identity of the object making these DT_NEEDED requests.
    // Borrowed for the duration of resolve_elf32_dependencies() only.
    std::string_view requester_identity{};
};

struct Elf32ResolvedDependency {
    std::string requested_name;
    std::string identity;
    std::vector<std::uint8_t> image;
};

struct Elf32ResolvedDependencies {
    std::vector<Elf32ResolvedDependency> ordered;
};

enum class Elf32DependencyResolveError : std::uint8_t {
    None = 0,
    TooManyDependencies,
    EmptyDependencyName,
    DependencyNotFound,
    ProviderFailed,
    EmptyProviderIdentity,
    EmptyDependencyImage,
    ImageTooLarge,
    TotalImageBytesExceeded,
};

struct Elf32DependencyResolveResult {
    Elf32DependencyResolveError error{Elf32DependencyResolveError::None};
    Elf32ResolvedDependencies dependencies;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32DependencyResolveError::None;
    }
};

// Resolve the ordered DT_NEEDED occurrences through a caller-owned provider.
// T001 establishes ordered acquisition and ownership only. Resource/error
// hardening beyond the dependency-count and empty-name checks is added by T002.
[[nodiscard]] Elf32DependencyResolveResult resolve_elf32_dependencies(
    const Elf32LinkerStrings& strings,
    Elf32DependencyProvider& provider,
    const Elf32DependencyResolveOptions& options);

[[nodiscard]] const char* to_string(Elf32DependencyResolveError error) noexcept;

}  // namespace liba32android::elf
