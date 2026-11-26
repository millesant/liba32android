#include "compat/a32_libdl.h"

.syntax unified
.arm

.macro LIBA32_DL_STUB name, immediate
.text
.p2align 2
.global \name
.type \name, %function
\name:
    svc #\immediate
    bx lr
.size \name, .-\name
.endm

LIBA32_DL_STUB dlopen,  LIBA32ANDROID_A32_LIBDL_DLOPEN_SVC
LIBA32_DL_STUB dlsym,   LIBA32ANDROID_A32_LIBDL_DLSYM_SVC
LIBA32_DL_STUB dlclose, LIBA32ANDROID_A32_LIBDL_DLCLOSE_SVC
LIBA32_DL_STUB dlerror, LIBA32ANDROID_A32_LIBDL_DLERROR_SVC
LIBA32_DL_STUB dladdr,  LIBA32ANDROID_A32_LIBDL_DLADDR_SVC

.section .note.GNU-stack,"",%progbits
