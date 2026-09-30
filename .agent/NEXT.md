# NEXT

## Active JNI track

Start from validated ExceptionOccurred/ExceptionClear revision
`8490e1b059792c5cbaa34def21c13356536d9db7`.

Scan the supplied ARMv7 VLC/FMOD artifacts for the next unsupported JNIEnv or
JavaVM table load. Prefer a callsite with a narrow state contract that composes
with the accepted logical-reference, member-ID, method-call, array, field, and
pending-exception models.

Do not select a JNI family from table adjacency alone. Record the exact binary,
symbol/callsite, native-table byte offset, derived slot, and ARM32 argument /
return convention before mutation.

## Validation

Open one bounded change for the selected evidenced seam, add focused host
coverage first, then use exact-head checks. After terminal CI success, leave CI
and converge the accepted state.
