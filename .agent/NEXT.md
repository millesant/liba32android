# NEXT

## Active JNI track

Start from validated NewObjectV revision
`eb676dda5eac4682d3ce29c488df63ef0be3ed04`.

Scan the supplied ARMv7 VLC/FMOD artifacts for the next unsupported JNIEnv or
JavaVM table load. Record the exact artifact, symbol/callsite, byte offset,
derived table slot, and ARM32 argument/return convention before mutation.

A direct `libmla.so` wrapper already proves `NewWeakGlobalRef` at JNIEnv
slot 226 / offset `0x388`, but no matching `DeleteWeakGlobalRef` evidence
was found in the current scan. Do not open a weak-reference lifetime slice from
that unbalanced observation alone; prefer another evidenced seam with a
coherent creation/use/release contract, or obtain balanced weak-reference
evidence first.

Do not infer support from table adjacency.

## Validation

Open one bounded change for the selected seam, add focused host coverage first,
then use exact-head checks. After terminal success, leave CI and converge
evidence/state.
