# Compatibility spec delta — feature 043

Add bounded overlap-safe memmove at private SVC 0xB2 and extend the prepared
partial ARM32 libc.so with plain memmove plus the twelve bionic ARM EABI memory
helpers.

memcpy/memmove helper variants preserve destination/source/count argument
registers. EABI memset uses (destination,count,value) and must be reordered to
the existing libc memset service convention. EABI memclr uses
(destination,count) and must dispatch a zero-valued memset. Alignment suffixes
4/8 do not create extra semantic alignment checks.

The real fixture resolves and relocates thirty current partial-libc symbols and
executes all thirty wrappers. __aeabi_atexit and persistent destructor
registration remain outside this feature.
