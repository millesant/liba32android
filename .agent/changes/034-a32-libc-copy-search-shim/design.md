# Design — extend partial libc shim with copy/search imports

## Guest exports

Append three ARM stubs to the existing partial libc assembly:

- memmem -> svc #0xA8; bx lr
- strcpy -> svc #0xA9; bx lr
- strncpy -> svc #0xAA; bx lr

The assembly continues to include the shared compatibility header so service IDs
are not duplicated.

## Consumer

Declare the standard machine signatures and add fixture_memmem,
fixture_strcpy, and fixture_strncpy wrappers. The existing -fno-builtin build
contract remains required.

## Integration

Increase the resolved shim-symbol set from seven to ten and require each
resolved address to be a final R_ARM_JUMP_SLOT target.

Extend the service registry to ten exact SVC IDs. The wrapper execution helper
also seeds r3 so memmem's fourth AAPCS32 argument is represented directly.

Stage bounded guest strings/buffers and validate:

- memmem("alpha",5,"lp",2) returns haystack+1;
- strcpy copies alpha including NUL and returns destination;
- strncpy with count 4 copies "alph" with no invented terminator.

Each wrapper runs with service-call ceiling one and must resume to the requested
stop PC.

## CI

The existing dedicated partial-libc workflow inspects the ten shim symbols and
ten consumer JUMP_SLOT imports, then requires integration evidence reporting ten
required jump slots and ten completed service calls.

## Non-goals

No change to catalog identity, namespace semantics, generic ELF APIs, or libc
surfaces beyond these three prepared services.
