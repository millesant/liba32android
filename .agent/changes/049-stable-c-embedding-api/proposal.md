# Proposal — stable C embedding API version 1

The project now has accepted engine-independent CPU, mapped-memory, service,
ELF, and Android compatibility internals, but the repository audit still records
no stable public embedding/error ABI.

Promote only the mature lowest layer: opaque runtime lifetime, logical guest
memory operations, bounded ARM/Thumb execution, exact SVC trap publication, and
structured errors.

Use a C ABI so Android/JNI, native applications, Rust FFI, and other embedders
do not depend on C++ ABI or private headers. Keep higher-level ELF/platform
composition private until its ownership/policy surface is ready to freeze.
