#include "compat/a32_android_apk_runtime.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

[[nodiscard]] bool valid_bare_soname(
    std::string_view value,
    std::uint32_t max_bytes) noexcept {
    return max_bytes != 0U &&
           !value.empty() &&
           value.size() <= max_bytes &&
           !contains_nul(value) &&
           value.find('/') == std::string_view::npos &&
           value.find('\\') == std::string_view::npos;
}

[[nodiscard]] bool valid_abi_directory(
    std::string_view value,
    std::uint32_t max_bytes) noexcept {
    if (max_bytes == 0U ||
        value.empty() ||
        value.size() > max_bytes ||
        contains_nul(value) ||
        value.front() == '/' ||
        value.back() == '/' ||
        value.find('\\') != std::string_view::npos ||
        value.find('!') != std::string_view::npos) {
        return false;
    }

    std::size_t cursor = 0U;
    while (cursor < value.size()) {
        const std::size_t separator = value.find('/', cursor);
        const std::size_t end =
            separator == std::string_view::npos
                ? value.size()
                : separator;
        const std::string_view component =
            value.substr(cursor, end - cursor);
        if (component.empty() ||
            component == "." ||
            component == "..") {
            return false;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        cursor = separator + 1U;
    }
    return true;
}

[[nodiscard]] bool checked_candidate_size(
    std::size_t root_bytes,
    std::size_t name_bytes,
    std::uint32_t max_path_bytes,
    std::size_t& result) noexcept {
    if (max_path_bytes == 0U ||
        root_bytes >
            std::numeric_limits<std::size_t>::max() - 1U ||
        root_bytes + 1U >
            std::numeric_limits<std::size_t>::max() - name_bytes) {
        return false;
    }
    result = root_bytes + 1U + name_bytes;
    return result <= max_path_bytes;
}

}  // namespace

bool A32AndroidApkRootProvider::declared(
    std::string_view requested_name) const noexcept {
    return std::any_of(
        application_sonames_.begin(),
        application_sonames_.end(),
        [&](const std::string& soname) {
            return soname == requested_name;
        });
}

bool A32AndroidApkRootProvider::build_candidate(
    std::string_view requested_name,
    std::string& candidate) const {
    std::size_t bytes = 0U;
    if (!checked_candidate_size(
            search_root_.size(),
            requested_name.size(),
            max_path_bytes_,
            bytes)) {
        return false;
    }

    candidate.clear();
    candidate.reserve(bytes);
    candidate.append(search_root_.data(), search_root_.size());
    candidate.push_back('/');
    candidate.append(requested_name.data(), requested_name.size());
    return candidate.size() == bytes;
}

elf::Elf32DependencyProviderResult A32AndroidApkRootProvider::resolve(
    std::string_view requested_name,
    std::uint64_t max_image_bytes) {
    if (!declared(requested_name)) {
        return provider_failure(
            elf::Elf32DependencyProviderError::NotFound);
    }

    std::string candidate;
    if (!build_candidate(requested_name, candidate)) {
        return provider_failure(
            elf::Elf32DependencyProviderError::Failed);
    }

    A32AndroidLibrarySourceResult loaded =
        source_.load(candidate, max_image_bytes);
    switch (loaded.error) {
    case A32AndroidLibrarySourceError::NotFound:
        return provider_failure(
            elf::Elf32DependencyProviderError::NotFound);
    case A32AndroidLibrarySourceError::Failed:
        return provider_failure(
            elf::Elf32DependencyProviderError::Failed);
    case A32AndroidLibrarySourceError::None:
        break;
    }

    if (loaded.identity.empty() ||
        loaded.image.empty() ||
        loaded.image.size() > max_image_bytes) {
        return provider_failure(
            elf::Elf32DependencyProviderError::Failed);
    }

    elf::Elf32DependencyProviderResult result;
    result.source.identity = std::move(loaded.identity);
    result.source.image = std::move(loaded.image);
    return result;
}

elf::Elf32DependencyProviderResult A32AndroidApkRootProvider::resolve_for(
    std::string_view requester_identity,
    std::string_view requested_name,
    std::uint64_t max_image_bytes) {
    if (!requester_identity.empty()) {
        return provider_failure(
            elf::Elf32DependencyProviderError::NotFound);
    }
    return resolve(requested_name, max_image_bytes);
}

