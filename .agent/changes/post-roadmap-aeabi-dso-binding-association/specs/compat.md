# Compatibility/ELF spec delta — lifecycle-scoped DSO association

Lifecycle call execution may borrow one caller-owned
`Elf32LifecycleExecutionContext`. Each guest INIT/INIT_ARRAY/FINI_ARRAY/FINI
call scopes `object_index` to the call's stable dependency-graph object index
and restores the prior value on every return. Nested lifecycle execution
therefore restores the outer object provenance after the inner call completes.

`A32AeabiAtexitService` may optionally borrow finite caller-owned
`A32AeabiObjectDsoBinding` storage plus that lifecycle execution context.
Legacy registration is unchanged when binding storage or object context is
absent.

A successful registration with non-zero r2 under a known object learns one
object-index <-> opaque DSO association. Repeated identical associations reuse
one binding slot. Same-object/different-DSO, same-DSO/different-object, or
binding-capacity conflicts return guest -1 and append neither registration nor
binding.

Context-free registrations and zero-DSO registrations remain valid records but
do not invent an association.

`A32LibDlCloseTransaction` resolves an object's DSO from caller explicit
bindings and/or learned registration state. Either source may satisfy lookup;
internal ambiguity or disagreement between sources is InvalidBinding.

Successful targeted physical unload forgets learned associations for reclaimed
object indexes only after root release/unmapping succeeds. Failed teardown or
reclamation preserves the learned association for retry.

This delta does not infer ownership for registrations made outside lifecycle
execution, objects that never register __aeabi_atexit, or process-wide exit.
