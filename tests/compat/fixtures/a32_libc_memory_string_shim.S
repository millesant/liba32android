#include "compat/a32_libc_memory_string.h"
#include "compat/a32_libc_integer.h"
#include "compat/a32_libc_heap.h"

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
LIBA32_STUB memmove, LIBA32ANDROID_A32_LIBC_MEMMOVE_SVC
LIBA32_STUB memset,  LIBA32ANDROID_A32_LIBC_MEMSET_SVC
LIBA32_STUB memcmp,  LIBA32ANDROID_A32_LIBC_MEMCMP_SVC
LIBA32_STUB memchr,  LIBA32ANDROID_A32_LIBC_MEMCHR_SVC
LIBA32_STUB strlen,  LIBA32ANDROID_A32_LIBC_STRLEN_SVC
LIBA32_STUB strcmp,  LIBA32ANDROID_A32_LIBC_STRCMP_SVC
LIBA32_STUB strncmp, LIBA32ANDROID_A32_LIBC_STRNCMP_SVC
LIBA32_STUB memmem,  LIBA32ANDROID_A32_LIBC_MEMMEM_SVC
LIBA32_STUB strcpy,  LIBA32ANDROID_A32_LIBC_STRCPY_SVC
LIBA32_STUB strncpy, LIBA32ANDROID_A32_LIBC_STRNCPY_SVC
LIBA32_STUB atoi,    LIBA32ANDROID_A32_LIBC_ATOI_SVC
LIBA32_STUB strtol,  LIBA32ANDROID_A32_LIBC_STRTOL_SVC
LIBA32_STUB __errno, LIBA32ANDROID_A32_LIBC_ERRNO_SVC
LIBA32_STUB malloc,  LIBA32ANDROID_A32_LIBC_MALLOC_SVC
LIBA32_STUB calloc,  LIBA32ANDROID_A32_LIBC_CALLOC_SVC
LIBA32_STUB realloc, LIBA32ANDROID_A32_LIBC_REALLOC_SVC
LIBA32_STUB free,    LIBA32ANDROID_A32_LIBC_FREE_SVC


.macro LIBA32_EABI_MEMSET name
.text
.p2align 2
.global \name
.type \name, %function
\name:
    mov r3, r1
    mov r1, r2
    mov r2, r3
    svc #LIBA32ANDROID_A32_LIBC_MEMSET_SVC
    bx lr
.size \name, .-\name
.endm

.macro LIBA32_EABI_MEMCLR name
.text
.p2align 2
.global \name
.type \name, %function
\name:
    mov r2, r1
    mov r1, #0
    svc #LIBA32ANDROID_A32_LIBC_MEMSET_SVC
    bx lr
.size \name, .-\name
.endm

LIBA32_STUB __aeabi_memcpy,   LIBA32ANDROID_A32_LIBC_MEMCPY_SVC
LIBA32_STUB __aeabi_memcpy4,  LIBA32ANDROID_A32_LIBC_MEMCPY_SVC
LIBA32_STUB __aeabi_memcpy8,  LIBA32ANDROID_A32_LIBC_MEMCPY_SVC
LIBA32_STUB __aeabi_memmove,  LIBA32ANDROID_A32_LIBC_MEMMOVE_SVC
LIBA32_STUB __aeabi_memmove4, LIBA32ANDROID_A32_LIBC_MEMMOVE_SVC
LIBA32_STUB __aeabi_memmove8, LIBA32ANDROID_A32_LIBC_MEMMOVE_SVC
LIBA32_EABI_MEMSET __aeabi_memset
LIBA32_EABI_MEMSET __aeabi_memset4
LIBA32_EABI_MEMSET __aeabi_memset8
LIBA32_EABI_MEMCLR __aeabi_memclr
LIBA32_EABI_MEMCLR __aeabi_memclr4
LIBA32_EABI_MEMCLR __aeabi_memclr8

.section .note.GNU-stack,"",%progbits
