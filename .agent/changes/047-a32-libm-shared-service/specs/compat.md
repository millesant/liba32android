# Compatibility spec delta — feature 047

Expose private SVCs 0xC1-0xD1 for the seventeen math symbols shared by the
supplied FMOD and VLC ARMv7 targets.

Arguments/results follow Android ARMv7 softfp core-register layout. Floating
values cross the service boundary by raw bits. frexp writes its int exponent
through logical GuestMemory; ldexp reads its signed exponent from r2.

Host math is permitted as the numerical engine only if host errno and fenv are
restored after each call. Guest errno/fenv behavior is not claimed.

The generated freestanding ARM32 libm.so and softfp consumer must resolve,
eagerly relocate, and execute all seventeen symbols through namespace-gated
platform compatibility.

The wider VLC-only math surface and complete Android libm behavior remain
deferred.
