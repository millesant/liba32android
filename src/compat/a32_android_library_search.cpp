#include "compat/a32_android_library_search.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace liba32android::compat {
namespace {

[[nodiscard]] elf::Elf32DependencyProviderResult provider_failure(
    elf::Elf32DependencyProviderError error) {
    elf::Elf32DependencyProviderResult result;
    result.error = error;
    return result;
}

[[nodiscard]] bool contains_nul(std::string_view value) noexcept {
    return value.find('\0') != std::string_view::npos;
}

}  // namespace

bool A32AndroidLibrarySearchProvider::valid_bare_name(
    std::string_view requested_name) const noexcept {
    if (requested_name.empty() ||
        contains_nul(requested_name) ||
        requested_name.find('/') != std::string_view::npos ||
        requested_name.find('\\') != std::string_view::npos) {
        return false;
    }
    return true;
}

bool A32AndroidLibrarySearchProvider::build_candidate(
    std::string_view root,
    std::string_view requested_name,
    std::string& output) const {
    if (options_.max_path_bytes == 0U ||
        root.empty() ||
        contains_nul(root)) {
        return false;
    }

    const bool has_separator = root.back() == '/';
    const std::uint64_t size =
        static_cast<std::uint64_t>(root.size()) +
        (has_separator ? 0U : 1U) +
        requested_name.size();
    if (size == 0U ||
        size > options_.max_path_bytes ||
        size > std::numeric_limits<std::size_t>::max()) {
        return false;
    }

    output.clear();
    output.reserve(static_cast<std::size_t>(size));
    output.append(root.data(), root.size());
    if (!has_separator) {
        output.push_back('/');
    }
    output.append(requested_name.data(), requested_name.size());
    return true;
}

elf::Elf32DependencyProviderResult
A32AndroidLibrarySearchProvider::resolve(
    std::string_view requested_name,
    std::uint64_t max_image_bytes) {
    return resolve_for({}, requested_name, max_image_bytes);
}

elf::Elf32DependencyProviderResult
A32AndroidLibrarySearchProvider::resolve_for(
    std::string_view requester_identity,
    std::string_view requested_name,
    std::uint64_t max_image_bytes) {
    if (requester_identity.empty() ||
        contains_nul(requester_identity) ||
        !valid_bare_name(requested_name)) {
        return provider_failure(elf::Elf32DependencyProviderError::NotFound);
    }

    std::string candidate;
    for (const A32AndroidLibrarySearchRoot& root : roots_) {
        if (root.requester_identity != requester_identity) {
            continue;
        }
        if (root.requester_identity.empty() ||
            !build_candidate(root.root, requested_name, candidate)) {
            return provider_failure(elf::Elf32DependencyProviderError::Failed);
        }

        A32AndroidLibrarySourceResult source_result =
            source_.load(candidate, max_image_bytes);
        switch (source_result.error) {
        case A32AndroidLibrarySourceError::NotFound:
            continue;
        case A32AndroidLibrarySourceError::Failed:
            return provider_failure(elf::Elf32DependencyProviderError::Failed);
        case A32AndroidLibrarySourceError::None:
            break;
        }

        if (source_result.identity.empty() ||
            source_result.image.empty() ||
            source_result.image.size() > max_image_bytes) {
            return provider_failure(elf::Elf32DependencyProviderError::Failed);
        }

        elf::Elf32DependencyProviderResult result;
        result.source.identity = std::move(source_result.identity);
        result.source.image = std::move(source_result.image);
        return result;
    }

    return provider_failure(elf::Elf32DependencyProviderError::NotFound);
}

}  // namespace liba32android::compat
