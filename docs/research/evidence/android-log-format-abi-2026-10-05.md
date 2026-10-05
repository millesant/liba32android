# ARM32 Android log print/vprint ABI evidence — 2026-10-05

## Supplied-binary selection

The supplied VLC APK is
`VLC-Android-3.7.2-Beta-2-all-20260925-0117.apk`, SHA-256
`10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5`.

Its ARMv7 undefined symbols select:

- `libmla.so`: `__android_log_print`;
- `libvlc.so`: `__android_log_write`, `__android_log_print`,
  `__android_log_vprint`;
- `libvlcjni.so`: `__android_log_print`.

The existing compatibility layer already covers `__android_log_write`, so the
remaining bounded liblog gap is print/vprint.

## Android API contract

AOSP's public `liblog/include/android/log.h` declares:

- `int __android_log_print(int prio, const char* tag, const char* fmt, ...)`;
- `int __android_log_vprint(int prio, const char* tag, const char* fmt,
  va_list ap)`.

The same Android header documents printf-compatible formatting and a normal
return value of 1 when the message is written, with logging policy able to
return a negative result. The compatibility runtime therefore continues to
treat final logging/filter behavior as a caller-owned sink decision rather than
hard-coding host liblog.

Primary source:
https://android.googlesource.com/platform/system/logging/+/refs/heads/main/liblog/include/android/log.h

## AAPCS32 variadic boundary

The Arm Procedure Call Standard defines the 32-bit `va_list` representation
as a structure containing one pointer:

```c
struct __va_list {
    void *__ap;
};
```

It also states that a va_list may address any object in a parameter list and
that double-word-aligned objects appear at double-word alignment in memory.

For `__android_log_print`, the three named 32-bit arguments consume r0-r2,
so a word-sized first variadic argument may occupy r3 and subsequent arguments
continue from the caller's stack. A 64-bit variadic object follows the AAPCS32
double-word alignment rule rather than being assembled from a misaligned r3
plus stack word.

For `__android_log_vprint`, the fourth parameter is the one-word ARM32
va_list value itself. The compatibility decoder can therefore treat r3 as the
logical guest address of the next argument and advance it under the same
word/double-word alignment rules.

Primary source:
https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst

## Safety consequence

A host va_list is ABI-specific and must not be manufactured from ARM32 guest
state. The compatibility service instead decodes bounded logical argument
words itself and only formats already-decoded scalars. Guest format strings,
guest string pointers, and guest va_list storage remain GuestMemory inputs and
never become host pointers.
