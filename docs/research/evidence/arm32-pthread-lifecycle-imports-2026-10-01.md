# ARM32 pthread lifecycle import evidence — 2026-10-01

## Supplied artifacts

- ARM32 FMOD `libfmod.so`
  - sha256: `982e994c46a7f797fbd6e10df31a98d544c2bf117fe33c2292f4d6cc454a6544`
- VLC ARMv7 `libc++_shared.so`
  - sha256: `30986ee10a51d9d9486d51d2a8b152d86b6f9c818f4e029a238b30924939b57d`
- VLC ARMv7 `libmla.so`
  - sha256: `4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962`
- VLC ARMv7 `libvlc.so`
  - sha256: `f92266dbe28b4e4e2477225cf6bbb56291c2e1ca7a72a4573013ebf9d52517d4`
- VLC ARMv7 `libvlcjni.so`
  - sha256: `e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`

The VLC libraries were extracted from the supplied
`VLC-Android-3.7.2-Beta-2-all-20260925-0117.apk` under
`lib/armeabi-v7a/`.

## Direct ARM32 relocation evidence

`readelf -rW` reports the following eager `R_ARM_JUMP_SLOT` imports.

### FMOD

- `0x00117f68 pthread_self`
- `0x00117f6c pthread_attr_init`
- `0x00117f70 pthread_attr_setdetachstate`
- `0x00117f74 pthread_create`
- `0x00117f78 pthread_attr_destroy`
- `0x00117f7c pthread_attr_setstacksize`
- `0x00117f80 pthread_mutexattr_init`
- `0x00117f84 pthread_mutexattr_settype`
- `0x00117f88 pthread_mutex_init`
- `0x00117f8c pthread_mutex_destroy`
- `0x00117f90 pthread_mutex_lock`
- `0x00117f94 pthread_mutex_unlock`
- `0x00117fe4 pthread_once`

### VLC libc++_shared

- `0x000870d0 clock_gettime@LIBC`
- `0x000870e0 pthread_cond_signal@LIBC`
- `0x000870e4 pthread_cond_broadcast@LIBC`
- `0x000870e8 pthread_cond_wait@LIBC`
- `0x000870ec pthread_cond_timedwait@LIBC`
- `0x0008710c pthread_cond_destroy@LIBC`
- `0x00087d38 pthread_mutex_trylock@LIBC`
- `0x00087d58 pthread_mutex_destroy@LIBC`
- `0x00087d68 pthread_mutexattr_init@LIBC`
- `0x00087d6c pthread_mutexattr_settype@LIBC`
- `0x00087d70 pthread_mutexattr_destroy@LIBC`
- `0x00087d74 pthread_mutex_init@LIBC`
- `0x00087f4c pthread_once@LIBC`
- `0x00087ee8 pthread_join@LIBC`
- `0x00087eec pthread_detach@LIBC`

### VLC libmla

- `0x007f81b0 pthread_once@LIBC`
- `0x007f87cc pthread_self@LIBC`
- `0x007fb4dc pthread_equal@LIBC`
- `0x007fbef4 pthread_create@LIBC`
- `0x00803b74 pthread_mutexattr_init@LIBC`
- `0x00803b78 pthread_mutexattr_settype@LIBC`
- `0x00803b7c pthread_mutex_init@LIBC`
- `0x00803b80 pthread_mutexattr_destroy@LIBC`
- `0x00803b84 pthread_mutex_destroy@LIBC`
- `0x00803b88 pthread_mutex_lock@LIBC`
- `0x00803b8c pthread_mutex_trylock@LIBC`
- `0x00803b90 pthread_mutex_unlock@LIBC`
- `0x00803bac pthread_join@LIBC`

### VLC libvlc

