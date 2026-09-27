# Proposal — requester-scoped Android app-library search

The supplied VLC package demonstrates real application-local native dependency
edges inside `lib/armeabi-v7a`. The generic ELF provider seam already carries
the exact requester identity, while the Android platform provider handles only
platform compatibility libraries.

Add a compatibility-layer provider that maps exact requester identities to
ordered virtual roots and asks a caller-owned byte source for
`root/requested-soname`. Keep concrete filesystem/APK access behind the source
interface so Android path policy does not leak into the generic ELF layer and
feature 048 does not need a ZIP implementation.

Prove the seam with a reproducible real ARM32 root/child fixture where the child
is available only through an APK-style virtual path.