std::vector<std::string>
A32AndroidApkRuntimeBootstrap::copy_application_sonames(
    std::span<const std::string_view> application_sonames,
    const A32AndroidApkRuntimeBootstrapOptions& options) {
    std::vector<std::string> copied;
    if (options.max_application_libraries == 0U ||
        options.max_soname_bytes == 0U ||
        application_sonames.empty() ||
        application_sonames.size() >
            options.max_application_libraries) {
        return copied;
    }

    copied.reserve(application_sonames.size());
    for (const std::string_view soname : application_sonames) {
        if (!valid_bare_soname(
                soname, options.max_soname_bytes)) {
            copied.clear();
            return copied;
        }
        const bool duplicate = std::any_of(
            copied.begin(), copied.end(),
            [&](const std::string& existing) {
                return existing == soname;
            });
        if (duplicate) {
            copied.clear();
            return copied;
        }
        copied.emplace_back(soname);
    }
    return copied;
}

std::string A32AndroidApkRuntimeBootstrap::build_search_root(
    std::string_view apk_path,
    std::string_view abi_directory,
    const A32AndroidApkRuntimeBootstrapOptions& options) {
    if (apk_path.empty() ||
        contains_nul(apk_path) ||
        apk_path.find("!/") != std::string_view::npos ||
        !valid_abi_directory(
            abi_directory, options.max_abi_directory_bytes) ||
        options.source.max_virtual_path_bytes == 0U ||
        options.search.max_path_bytes == 0U) {
        return {};
    }

    const std::size_t delimiter_bytes = 2U;
    if (apk_path.size() >
            std::numeric_limits<std::size_t>::max() -
                delimiter_bytes ||
        apk_path.size() + delimiter_bytes >
            std::numeric_limits<std::size_t>::max() -
                abi_directory.size()) {
        return {};
    }
    const std::size_t bytes =
        apk_path.size() + delimiter_bytes + abi_directory.size();
    if (bytes > options.source.max_virtual_path_bytes ||
        bytes > options.search.max_path_bytes) {
        return {};
    }

    std::string result;
    result.reserve(bytes);
    result.append(apk_path.data(), apk_path.size());
    result.append("!/");
    result.append(
        abi_directory.data(), abi_directory.size());
    return result;
}

std::vector<std::string>
A32AndroidApkRuntimeBootstrap::build_application_identities(
    std::string_view search_root,
    std::span<const std::string> application_sonames,
    const A32AndroidApkRuntimeBootstrapOptions& options) {
    std::vector<std::string> identities;
    if (search_root.empty() ||
        application_sonames.empty()) {
        return identities;
    }

    identities.reserve(application_sonames.size());
    for (const std::string& soname : application_sonames) {
        std::size_t bytes = 0U;
        if (!checked_candidate_size(
                search_root.size(),
                soname.size(),
                std::min(
                    options.source.max_virtual_path_bytes,
                    options.search.max_path_bytes),
                bytes)) {
            identities.clear();
            return identities;
        }
        std::string identity;
        identity.reserve(bytes);
        identity.append(search_root.data(), search_root.size());
        identity.push_back('/');
        identity.append(soname);
        identities.push_back(std::move(identity));
    }
    return identities;
}

std::vector<A32AndroidLibrarySearchRoot>
A32AndroidApkRuntimeBootstrap::build_requester_roots(
    std::span<const std::string> application_identities,
    std::string_view search_root) {
    std::vector<A32AndroidLibrarySearchRoot> roots;
    if (application_identities.empty() || search_root.empty()) {
        return roots;
    }

    roots.reserve(application_identities.size());
    for (const std::string& identity : application_identities) {
        roots.push_back(A32AndroidLibrarySearchRoot{
            .requester_identity = identity,
            .root = search_root,
        });
    }
    return roots;
}