- `0x025fba84 pthread_mutexattr_init@LIBC`
- `0x025fba88 pthread_mutexattr_settype@LIBC`
- `0x025fba8c pthread_mutex_init@LIBC`
- `0x025fba90 pthread_mutexattr_destroy@LIBC`
- `0x025fba94 pthread_mutex_destroy@LIBC`
- `0x025fba98 pthread_mutex_lock@LIBC`
- `0x025fba9c pthread_mutex_trylock@LIBC`
- `0x025fbaa0 pthread_mutex_unlock@LIBC`
- `0x025fbaa4 pthread_attr_init@LIBC`
- `0x025fbacc clock_gettime@LIBC`
- `0x025fbaa8 pthread_attr_setdetachstate@LIBC`
- `0x025fbaac pthread_create@LIBC`
- `0x025fbab0 pthread_attr_destroy@LIBC`
- `0x025fbab4 pthread_join@LIBC`
- `0x025fbab8 pthread_exit@LIBC`
- `0x025fbafc pthread_once@LIBC`
- `0x025fbd74 pthread_cond_destroy@LIBC`
- `0x025fbd78 pthread_cond_init@LIBC`
- `0x025fbd7c pthread_cond_broadcast@LIBC`
- `0x025fbd80 pthread_cond_signal@LIBC`
- `0x025fbd84 pthread_cond_wait@LIBC`
- `0x025fbd88 pthread_attr_getstacksize@LIBC`
- `0x025fbd8c pthread_attr_setstacksize@LIBC`
- `0x025fbd90 pthread_self@LIBC`
- `0x025fbd94 pthread_setname_np@LIBC`
- `0x025fbec4 pthread_rwlock_init@LIBC`
- `0x025fbec8 pthread_rwlock_wrlock@LIBC`
- `0x025fbecc pthread_rwlock_unlock@LIBC`
- `0x025fbed0 pthread_rwlock_destroy@LIBC`
- `0x025fbedc pthread_setschedparam@LIBC`
- `0x025fbee0 pthread_cond_timedwait@LIBC`
- `0x025fbf34 pthread_rwlock_rdlock@LIBC`
- `0x025fbf48 pthread_getschedparam@LIBC`
- `0x025fbf4c pthread_equal@LIBC`

### VLC libvlcjni

- `0x00013eec pthread_cond_init@LIBC`
- `0x00013f14 pthread_cond_destroy@LIBC`
- `0x00013f3c pthread_cond_wait@LIBC`
- `0x00013f60 pthread_cond_signal@LIBC`
- `0x00013ff0 pthread_create@LIBC`
- `0x00013ffc pthread_join@LIBC`

## Resulting bounded slice

The supplied ARM32 artifacts directly require:

- `pthread_attr_init`
- `pthread_attr_destroy`
- `pthread_attr_setdetachstate`
- `pthread_attr_getstacksize`
- `pthread_attr_setstacksize`
- `pthread_create`
- `pthread_self`
- `pthread_equal`
- `pthread_exit`
- `pthread_join`
- `pthread_detach`
- `pthread_getschedparam`
- `pthread_setschedparam`
- `pthread_setname_np`
- `pthread_cond_init`
- `pthread_cond_destroy`
- `pthread_cond_wait`
- `pthread_cond_timedwait`
- `pthread_cond_signal`
- `pthread_cond_broadcast`
- `pthread_mutexattr_init`
- `pthread_mutexattr_settype`
- `pthread_mutexattr_destroy`
- `pthread_mutex_init`
- `pthread_mutex_destroy`
- `pthread_mutex_lock`
- `pthread_mutex_trylock`
- `pthread_mutex_unlock`
- `pthread_once`
- `pthread_rwlock_init`
- `pthread_rwlock_destroy`
- `pthread_rwlock_rdlock`
- `pthread_rwlock_wrlock`
- `pthread_rwlock_unlock`

No supplied ARM32 artifact imports pthread condattr functions, so the accepted
condvar slice keeps default condition attributes only. VLC's
`libc++_shared.so` and `libvlc.so` also import `clock_gettime`; that
guest-visible libc API is evidence for separate utility work, while the condvar
implementation uses an internal deterministic clock seam.

The bounded compatibility surface additionally includes
`pthread_attr_getdetachstate` as the read side of the accepted detach-state
attribute pair. `pthread_join` is directly imported by multiple supplied VLC
ARMv7 libraries; `pthread_detach` is directly imported by the shipped ARMv7
`libc++_shared.so`. A complete scan of every `lib/armeabi-v7a/*.so` in the supplied VLC APK
found the new utility imports only in ARM32 `libvlc.so`. That file has sha256
`f92266dbe28b4e4e2477225cf6bbb56291c2e1ca7a72a4573013ebf9d52517d4`.
The supplied APK has sha256
`10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5`.

