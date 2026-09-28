#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "elf/elf32_dependency_resolver.h"

namespace liba32android::compat {

enum class A32AndroidLibrarySourceError : std::uint8_t {
    None = 0,
    NotFound,
    Failed,
};

struct A32AndroidLibrarySourceResult {
    A32AndroidLibrarySourceError error{A32AndroidLibrarySourceError::None};
    std::string identity;
    std::vector<std::uint8_t> image;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == A32AndroidLibrarySourceError::None;
    }
};

class A32AndroidLibrarySource {
public:
    virtual ~A32AndroidLibrarySource() = default;

    // virtual_path is a provider-defined Android library location, for example
    // "/data/app/.../lib/arm/libfoo.so" or
    // "base.apk!/lib/armeabi-v7a/libfoo.so". The source owns all I/O or
    // archive access; the search provider only constructs bounded candidates.
    [[nodiscard]] virtual A32AndroidLibrarySourceResult load(
        std::string_view virtual_path,
        std::uint64_t max_image_bytes) = 0;
};

struct A32ApkLibrarySourceOptions {
    std::uint32_t max_virtual_path_bytes{};
    std::uint64_t max_archive_bytes{};
    std::uint32_t max_entries{};
    std::uint64_t max_central_directory_bytes{};
    std::uint32_t max_entry_name_bytes{};
};

// Exact-entry source for ordinary single-disk ZIP32 APK paths expressed as
// "<archive>!/<entry>". The source never extracts to disk and supports only
// stored and raw-DEFLATE entries.
class A32ApkLibrarySource final : public A32AndroidLibrarySource {
public:
    explicit A32ApkLibrarySource(
        A32ApkLibrarySourceOptions options) noexcept
        : options_(options) {}

    [[nodiscard]] A32AndroidLibrarySourceResult load(
        std::string_view virtual_path,
        std::uint64_t max_image_bytes) override;

private:
    A32ApkLibrarySourceOptions options_;
};

struct A32FilesystemLibrarySourceOptions {
    std::uint32_t max_path_bytes{};
};

// Concrete regular-file source for caller-supplied filesystem roots. Paths are
// consumed exactly as provided: no canonicalization, basename rewriting,
// package-manager lookup, or APK/ZIP interpretation occurs here.
class A32FilesystemLibrarySource final : public A32AndroidLibrarySource {
public:
    explicit A32FilesystemLibrarySource(
        A32FilesystemLibrarySourceOptions options) noexcept
        : options_(options) {}

    [[nodiscard]] A32AndroidLibrarySourceResult load(
        std::string_view virtual_path,
        std::uint64_t max_image_bytes) override;

private:
    A32FilesystemLibrarySourceOptions options_;
};

struct A32AndroidLibrarySearchRoot {
    // Exact opaque requester identity produced by the dependency loader.
    std::string_view requester_identity;
    // Virtual directory/root. A trailing slash is optional.
    std::string_view root;
};

struct A32AndroidLibrarySearchOptions {
    std::uint32_t max_path_bytes{};
};

// Requester-aware application/native-library search policy. This provider is
// intentionally limited to bare SONAME requests. It scans caller-provided
// roots in order and delegates concrete filesystem/APK access to a borrowed
// source. It never interprets platform namespace links or explicit paths.
class A32AndroidLibrarySearchProvider final
    : public elf::Elf32DependencyProvider {
public:
    A32AndroidLibrarySearchProvider(
        std::span<const A32AndroidLibrarySearchRoot> roots,
        A32AndroidLibrarySource& source,
        A32AndroidLibrarySearchOptions options) noexcept
        : roots_(roots), source_(source), options_(options) {}

    A32AndroidLibrarySearchProvider(
        const A32AndroidLibrarySearchProvider&) = delete;
    A32AndroidLibrarySearchProvider& operator=(
        const A32AndroidLibrarySearchProvider&) = delete;
    A32AndroidLibrarySearchProvider(
        A32AndroidLibrarySearchProvider&&) = delete;
    A32AndroidLibrarySearchProvider& operator=(
        A32AndroidLibrarySearchProvider&&) = delete;

    [[nodiscard]] elf::Elf32DependencyProviderResult resolve(
        std::string_view requested_name,
        std::uint64_t max_image_bytes) override;

    [[nodiscard]] elf::Elf32DependencyProviderResult resolve_for(
        std::string_view requester_identity,
        std::string_view requested_name,
        std::uint64_t max_image_bytes) override;

    [[nodiscard]] std::size_t root_count() const noexcept {
        return roots_.size();
    }

private:
    [[nodiscard]] bool valid_bare_name(
        std::string_view requested_name) const noexcept;
    [[nodiscard]] bool build_candidate(
        std::string_view root,
        std::string_view requested_name,
        std::string& output) const;

    std::span<const A32AndroidLibrarySearchRoot> roots_;
    A32AndroidLibrarySource& source_;
    A32AndroidLibrarySearchOptions options_;
};

}  // namespace liba32android::compat
