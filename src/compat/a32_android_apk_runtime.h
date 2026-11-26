#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "compat/a32_android_library_search.h"
#include "compat/a32_libdl_open_transaction.h"
#include "elf/elf32_dependency_resolver.h"
#include "elf/elf32_lifecycle.h"
#include "elf/elf32_link_map.h"
#include "memory/guest_memory.h"

namespace liba32android::compat {

struct A32AndroidApkRuntimeBootstrapOptions {
    std::uint32_t max_application_libraries{};
    std::uint32_t max_soname_bytes{};
    std::uint32_t max_abi_directory_bytes{};
    A32ApkLibrarySourceOptions source;
    A32AndroidLibrarySearchOptions search;
    A32LibDlOpenTransactionOptions open;
};

enum class A32AndroidApkRuntimeBootstrapError : std::uint8_t {
    None = 0,
    InvalidOptions,
    InvalidRootName,
    OpenFailed,
};

struct A32AndroidApkRuntimeBootstrapResult {
    A32AndroidApkRuntimeBootstrapError error{
        A32AndroidApkRuntimeBootstrapError::None};
    A32LibDlOpenTransactionResult open_result;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == A32AndroidApkRuntimeBootstrapError::None;
    }
};

// Application-local guard/root provider. Context-free lookup resolves declared
// app SONAMEs for initial bootstrap/later named dlopen. In requester-aware
// lookup it runs after the accepted app search provider and fails closed for a
// declared app SONAME that the APK search could not supply, preventing accidental
// platform substitution; undeclared names continue to platform fallback.
class A32AndroidApkRootProvider final
    : public elf::Elf32DependencyProvider {
public:
    A32AndroidApkRootProvider(
        std::span<const std::string> application_sonames,
        std::string_view search_root,
        A32AndroidLibrarySource& source,
        std::uint32_t max_path_bytes) noexcept
        : application_sonames_(application_sonames),
          search_root_(search_root),
          source_(source),
          max_path_bytes_(max_path_bytes) {}

    [[nodiscard]] elf::Elf32DependencyProviderResult resolve(
        std::string_view requested_name,
        std::uint64_t max_image_bytes) override;

    [[nodiscard]] elf::Elf32DependencyProviderResult resolve_for(
        std::string_view requester_identity,
        std::string_view requested_name,
        std::uint64_t max_image_bytes) override;

private:
    [[nodiscard]] bool declared(
        std::string_view requested_name) const noexcept;
    [[nodiscard]] bool build_candidate(
        std::string_view requested_name,
        std::string& candidate) const;

    std::span<const std::string> application_sonames_;
    std::string_view search_root_;
    A32AndroidLibrarySource& source_;
    std::uint32_t max_path_bytes_{};
};

// Persistent caller-supplied APK/ABI composition. This object owns the
// application-side provider state and one A32LibDlOpenTransaction while
// borrowing caller memory/link-map/handle/lifecycle/platform state.
class A32AndroidApkRuntimeBootstrap final {
public:
    A32AndroidApkRuntimeBootstrap(
        memory::MappedGuestMemory& memory,
        elf::Elf32LinkMap& link_map,
        elf::Elf32DependencyProvider& platform_provider,
        std::span<A32LibDlHandle> handles,
        elf::Elf32LifecycleState& lifecycle,
        std::string apk_path,
        std::string abi_directory,
        std::span<const std::string_view> application_sonames,
        A32AndroidApkRuntimeBootstrapOptions options);

    A32AndroidApkRuntimeBootstrap(
        const A32AndroidApkRuntimeBootstrap&) = delete;
    A32AndroidApkRuntimeBootstrap& operator=(
        const A32AndroidApkRuntimeBootstrap&) = delete;
    A32AndroidApkRuntimeBootstrap(
        A32AndroidApkRuntimeBootstrap&&) = delete;
    A32AndroidApkRuntimeBootstrap& operator=(
        A32AndroidApkRuntimeBootstrap&&) = delete;

    [[nodiscard]] A32AndroidApkRuntimeBootstrapResult open_root(
        std::string_view root_soname,
        std::optional<std::uint32_t> nested_stack_top = std::nullopt,
        A32LibDlOpenPolicy policy = {});

    [[nodiscard]] bool configuration_valid() const noexcept {
        return configuration_valid_;
    }

    [[nodiscard]] std::size_t application_library_count() const noexcept {
        return application_sonames_.size();
    }

    [[nodiscard]] std::string_view search_root() const noexcept {
        return search_root_;
    }

    [[nodiscard]] elf::Elf32DependencyProvider& provider() noexcept {
        return provider_chain_;
    }

    [[nodiscard]] A32LibDlOpenTransaction& open_transaction() noexcept {
        return open_transaction_;
    }

private:
    [[nodiscard]] static std::vector<std::string>
    copy_application_sonames(
        std::span<const std::string_view> application_sonames,
        const A32AndroidApkRuntimeBootstrapOptions& options);

    [[nodiscard]] static std::string build_search_root(
        std::string_view apk_path,
        std::string_view abi_directory,
        const A32AndroidApkRuntimeBootstrapOptions& options);

    [[nodiscard]] static std::vector<std::string>
    build_application_identities(
        std::string_view search_root,
        std::span<const std::string> application_sonames,
        const A32AndroidApkRuntimeBootstrapOptions& options);

    [[nodiscard]] static std::vector<A32AndroidLibrarySearchRoot>
    build_requester_roots(
        std::span<const std::string> application_identities,
        std::string_view search_root);

    [[nodiscard]] bool validate_configuration(
        std::span<const std::string_view> requested_application_sonames) const
        noexcept;

    [[nodiscard]] bool declared(
        std::string_view soname) const noexcept;

    A32AndroidApkRuntimeBootstrapOptions options_;
    std::string apk_path_;
    std::string abi_directory_;
    std::vector<std::string> application_sonames_;
    std::string search_root_;
    std::vector<std::string> application_identities_;
    std::vector<A32AndroidLibrarySearchRoot> requester_roots_;
    A32ApkLibrarySource source_;
    A32AndroidApkRootProvider root_provider_;
    A32AndroidLibrarySearchProvider application_provider_;
    elf::Elf32DependencyProvider& platform_provider_;
    std::array<elf::Elf32DependencyProvider*, 3> provider_list_;
    elf::Elf32DependencyProviderChain provider_chain_;
    A32LibDlOpenTransaction open_transaction_;
    bool configuration_valid_{};
};

[[nodiscard]] const char* to_string(
    A32AndroidApkRuntimeBootstrapError error) noexcept;

}  // namespace liba32android::compat