Explicit-stack attrs, cancellation, cond attributes, rwlock attrs, timed
rwlocks, and pthread attribute scheduling functions remain outside the direct
supplied ARM32 evidence. The accepted #42 implementation still provides
bounded tryrdlock/trywrlock as coherent nonblocking companions covered by
focused tests.

The separately supplied `libemu32.so` is AArch64, not AArch32
(`sha256:a467c34bc1543a2a193191ad42c4ac3a8e4a00181e83fa223abf7d42bc421119`).
Its imports include `sem_trywait`, `sem_getvalue`,
`pthread_attr_getdetachstate`, `pthread_attr_setschedparam`, and
`pthread_setname_np`; those are architecture-specific supporting observations
only and do not justify new AArch32 exports. The supplied ARM32 FMOD library
(`sha256:982e994c46a7f797fbd6e10df31a98d544c2bf117fe33c2292f4d6cc454a6544`)
adds no new utility imports beyond the already accepted lifecycle/mutex set.

Static imports prove required symbol resolution and ABI reachability. The
repository's generated ARM32 lifecycle fixture separately exercises blocked
join/wake/resume, detach reclamation, condition wait/signal/reacquire/resume, deterministic timed timeout, recursive
mutex attrs, pthread_once completion, and rwlock reader/writer wake/resume;
focused host/ARM execution tests cover TLS destructor iteration, multi-waiter
condvar ordering, once interleaving/failure latching, and rwlock preference. None of this evidence implies host-thread behavior or
general scheduler fairness.

## Cleanup-handler evidence update — 2026-10-02

The supplied VLC APK's ARM32 `lib/armeabi-v7a/libvlc.so` has direct
`R_ARM_JUMP_SLOT` imports for the two Bionic cleanup helpers:

- `0x025fb8cc __pthread_cleanup_push@LIBC`
- `0x025fba04 __pthread_cleanup_pop@LIBC`

The same complete ARM32 supplied-artifact scan found no direct imports for
`pthread_cancel`, `pthread_setcancelstate`, `pthread_setcanceltype`, or
`pthread_testcancel`. Cleanup push/pop are therefore accepted as explicit
cleanup-stack / voluntary-exit behavior only; this evidence does not justify
pthread cancellation semantics.

## Signal evidence update — 2026-10-02

The supplied VLC APK's ARM32 `lib/armeabi-v7a/libvlc.so` imports:

- `pthread_sigmask@LIBC` at JUMP_SLOT `0x025fba64`
- `sigpending@LIBC` at JUMP_SLOT `0x025fba68`
- `sigwait@LIBC` at JUMP_SLOT `0x025fba6c`
- `raise@LIBC` at JUMP_SLOT `0x025fbaf0`
- `sigaction@LIBC` at JUMP_SLOT `0x025fbe98`

The `vlc_writev` disassembly shows a concrete SIGPIPE sequence using mask
`0x00001000` (signal 13): block SIGPIPE with `pthread_sigmask`, perform the
write, call `sigpending` after EPIPE, consume a pending SIGPIPE with
`sigwait`, and restore the previous mask when it was not already blocked.

Observed `raise` call sites pass signal 8 (`SIGFPE`) in fatal/error paths.
This evidence requires an explicit default-fatal delivery boundary; returning
success without delivering or terminating would be incorrect.

The supplied ARM32 artifact scan found no direct `pthread_kill` import.

Bionic primary-source ABI/behavior used for the accepted contract:

- LP32 Android ARM uses a 32-bit `sigset_t`; Bionic documents the historical
  32-bit ABI as too small for realtime signal sets.
- ARM32 `struct sigaction` is four 32-bit words in handler, mask, flags,
  restorer order.
- Bionic `pthread_sigmask` returns an error number directly while preserving
  errno.
- Bionic `sigwait` returns zero plus the selected signal on success and an
  error number on failure.
- Bionic `raise` is thread-directed; the compatibility layer preserves that
  observable scope with logical thread identity rather than host `tgkill`.

