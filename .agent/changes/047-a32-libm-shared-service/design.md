# Design — shared ARM32 libm compatibility

## Service IDs and ABI

Assign consecutive private SVC IDs 0xC1-0xD1 to the seventeen shared math
functions. The service is stateless apart from transient host calculations.

Android ARMv7 softfp values remain in core registers:
- float argument/result: one raw rN/r0 word;
- double argument/result: little-endian register pair;
- second binary double: r2/r3;
- frexp exponent pointer: r2;
- ldexp signed exponent: r2.

Bit-cast rather than numeric-cast at the ABI boundary.

## Host-state isolation

For every host math invocation:
1. snapshot errno;
2. snapshot fenv when available;
3. call the selected std:: math primitive;
4. restore fenv;
5. restore errno.

The returned numeric value is retained while host process state is restored.
No guest errno/fenv state is published in this feature.

## frexp guest write

Compute the mantissa/exponent first. Then write the signed exponent as one
little-endian 32-bit word through GuestMemory. A null, wrapping, or unreadable
destination fails the service. Only after that successful guest write is the
double result published to r0/r1.

## Real shim

The generated libm.so has no host dependencies and consists of one direct SVC
stub per symbol. The consumer is compiled -fno-builtin and -mfloat-abi=softfp
so ordinary dynamic imports/JUMP_SLOT calls preserve the target ABI instead of
being folded into compiler builtins.

Real integration uses exact-value cases to test all wrappers and avoids making
a project-wide ULP/tolerance policy in this slice.
