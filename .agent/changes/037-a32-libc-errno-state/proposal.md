# Proposal — guest errno state and __errno service

## Intent

Connect feature-035 conversion errno publication to the actual ABI-visible
__errno symbol imported by both supplied ARM32 targets.

## State

Use one caller-selected logical guest four-byte slot. The state implements both
the errno sink and exact __errno host service.

## Publication

Strengthen the errno sink so it receives GuestMemory and can fail. Conversion
errors write Android guest errno bits directly into the configured guest slot;
__errno returns that slot's logical address.

## Thread boundary

One state corresponds to one guest thread/execution context. Thread selection is
an embedding responsibility until guest pthread/TLS support exists.

## Non-goals

No Android TLS layout, __get_tls, pthread keys, or other thread-local libc
storage.
