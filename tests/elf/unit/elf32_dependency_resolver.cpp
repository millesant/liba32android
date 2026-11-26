#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "elf/elf32_dependency_resolver.h"

namespace {

using liba32android::elf::Elf32DependencyCatalogEntry;
using liba32android::elf::Elf32DependencyCatalogProvider;
using liba32android::elf::Elf32DependencyProvider;
using liba32android::elf::Elf32DependencyProviderChain;
using liba32android::elf::Elf32DependencyProviderError;
using liba32android::elf::Elf32DependencyProviderResult;
using liba32android::elf::Elf32DependencyResolveError;
using liba32android::elf::Elf32DependencyResolveOptions;
using liba32android::elf::Elf32DependencySource;
using liba32android::elf::Elf32LinkerStrings;
using liba32android::elf::resolve_elf32_dependencies;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

Elf32DependencyProviderResult success(std::string identity,
                                      std::vector<std::uint8_t> image) {
    Elf32DependencyProviderResult result;
    result.source.identity = std::move(identity);
    result.source.image = std::move(image);
    return result;
}

class RecordingProvider final : public Elf32DependencyProvider {
public:
    std::vector<Elf32DependencyProviderResult> responses;
    std::vector<std::string> requests;
    std::vector<std::uint64_t> limits;

    Elf32DependencyProviderResult resolve(
        std::string_view requested_name,
        std::uint64_t max_image_bytes) override {
        requests.emplace_back(requested_name.data(), requested_name.size());
        limits.push_back(max_image_bytes);

        const std::size_t index = requests.size() - 1;
        if (index >= responses.size()) {
            Elf32DependencyProviderResult result;
            result.error = Elf32DependencyProviderError::Failed;
            return result;
        }
        return responses[index];
    }
};

class RequesterRecordingProvider final : public Elf32DependencyProvider {
public:
    std::vector<Elf32DependencyProviderResult> responses;
    std::vector<std::string> requesters;
    std::vector<std::string> requests;
    std::vector<std::uint64_t> limits;
    std::size_t legacy_calls{};

    Elf32DependencyProviderResult resolve(
        std::string_view,
        std::uint64_t) override {
        ++legacy_calls;
        Elf32DependencyProviderResult result;
        result.error = Elf32DependencyProviderError::Failed;
        return result;
    }

