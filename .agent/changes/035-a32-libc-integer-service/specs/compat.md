# Compatibility spec delta — feature 035

Add bounded ARM32 atoi/strtol host services at private shared SVC IDs 0xAB and
0xAC.

Model ARM32 int/long results as signed 32-bit r0 bits. strtol consumes r0 input,
r1 optional guest char** endptr slot, and signed r2 base; atoi uses base 10 and
no endptr.

Bound bytes examined. Implement ASCII whitespace/sign, base 0 or 2-36, guarded
0x/0b prefixes, octal/decimal auto-base, original-input no-digit endptr, signed
saturation, and continued digit consumption after overflow.

Publish Android guest EINVAL=22/ERANGE=34 through a caller-owned errno sink; do
not touch host errno. Write a non-null guest endptr before errno/r0 publication.

No guest exports, __errno/TLS, locale-aware ctype, unsigned/wide/64-bit or
floating conversion are part of this feature.
