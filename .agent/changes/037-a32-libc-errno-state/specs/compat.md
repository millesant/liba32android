# Compatibility spec delta — feature 037

Add private SVC 0xAD for __errno and A32LibcGuestErrnoState over one
caller-selected non-zero logical guest four-byte slot.

Strengthen A32LibcErrnoSink to receive GuestMemory and return success. The guest
errno state writes signed errno bits little-endian into its slot; integer
conversion fails if required errno publication fails.

__errno returns the logical guest slot in r0 and never a host pointer.

Use one state/slot per guest thread when threading is introduced. Do not model
Android TLS layout, __get_tls, pthread TLS keys, or other thread-local state in
this feature.
