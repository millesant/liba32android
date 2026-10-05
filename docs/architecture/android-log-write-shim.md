# ARM32 guest `liblog.so` shim/provider path

Status: write/print/vprint compatibility implemented; exact-head validation pending

## Goal

Connect the bounded Android logging compatibility services to the existing
ELF dependency/symbol/relocation machinery with one reproducible ARM32
`liblog.so`.

The shim now exports the three Android logging entrypoints directly required by
the supplied ARM32 FMOD/VLC evidence set:

- `__android_log_write`;
- `__android_log_print`;
- `__android_log_vprint`.

The original write path remains byte-preserving and unchanged. The print paths
add bounded AAPCS32 vararg decoding and formatting before reusing the same
caller-owned `A32AndroidLogSink` boundary.

## Private guest/host protocol

`src/compat/a32_android_log_shim.h` is the single source of truth for the
private service immediates:

- `__android_log_write` -> `0xA0`;
- `__android_log_print` -> `0x133`;
- `__android_log_vprint` -> `0x134`.

Each generated guest function is a direct ARM SVC stub followed by `bx lr`.
The stubs do not rewrite argument words, so the host compatibility services see
the exact AAPCS32 call state at the logging entrypoint.

## ARM32 varargs boundary

Android declares `__android_log_print(int, const char*, const char*, ...)`
and `__android_log_vprint(int, const char*, const char*, va_list)`.
AAPCS32 defines `va_list` as a one-word structure containing a pointer to the
current argument and requires double-word arguments to appear at double-word
alignment.

For `__android_log_print`, the three named arguments occupy r0-r2. The
formatter therefore consumes the first eligible word argument from r3 and then
continues from the guest stack, applying AAPCS32 alignment before 64-bit
arguments. For `__android_log_vprint`, r3 is the one-word ARM32 `va_list`
value and decoding proceeds only through bounded `GuestMemory` reads.

The selected formatter supports:

- `%%`;
- signed/unsigned integer `d i u o x X` with `hh h l ll j z t`;
- `c`, guest-string `s`, logical guest-pointer `p`;
- double `f F e E g G a A`;
- `- + space # 0` flags where meaningful;
- bounded numeric or `*` width and precision.

The implementation never forwards a guest format string or guest `va_list`
to a host variadic function. Numeric conversions use host formatting only after
the conversion specification has been parsed, validated, and rebuilt from
bounded state. Guest `%s` pointers are copied through `GuestMemory`, and
`%p` formats the logical 32-bit guest value rather than constructing or
exposing a host pointer.

`%n`, positional parameters, wide/long-double formatting, locale extensions,
and every unselected conversion fail the host-service call before sink
invocation.

## ELF/provider boundary

The reproducible shim is linked with SONAME `liblog.so`. The freestanding
consumer has one `DT_NEEDED liblog.so` edge and eager
`R_ARM_JUMP_SLOT` imports for all three entrypoints.

`make_a32_android_log_shim_catalog_entry` keeps stable identity
`liba32android-compat-liblog`. The generated image is still build/embedding
material rather than bytes embedded into `liba32android.so`.

## Integration path

The real ARM32 fixture:

1. loads the consumer and namespace-gated platform `liblog.so`;
2. resolves and eagerly relocates write/print/vprint;
3. executes the existing write wrapper;
4. executes a real variadic print call whose first formatted argument arrives
   in r3 and later arguments arrive on the stack;
5. executes a wrapper that constructs a real NDK ARM32 `va_list` and forwards
   it to `__android_log_vprint`;
6. verifies all three calls reach one recording sink with the expected bounded
   formatted bytes.

The print fixture covers string, signed integer, alternate hexadecimal,
64-bit integer, width/precision, and logical-pointer formatting across both
vararg entry forms.

## Limits

This is not a complete Android `liblog.so` or a general printf
implementation. Android filtering/policy remains the sink's responsibility.
There is no host `liblog` passthrough, assertion/event/buffer logging,
automatic platform-catalog installation, locale/wide formatting, arbitrary
printf extensions, JNI/graphics/audio surface, or broad application
compatibility claim.
