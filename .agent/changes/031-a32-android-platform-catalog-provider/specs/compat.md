# Compatibility spec delta — feature 031

Add a finite requester-aware Android platform catalog provider over caller-owned
Elf32DependencyCatalogEntry records.

Unknown names return NotFound before policy. Duplicate exact names return Failed
before policy. A unique exact name is passed with the exact requester identity
to A32AndroidPlatformAccessPolicy; Allow reuses the existing exact catalog
provider and its validation, while NotFound/Failed propagate.

The provider owns no entry/image/string/policy storage. Keep the feature-028
single-liblog provider source-compatible.

Do not add automatic platform-catalog construction, path/search/APK behavior,
namespace inference, guest libc.so, or broader platform semantics.