    Elf32DependencyProviderResult resolve_for(
        std::string_view requester_identity,
        std::string_view requested_name,
        std::uint64_t max_image_bytes) override {
        requesters.emplace_back(requester_identity.data(),
                                requester_identity.size());
        requests.emplace_back(requested_name.data(), requested_name.size());
        limits.push_back(max_image_bytes);
        const std::size_t index = requests.size() - 1;
        if (index >= responses.size()) {
            Elf32DependencyProviderResult result;
            result.error = Elf32DependencyProviderError::Failed;
            return result;
        }
        return responses[index];
    }
};

Elf32DependencyResolveOptions options(std::uint32_t max_dependencies = 8) {
    return Elf32DependencyResolveOptions{
        .max_dependencies = max_dependencies,
        .max_image_bytes = 4096,
        .max_total_image_bytes = 16384,
    };
}

int test_ordered_success_and_owned_results() {
    Elf32LinkerStrings strings;
    strings.needed = {"liba.so", "libb.so"};

    RecordingProvider provider;
    provider.responses = {
        success("provider:a", {0x7f, 'E', 'L', 'F', 1}),
        success("provider:b", {1, 2, 3, 4}),
    };

    auto result = resolve_elf32_dependencies(strings, provider, options());
    if (!result) return fail("ordered dependency resolution failed");
    if (provider.requests != std::vector<std::string>{"liba.so", "libb.so"}) {
        return fail("provider request order did not match DT_NEEDED order");
    }
    if (provider.limits != std::vector<std::uint64_t>{4096, 4096}) {
        return fail("provider did not receive the configured image ceiling");
    }
    if (result.dependencies.ordered.size() != 2 ||
        result.dependencies.ordered[0].requested_name != "liba.so" ||
        result.dependencies.ordered[0].identity != "provider:a" ||
        result.dependencies.ordered[0].image !=
            std::vector<std::uint8_t>{0x7f, 'E', 'L', 'F', 1} ||
        result.dependencies.ordered[1].requested_name != "libb.so" ||
        result.dependencies.ordered[1].identity != "provider:b" ||
        result.dependencies.ordered[1].image !=
            std::vector<std::uint8_t>{1, 2, 3, 4}) {
        return fail("resolved dependencies did not preserve ordered owned results");
    }

    provider.responses.clear();
    if (result.dependencies.ordered[0].identity != "provider:a" ||
        result.dependencies.ordered[0].image.size() != 5) {
        return fail("resolved dependency result depended on provider storage lifetime");
    }
    return 0;
}

int test_requester_context_is_forwarded_exactly() {
    Elf32LinkerStrings strings;
    strings.needed = {"first.so", "second.so"};

    RequesterRecordingProvider provider;
    provider.responses = {
        success("first-id", {1}),
        success("second-id", {2}),
    };

    const std::string requester{"root\0id", 7};
    auto configured = options();
    configured.requester_identity =
        std::string_view{requester.data(), requester.size()};

    const auto result =
        resolve_elf32_dependencies(strings, provider, configured);
    const std::vector<std::string> expected_requesters{requester, requester};
    if (!result ||
        provider.requesters != expected_requesters ||
        provider.requests !=
            std::vector<std::string>{"first.so", "second.so"} ||
        provider.limits != std::vector<std::uint64_t>{4096, 4096} ||
        provider.legacy_calls != 0 ||
        result.dependencies.ordered.size() != 2) {
        return fail("requester-aware provider context was not forwarded exactly");
    }

    // A provider that only implements the original context-free resolve()
    // method must keep working even when the caller supplies requester context.
    RecordingProvider legacy;
    legacy.responses = {
        success("legacy-first", {3}),
        success("legacy-second", {4}),
    };
    const auto legacy_result =
        resolve_elf32_dependencies(strings, legacy, configured);
    if (!legacy_result ||
        legacy.requests !=
            std::vector<std::string>{"first.so", "second.so"} ||
        legacy.limits != std::vector<std::uint64_t>{4096, 4096}) {
        return fail("legacy provider fallback changed under requester context");
    }
    return 0;
}

int test_provider_chain_order_fallback_and_failures() {
    const std::string requester{"requester\0id", 12};
    const std::string request{"lib\0x.so", 8};

    {
        RequesterRecordingProvider local;
        RequesterRecordingProvider platform;
        Elf32DependencyProviderResult missing;
        missing.error = Elf32DependencyProviderError::NotFound;
        local.responses = {missing};
        platform.responses = {success("platform-id", {1, 2, 3})};
        std::array<Elf32DependencyProvider*, 2> providers{&local, &platform};
        Elf32DependencyProviderChain chain{
            std::span<Elf32DependencyProvider* const>{providers}};

        const auto result = chain.resolve_for(
            std::string_view{requester.data(), requester.size()},
            std::string_view{request.data(), request.size()},
            77);
        if (!result || result.source.identity != "platform-id" ||
            local.requesters != std::vector<std::string>{requester} ||
            platform.requesters != std::vector<std::string>{requester} ||
            local.requests != std::vector<std::string>{request} ||
            platform.requests != std::vector<std::string>{request} ||
            local.limits != std::vector<std::uint64_t>{77} ||
            platform.limits != std::vector<std::uint64_t>{77}) {
            return fail("provider chain did not preserve NotFound fallback/order/context");
        }
    }

    {
        RequesterRecordingProvider broken;
        RequesterRecordingProvider unused;
        Elf32DependencyProviderResult failed;
        failed.error = Elf32DependencyProviderError::Failed;
        broken.responses = {failed};
        unused.responses = {success("unused", {9})};
        std::array<Elf32DependencyProvider*, 2> providers{&broken, &unused};
        Elf32DependencyProviderChain chain{
            std::span<Elf32DependencyProvider* const>{providers}};

        const auto result = chain.resolve_for("root", "x.so", 55);
        if (result.error != Elf32DependencyProviderError::Failed ||
            broken.requests != std::vector<std::string>{"x.so"} ||
            !unused.requests.empty()) {
            return fail("provider chain did not short-circuit hard failure");
        }
    }

    {
        RequesterRecordingProvider first;
        RequesterRecordingProvider unused;
        first.responses = {success("first", {4})};
        unused.responses = {success("unused", {5})};
        std::array<Elf32DependencyProvider*, 2> providers{&first, &unused};
        Elf32DependencyProviderChain chain{
            std::span<Elf32DependencyProvider* const>{providers}};

        const auto result = chain.resolve_for("root", "x.so", 44);
        if (!result || result.source.identity != "first" ||
            first.requests != std::vector<std::string>{"x.so"} ||
            !unused.requests.empty()) {
            return fail("provider chain did not stop at first success");
        }
    }

    {
        RequesterRecordingProvider split;
        split.responses = {success("requester-only", {6})};
        std::array<Elf32DependencyProvider*, 1> providers{&split};
        Elf32DependencyProviderChain chain{
            std::span<Elf32DependencyProvider* const>{providers}};
        const auto result = chain.resolve("x.so", 33);
        if (result.error != Elf32DependencyProviderError::Failed ||
            split.legacy_calls != 1 || !split.requesters.empty()) {
            return fail("context-free provider-chain call did not preserve child resolve semantics");
        }
    }

    {
        std::array<Elf32DependencyProvider*, 0> providers{};
        Elf32DependencyProviderChain chain{
            std::span<Elf32DependencyProvider* const>{providers}};
        const auto result = chain.resolve_for("root", "x.so", 1);
        if (result.error != Elf32DependencyProviderError::NotFound) {
            return fail("empty provider chain did not report NotFound");
        }
    }

    {
        std::array<Elf32DependencyProvider*, 1> providers{nullptr};
        Elf32DependencyProviderChain chain{
            std::span<Elf32DependencyProvider* const>{providers}};
        const auto result = chain.resolve_for("root", "x.so", 1);
        if (result.error != Elf32DependencyProviderError::Failed) {
            return fail("null provider-chain entry was silently skipped");
        }
    }

    return 0;
}

int test_provider_chain_legacy_fallback_and_resolver_validation() {
    RecordingProvider legacy;
    legacy.responses = {success("legacy", {1, 2})};
    std::array<Elf32DependencyProvider*, 1> providers{&legacy};
    Elf32DependencyProviderChain chain{
        std::span<Elf32DependencyProvider* const>{providers}};

    Elf32LinkerStrings strings;
    strings.needed = {"legacy.so"};
    auto configured = options();
    configured.requester_identity = "opaque-root";

    const auto result =
        resolve_elf32_dependencies(strings, chain, configured);
    if (!result ||
        legacy.requests != std::vector<std::string>{"legacy.so"} ||
        legacy.limits != std::vector<std::uint64_t>{4096} ||
        result.dependencies.ordered.size() != 1 ||
        result.dependencies.ordered[0].identity != "legacy") {
        return fail("provider chain broke legacy child fallback");
    }

    RecordingProvider invalid;
    invalid.responses = {success("", {7})};
    std::array<Elf32DependencyProvider*, 1> invalid_providers{&invalid};
    Elf32DependencyProviderChain invalid_chain{
        std::span<Elf32DependencyProvider* const>{invalid_providers}};
    const auto invalid_result =
        resolve_elf32_dependencies(strings, invalid_chain, configured);
    if (invalid_result.error !=
        Elf32DependencyResolveError::EmptyProviderIdentity) {
        return fail("provider chain bypassed resolver result validation");
    }
    return 0;
}

int test_catalog_provider_exact_bytes_and_owned_results() {
    std::string raw_name{
        'l', 'i', 'b', static_cast<char>(0xff), '\0', 'x',
    };
    std::string identity{"id\0raw", 6};
    std::vector<std::uint8_t> image{0x7f, 0xff, 0x80, 0x01};
    std::array<std::uint8_t, 0> empty_image{};

    const std::array entries{
        Elf32DependencyCatalogEntry{
            .requested_name = "unrelated-bad.so",
            .identity = "ignored",
            .image = std::span<const std::uint8_t>{empty_image},
        },
        Elf32DependencyCatalogEntry{
            .requested_name =
                std::string_view{raw_name.data(), raw_name.size()},
            .identity = std::string_view{identity.data(), identity.size()},
            .image =
                std::span<const std::uint8_t>{image.data(), image.size()},
        },
    };
    Elf32DependencyCatalogProvider catalog{
        std::span<const Elf32DependencyCatalogEntry>{entries}};

    const auto result = catalog.resolve_for(
        "requester-is-ignored",
        std::string_view{raw_name.data(), raw_name.size()},
        image.size());
    if (!result ||
        result.source.identity != std::string{"id\0raw", 6} ||
        result.source.image !=
            std::vector<std::uint8_t>{0x7f, 0xff, 0x80, 0x01}) {
        return fail("catalog provider did not preserve exact raw bytes");
    }

    identity[0] = 'X';
    image[0] = 0;
    if (result.source.identity != std::string{"id\0raw", 6} ||
        result.source.image[0] != 0x7f) {
        return fail("catalog provider result depended on backing storage");
    }

    const auto miss = catalog.resolve("libx.so", 16);
    if (miss.error != Elf32DependencyProviderError::NotFound) {
        return fail("catalog provider normalized or basename-matched a miss");
    }
    return 0;
}

int test_catalog_provider_rejects_ambiguous_malformed_and_oversize_entries() {
    const std::array<std::uint8_t, 1> one{1};
    const std::array<std::uint8_t, 3> three{1, 2, 3};
    const std::array<std::uint8_t, 0> empty{};

    {
        const std::array entries{
            Elf32DependencyCatalogEntry{
                .requested_name = "dup.so",
                .identity = "first",
                .image = std::span<const std::uint8_t>{one},
            },
            Elf32DependencyCatalogEntry{
                .requested_name = "dup.so",
                .identity = "second",
                .image = std::span<const std::uint8_t>{one},
            },
        };
        Elf32DependencyCatalogProvider catalog{
            std::span<const Elf32DependencyCatalogEntry>{entries}};
        if (catalog.resolve("dup.so", 8).error !=
            Elf32DependencyProviderError::Failed) {
            return fail("catalog provider did not reject duplicate exact names");
        }
    }

    {
        const std::array entries{
            Elf32DependencyCatalogEntry{
                .requested_name = "empty-id.so",
                .identity = "",
                .image = std::span<const std::uint8_t>{one},
            },
            Elf32DependencyCatalogEntry{
                .requested_name = "empty-image.so",
                .identity = "empty-image",
                .image = std::span<const std::uint8_t>{empty},
            },
            Elf32DependencyCatalogEntry{
                .requested_name = "large.so",
                .identity = "large",
                .image = std::span<const std::uint8_t>{three},
            },
        };
        Elf32DependencyCatalogProvider catalog{
            std::span<const Elf32DependencyCatalogEntry>{entries}};
        if (catalog.resolve("empty-id.so", 8).error !=
                Elf32DependencyProviderError::Failed ||
            catalog.resolve("empty-image.so", 8).error !=
                Elf32DependencyProviderError::Failed ||
            catalog.resolve("large.so", 2).error !=
                Elf32DependencyProviderError::Failed) {
            return fail("catalog provider accepted malformed/oversized selected entry");
        }
    }
    return 0;
}

int test_catalogs_compose_application_then_platform() {
    const std::array<std::uint8_t, 2> app_image{1, 2};
    const std::array<std::uint8_t, 3> platform_image{3, 4, 5};
    const std::array app_entries{
        Elf32DependencyCatalogEntry{
            .requested_name = "libvlc.so",
            .identity = "apk:libvlc.so",
            .image = std::span<const std::uint8_t>{app_image},
        },
    };
    const std::array platform_entries{
        Elf32DependencyCatalogEntry{
            .requested_name = "libc.so",
            .identity = "platform:libc.so",
            .image = std::span<const std::uint8_t>{platform_image},
        },
    };
    Elf32DependencyCatalogProvider app{
        std::span<const Elf32DependencyCatalogEntry>{app_entries}};
    Elf32DependencyCatalogProvider platform{
        std::span<const Elf32DependencyCatalogEntry>{platform_entries}};
    std::array<Elf32DependencyProvider*, 2> providers{&app, &platform};
    Elf32DependencyProviderChain chain{
        std::span<Elf32DependencyProvider* const>{providers}};

    Elf32LinkerStrings strings;
    strings.needed = {"libvlc.so", "libc.so"};
    auto configured = options();
    configured.requester_identity = "apk:libroot.so";
    const auto result =
        resolve_elf32_dependencies(strings, chain, configured);
    if (!result || result.dependencies.ordered.size() != 2 ||
        result.dependencies.ordered[0].identity != "apk:libvlc.so" ||
        result.dependencies.ordered[0].image !=
            std::vector<std::uint8_t>{1, 2} ||
        result.dependencies.ordered[1].identity != "platform:libc.so" ||
        result.dependencies.ordered[1].image !=
            std::vector<std::uint8_t>{3, 4, 5}) {
        return fail("application/platform catalogs did not compose through provider chain");
    }
    return 0;
}

int test_repeated_occurrences_are_not_deduplicated() {
    Elf32LinkerStrings strings;
    strings.needed = {"libsame.so", "libsame.so"};

    RecordingProvider provider;
    provider.responses = {
        success("first", {1}),
        success("second", {2}),
    };

    const auto result = resolve_elf32_dependencies(strings, provider, options());
    if (!result) return fail("repeated dependency resolution failed");
    if (provider.requests != std::vector<std::string>{"libsame.so", "libsame.so"}) {
        return fail("repeated dependency did not cause one provider request per occurrence");
    }
    if (result.dependencies.ordered.size() != 2 ||
        result.dependencies.ordered[0].identity != "first" ||
        result.dependencies.ordered[1].identity != "second") {
        return fail("repeated dependency occurrences were deduplicated or reordered");
    }
    return 0;
}

int test_empty_set_and_count_precheck() {
    {
        Elf32LinkerStrings strings;
        RecordingProvider provider;
        const auto result = resolve_elf32_dependencies(strings, provider, options(0));
        if (!result || !result.dependencies.ordered.empty() || !provider.requests.empty()) {
            return fail("empty dependency set did not succeed without provider calls");
        }
    }

    {
        Elf32LinkerStrings strings;
        strings.needed = {"a.so", "b.so"};
        RecordingProvider provider;
        const auto result = resolve_elf32_dependencies(strings, provider, options(1));
        if (result.error != Elf32DependencyResolveError::TooManyDependencies ||
            !provider.requests.empty() || !result.dependencies.ordered.empty()) {
            return fail("dependency-count precheck did not fail before provider calls");
        }
    }
    return 0;
}

int test_empty_name_rejected_before_its_provider_call() {
    Elf32LinkerStrings strings;
    strings.needed = {"ok.so", ""};

    RecordingProvider provider;
    provider.responses = {
        success("ok", {1, 2, 3}),
    };

    const auto result = resolve_elf32_dependencies(strings, provider, options());
    if (result.error != Elf32DependencyResolveError::EmptyDependencyName) {
        return fail("empty dependency name was not rejected");
    }
    if (provider.requests != std::vector<std::string>{"ok.so"}) {
        return fail("provider was invoked for an empty dependency name");
    }
    if (!result.dependencies.ordered.empty()) {
        return fail("empty-name failure exposed a successful partial aggregate");
    }
    return 0;
}

int test_raw_bytes_and_slashes_forwarded_exactly() {
    Elf32LinkerStrings strings;
    const std::string raw_name{
        static_cast<char>(0xff),
        static_cast<char>(0x80),
        '/',
        'x',
    };
    strings.needed = {raw_name, "dir/sub/libx.so"};

    RecordingProvider provider;
    provider.responses = {
        success("raw", {9}),
        success("slash", {8}),
    };

    const auto result = resolve_elf32_dependencies(strings, provider, options());
    if (!result) return fail("raw dependency-name forwarding failed");
    if (provider.requests.size() != 2 ||
        provider.requests[0].size() != raw_name.size() ||
        provider.requests[0] != raw_name ||
        provider.requests[1] != "dir/sub/libx.so") {
        return fail("dependency request bytes were normalized or rewritten");
    }
    if (result.dependencies.ordered[0].requested_name != raw_name ||
        result.dependencies.ordered[1].requested_name != "dir/sub/libx.so") {
        return fail("resolved result did not retain exact requested-name bytes");
    }
    return 0;
}


int test_provider_errors_are_distinct() {
    Elf32LinkerStrings strings;
    strings.needed = {"missing.so"};

    {
        RecordingProvider provider;
        Elf32DependencyProviderResult response;
        response.error = Elf32DependencyProviderError::NotFound;
        provider.responses = {response};

        const auto result = resolve_elf32_dependencies(strings, provider, options());
        if (result.error != Elf32DependencyResolveError::DependencyNotFound ||
            !result.dependencies.ordered.empty()) {
            return fail("provider NotFound was not translated distinctly");
        }
    }

    {
        RecordingProvider provider;
        Elf32DependencyProviderResult response;
        response.error = Elf32DependencyProviderError::Failed;
        provider.responses = {response};

        const auto result = resolve_elf32_dependencies(strings, provider, options());
        if (result.error != Elf32DependencyResolveError::ProviderFailed ||
            !result.dependencies.ordered.empty()) {
            return fail("provider failure was not translated distinctly");
        }
    }
    return 0;
}

int test_provider_success_requires_identity_and_image() {
    Elf32LinkerStrings strings;
    strings.needed = {"libx.so"};

    {
        RecordingProvider provider;
        provider.responses = {success("", {1})};
        const auto result = resolve_elf32_dependencies(strings, provider, options());
        if (result.error != Elf32DependencyResolveError::EmptyProviderIdentity) {
            return fail("empty provider identity was not rejected");
        }
    }

    {
        RecordingProvider provider;
        provider.responses = {success("identity", {})};
        const auto result = resolve_elf32_dependencies(strings, provider, options());
        if (result.error != Elf32DependencyResolveError::EmptyDependencyImage) {
            return fail("empty dependency image was not rejected");
        }
    }
    return 0;
}

int test_zero_and_per_image_limits() {
    Elf32LinkerStrings strings;
    strings.needed = {"libx.so"};

    {
        RecordingProvider provider;
        Elf32DependencyResolveOptions limited = options();
        limited.max_image_bytes = 0;
        const auto result = resolve_elf32_dependencies(strings, provider, limited);
        if (result.error != Elf32DependencyResolveError::ImageTooLarge ||
            !provider.requests.empty()) {
            return fail("zero per-image budget did not fail before provider access");
        }
    }

    {
        RecordingProvider provider;
        provider.responses = {success("identity", {1, 2, 3})};
        Elf32DependencyResolveOptions limited = options();
        limited.max_image_bytes = 2;
        const auto result = resolve_elf32_dependencies(strings, provider, limited);
        if (result.error != Elf32DependencyResolveError::ImageTooLarge ||
            provider.limits != std::vector<std::uint64_t>{2} ||
            !result.dependencies.ordered.empty()) {
            return fail("oversized provider image did not respect the per-image ceiling");
        }
    }
    return 0;
}

int test_total_image_budget_and_request_ceiling() {
    Elf32LinkerStrings strings;
    strings.needed = {"a.so", "b.so"};

    {
        RecordingProvider provider;
        Elf32DependencyResolveOptions limited = options();
        limited.max_total_image_bytes = 0;
        const auto result = resolve_elf32_dependencies(strings, provider, limited);
        if (result.error != Elf32DependencyResolveError::TotalImageBytesExceeded ||
            !provider.requests.empty()) {
            return fail("zero total-image budget did not fail before provider access");
        }
    }

    {
        RecordingProvider provider;
        provider.responses = {
            success("a", {1, 2, 3}),
            success("b", {4, 5}),
        };
        Elf32DependencyResolveOptions limited = options();
        limited.max_image_bytes = 4;
        limited.max_total_image_bytes = 5;

        const auto result = resolve_elf32_dependencies(strings, provider, limited);
        if (!result ||
            provider.limits != std::vector<std::uint64_t>{4, 2} ||
            result.dependencies.ordered.size() != 2) {
            return fail("remaining total budget was not propagated as the provider ceiling");
        }
    }

    {
        RecordingProvider provider;
        provider.responses = {success("identity", {1, 2, 3, 4})};
        Elf32DependencyResolveOptions limited = options();
        limited.max_image_bytes = 8;
        limited.max_total_image_bytes = 3;

        Elf32LinkerStrings one;
        one.needed = {"libx.so"};
        const auto result = resolve_elf32_dependencies(one, provider, limited);
        if (result.error != Elf32DependencyResolveError::TotalImageBytesExceeded ||
            provider.limits != std::vector<std::uint64_t>{3}) {
            return fail("provider image exceeding the remaining total budget was misclassified");
        }
    }
    return 0;
}

int test_later_failure_returns_no_partial_aggregate() {
    Elf32LinkerStrings strings;
    strings.needed = {"a.so", "b.so"};

    RecordingProvider provider;
    Elf32DependencyProviderResult failed;
    failed.error = Elf32DependencyProviderError::NotFound;
    provider.responses = {
        success("a", {1, 2}),
        failed,
    };

    const auto result = resolve_elf32_dependencies(strings, provider, options());
    if (result.error != Elf32DependencyResolveError::DependencyNotFound ||
        provider.requests != std::vector<std::string>{"a.so", "b.so"} ||
        !result.dependencies.ordered.empty()) {
        return fail("later provider failure exposed a successful partial aggregate");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_ordered_success_and_owned_results(); status != 0) return status;
    if (const int status = test_requester_context_is_forwarded_exactly(); status != 0) return status;
    if (const int status = test_provider_chain_order_fallback_and_failures(); status != 0) return status;
    if (const int status = test_provider_chain_legacy_fallback_and_resolver_validation(); status != 0) return status;
    if (const int status = test_catalog_provider_exact_bytes_and_owned_results(); status != 0) return status;
    if (const int status = test_catalog_provider_rejects_ambiguous_malformed_and_oversize_entries(); status != 0) return status;
    if (const int status = test_catalogs_compose_application_then_platform(); status != 0) return status;
    if (const int status = test_repeated_occurrences_are_not_deduplicated(); status != 0) return status;
    if (const int status = test_empty_set_and_count_precheck(); status != 0) return status;
    if (const int status = test_empty_name_rejected_before_its_provider_call(); status != 0) return status;
    if (const int status = test_raw_bytes_and_slashes_forwarded_exactly(); status != 0) return status;
    if (const int status = test_provider_errors_are_distinct(); status != 0) return status;
    if (const int status = test_provider_success_requires_identity_and_image(); status != 0) return status;
    if (const int status = test_zero_and_per_image_limits(); status != 0) return status;
    if (const int status = test_total_image_budget_and_request_ceiling(); status != 0) return status;
    if (const int status = test_later_failure_returns_no_partial_aggregate(); status != 0) return status;
    return 0;
}