A32AndroidApkRuntimeBootstrap::A32AndroidApkRuntimeBootstrap(
    memory::MappedGuestMemory& memory,
    elf::Elf32LinkMap& link_map,
    elf::Elf32DependencyProvider& platform_provider,
    std::span<A32LibDlHandle> handles,
    elf::Elf32LifecycleState& lifecycle,
    std::string apk_path,
    std::string abi_directory,
    std::span<const std::string_view> application_sonames,
    A32AndroidApkRuntimeBootstrapOptions options)
    : options_(std::move(options)),
      apk_path_(std::move(apk_path)),
      abi_directory_(std::move(abi_directory)),
      application_sonames_(
          copy_application_sonames(
              application_sonames, options_)),
      search_root_(
          build_search_root(
              apk_path_, abi_directory_, options_)),
      application_identities_(
          build_application_identities(
              search_root_,
              application_sonames_,
              options_)),
      requester_roots_(
          build_requester_roots(
              application_identities_,
              search_root_)),
      source_(options_.source),
      root_provider_(
          std::span<const std::string>{application_sonames_},
          search_root_,
          source_,
          options_.search.max_path_bytes),
      application_provider_(
          std::span<const A32AndroidLibrarySearchRoot>{
              requester_roots_},
          source_,
          options_.search),
      platform_provider_(platform_provider),
      provider_list_{{
          &root_provider_,
          &application_provider_,
          &platform_provider_,
      }},
      provider_chain_{
          std::span<elf::Elf32DependencyProvider* const>{
              provider_list_}},
      open_transaction_(
          memory,
          link_map,
          provider_chain_,
          handles,
          lifecycle,
          options_.open),
      configuration_valid_(
          validate_configuration(application_sonames)) {}

bool A32AndroidApkRuntimeBootstrap::validate_configuration(
    std::span<const std::string_view>
        requested_application_sonames) const noexcept {
    return options_.max_application_libraries != 0U &&
           options_.max_soname_bytes != 0U &&
           options_.max_abi_directory_bytes != 0U &&
           options_.source.max_virtual_path_bytes != 0U &&
           options_.source.max_archive_bytes != 0U &&
           options_.source.max_entries != 0U &&
           options_.source.max_central_directory_bytes != 0U &&
           options_.source.max_entry_name_bytes != 0U &&
           options_.search.max_path_bytes != 0U &&
           !apk_path_.empty() &&
           !abi_directory_.empty() &&
           !search_root_.empty() &&
           !requested_application_sonames.empty() &&
           application_sonames_.size() ==
               requested_application_sonames.size() &&
           application_identities_.size() ==
               application_sonames_.size() &&
           requester_roots_.size() ==
               application_sonames_.size();
}

bool A32AndroidApkRuntimeBootstrap::declared(
    std::string_view soname) const noexcept {
    return std::any_of(
        application_sonames_.begin(),
        application_sonames_.end(),
        [&](const std::string& candidate) {
            return candidate == soname;
        });
}

A32AndroidApkRuntimeBootstrapResult
A32AndroidApkRuntimeBootstrap::open_root(
    std::string_view root_soname,
    std::optional<std::uint32_t> nested_stack_top,
    A32LibDlOpenPolicy policy) {
    A32AndroidApkRuntimeBootstrapResult result;
    if (!configuration_valid_) {
        result.error =
            A32AndroidApkRuntimeBootstrapError::InvalidOptions;
        return result;
    }
    if (!declared(root_soname)) {
        result.error =
            A32AndroidApkRuntimeBootstrapError::InvalidRootName;
        return result;
    }

    result.open_result = open_transaction_.open(
        root_soname, nested_stack_top, policy);
    if (!result.open_result) {
        result.error =
            A32AndroidApkRuntimeBootstrapError::OpenFailed;
    }
    return result;
}

const char* to_string(
    A32AndroidApkRuntimeBootstrapError error) noexcept {
    switch (error) {
    case A32AndroidApkRuntimeBootstrapError::None:
        return "none";
    case A32AndroidApkRuntimeBootstrapError::InvalidOptions:
        return "invalid_options";
    case A32AndroidApkRuntimeBootstrapError::InvalidRootName:
        return "invalid_root_name";
    case A32AndroidApkRuntimeBootstrapError::OpenFailed:
        return "open_failed";
    }
    return "unknown";
}

}  // namespace liba32android::compat
