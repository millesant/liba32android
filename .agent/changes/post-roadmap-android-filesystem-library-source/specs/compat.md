# Compatibility spec delta — concrete filesystem library source

Add `A32FilesystemLibrarySource` as a bounded concrete implementation of the
feature-048 library-source interface.

The source consumes exact non-empty NUL-free paths under a caller path ceiling,
opens only the requested path, accepts only non-empty regular files, enforces
the exact caller image-byte ceiling before publication, retries interrupted
reads, and never returns partial success. Missing path components return
NotFound; other I/O/resource failures return Failed. Success returns the exact
path as opaque identity plus owned bytes.

The source performs no canonicalization, APK/ZIP parsing, AssetManager or
package-manager lookup, namespace/search policy, or explicit-path dlopen
semantics.
