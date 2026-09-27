# Proposal — shared ARM32 libm compatibility

Implement the smallest libm slice directly justified by both supplied target
sets: the exact seventeen-symbol FMOD/VLC intersection.

Translate Android ARMv7 softfp core-register arguments/results into host
floating values by bit representation, call the host math primitive, then
restore host errno/fenv so guest math cannot leak state into the embedding
process. Only frexp needs guest-memory mutation for its exponent pointer.

Generate a real ARM32 libm.so SVC shim plus freestanding softfp consumer and
prove all seventeen names through the existing namespace-gated platform catalog
and eager relocation path.

Do not broaden this change to the larger VLC-only math surface or invent guest
errno/fenv semantics.
