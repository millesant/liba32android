#include "compat/a32_libc_memory_string.h"

.syntax unified
.arm

.macro LIBA32_STUB name, immediate
.text
.p2align 2
.global \name
.type \name, %function
\name:
    svc #\immediate
    bx lr
.size \name, .-\name
.endm

LIBA32_STUB memcpy,  LIBA32ANDROID_A32_LIBC_MEMCPY_SVC
LIBA32_STUB memset,  LIBA32ANDROID_A32_LIBC_MEMSET_SVC
LIBA32_STUB memcmp,  LIBA32ANDROID_A32_LIBC_MEMCMP_SVC
LIBA32_STUB memchr,  LIBA32ANDROID_A32_LIBC_MEMCHR_SVC
LIBA32_STUB strlen,  LIBA32ANDROID_A32_LIBC_STRLEN_SVC
LIBA32_STUB strcmp,  LIBA32ANDROID_A32_LIBC_STRCMP_SVC
LIBA32_STUB strncmp, LIBA32ANDROID_A32_LIBC_STRNCMP_SVC

.section .note.GNU-stack,"",%progbits
