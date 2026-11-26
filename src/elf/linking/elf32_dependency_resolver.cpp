#include "elf/elf32_dependency_resolver.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace liba32android::elf {
namespace {

[[nodiscard]] Elf32DependencyResolveResult failure(Elf32DependencyResolveError error) {
    Elf32DependencyResolveResult result;
    result.error = error;
    return result;
}

[[nodiscard]] Elf32DependencyResolveError translate_provider_error(
    Elf32DependencyProviderError error) noexcept {
    switch (error) {
    case Elf32DependencyProviderError::None:
        return Elf32DependencyResolveError::None;
    case Elf32DependencyProviderError::NotFound:
        return Elf32DependencyResolveError::DependencyNotFound;
    case Elf32DependencyProviderError::Failed:
        return Elf32DependencyResolveError::ProviderFailed;
    }
    return Elf32DependencyResolveError::ProviderFailed;
}

}  // namespace

Elf32DependencyProviderResult Elf32DependencyProviderChain::resolve(
    std::string_view requested_name,
    std::uint64_t max_image_bytes) {
    for (Elf32DependencyProvider* provider : providers_) {
        if (provider == nullptr) {
            Elf32DependencyProviderResult result;
            result.error = Elf32DependencyProviderError::Failed;
            return result;
        }

        auto result = provider->resolve(requested_name, max_image_bytes);
        if (result.error == Elf32DependencyProviderError::NotFound) {
            continue;
        }
        return result;
    }

    Elf32DependencyProviderResult result;
    result.error = Elf32DependencyProviderError::NotFound;
    return result;
}

Elf32DependencyProviderResult Elf32DependencyProviderChain::resolve_for(
    std::string_view requester_identity,
    std::string_view requested_name,
    std::uint64_t max_image_bytes) {
    for (Elf32DependencyProvider* provider : providers_) {
        if (provider == nullptr) {
            Elf32DependencyProviderResult result;
            result.error = Elf32DependencyProviderError::Failed;
            return result;
        }

        auto result = provider->resolve_for(
            requester_identity, requested_name, max_image_bytes);
        if (result.error == Elf32DependencyProviderError::NotFound) {
            continue;
        }
        return result;
    }

    Elf32DependencyProviderResult result;
    result.error = Elf32DependencyProviderError::NotFound;
    return result;
}

Elf32DependencyProviderResult Elf32DependencyCatalogProvider::resolve(
    std::string_view requested_name,
    std::uint64_t max_image_bytes) {
    const Elf32DependencyCatalogEntry* match = nullptr;
    for (const auto& entry : entries_) {
        if (entry.requested_name != requested_name) {
            continue;
        }
        if (match != nullptr) {
            Elf32DependencyProviderResult result;
            result.error = Elf32DependencyProviderError::Failed;
            return result;
        }
        match = &entry;
    }

    if (match == nullptr) {
        Elf32DependencyProviderResult result;
        result.error = Elf32DependencyProviderError::NotFound;
        return result;
    }

    if (match->identity.empty() || match->image.empty() ||
        static_cast<std::uint64_t>(match->image.size()) > max_image_bytes) {
        Elf32DependencyProviderResult result;
        result.error = Elf32DependencyProviderError::Failed;
        return result;
    }

    Elf32DependencyProviderResult result;
    result.source.identity.assign(match->identity.data(), match->identity.size());
    result.source.image.assign(match->image.begin(), match->image.end());
    return result;
}

Elf32DependencyResolveResult resolve_elf32_dependencies(
    const Elf32LinkerStrings& strings,
    Elf32DependencyProvider& provider,
    const Elf32DependencyResolveOptions& options) {
    if (strings.needed.size() > options.max_dependencies) {
        return failure(Elf32DependencyResolveError::TooManyDependencies);
    }

    Elf32DependencyResolveResult result;
    result.dependencies.ordered.reserve(strings.needed.size());

    std::uint64_t total_image_bytes = 0;
    for (const std::string& requested_name : strings.needed) {
        if (requested_name.empty()) {
            return failure(Elf32DependencyResolveError::EmptyDependencyName);
        }

        const std::uint64_t remaining_total =
            options.max_total_image_bytes - total_image_bytes;
        if (remaining_total == 0) {
            return failure(Elf32DependencyResolveError::TotalImageBytesExceeded);
        }
        if (options.max_image_bytes == 0) {
            return failure(Elf32DependencyResolveError::ImageTooLarge);
        }

        const std::uint64_t request_limit =
            std::min(options.max_image_bytes, remaining_total);
        Elf32DependencyProviderResult provider_result =
            provider.resolve_for(options.requester_identity,
                                 requested_name,
                                 request_limit);
        if (!provider_result) {
            return failure(translate_provider_error(provider_result.error));
        }

        if (provider_result.source.identity.empty()) {
            return failure(Elf32DependencyResolveError::EmptyProviderIdentity);
        }
        if (provider_result.source.image.empty()) {
            return failure(Elf32DependencyResolveError::EmptyDependencyImage);
        }

        const std::uint64_t image_bytes =
            static_cast<std::uint64_t>(provider_result.source.image.size());
        if (image_bytes > request_limit) {
            return failure(remaining_total < options.max_image_bytes
                               ? Elf32DependencyResolveError::TotalImageBytesExceeded
                               : Elf32DependencyResolveError::ImageTooLarge);
        }
        if (image_bytes > options.max_total_image_bytes - total_image_bytes) {
            return failure(Elf32DependencyResolveError::TotalImageBytesExceeded);
        }
        total_image_bytes += image_bytes;

        result.dependencies.ordered.push_back(Elf32ResolvedDependency{
            .requested_name = requested_name,
            .identity = std::move(provider_result.source.identity),
            .image = std::move(provider_result.source.image),
        });
    }

    return result;
}

const char* to_string(Elf32DependencyResolveError error) noexcept {
    switch (error) {
    case Elf32DependencyResolveError::None:
        return "none";
    case Elf32DependencyResolveError::TooManyDependencies:
        return "too_many_dependencies";
    case Elf32DependencyResolveError::EmptyDependencyName:
        return "empty_dependency_name";
    case Elf32DependencyResolveError::DependencyNotFound:
        return "dependency_not_found";
    case Elf32DependencyResolveError::ProviderFailed:
        return "provider_failed";
    case Elf32DependencyResolveError::EmptyProviderIdentity:
        return "empty_provider_identity";
    case Elf32DependencyResolveError::EmptyDependencyImage:
        return "empty_dependency_image";
    case Elf32DependencyResolveError::ImageTooLarge:
        return "image_too_large";
    case Elf32DependencyResolveError::TotalImageBytesExceeded:
        return "total_image_bytes_exceeded";
    }
    return "unknown";
}

}  // namespace liba32android::elf
