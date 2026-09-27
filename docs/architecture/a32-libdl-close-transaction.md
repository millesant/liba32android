# Resident dlclose lifecycle transaction

Status: integrated; exact-head validation pending

The resident libdl close path can now be wired to a bounded exact-object
lifecycle transaction.

A finite binding maps a stable link-map object index to one opaque non-zero
guest DSO-handle word. Non-final synthetic references only decrement. The last
reference requires completed constructors and pending destructors, then runs:

`reverse FINI_ARRAY -> verify exact DSO registrations Complete -> DT_FINI -> mark Complete -> release handle`

FINI_ARRAY and DT_FINI execute through the accepted service-aware lifecycle
seam. This allows normal linked Android CRT teardown to reach
`__cxa_finalize` before DT_FINI. No alternative callback order is invented
when registrations remain pending.

Any guest teardown failure or post-FINI incomplete registration state latches
the object destructor state Failed and leaves the final synthetic handle live.
This avoids replaying potentially partial guest side effects.

The focused test exercises the full registered-destructor path. The real ARM32
libdl fixture adds one provider FINI_ARRAY marker and proves ordinary linked
`dlclose` invokes the transaction before the provider handle becomes invalid.

Dependencies remain resident; link-map indexes stay stable and no guest mapping
is reclaimed yet.
