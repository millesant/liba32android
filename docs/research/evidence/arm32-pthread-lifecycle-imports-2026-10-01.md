# ARM32 pthread lifecycle import evidence — 2026-10-01

## Supplied artifacts

- ARM32 FMOD `libfmod.so`
  - sha256: `982e994c46a7f797fbd6e10df31a98d544c2bf117fe33c2292f4d6cc454a6544`
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

### VLC libc++_shared

- `0x000870d0 clock_gettime@LIBC`
- `0x000870e0 pthread_cond_signal@LIBC`
- `0x000870e4 pthread_cond_broadcast@LIBC`
- `0x000870e8 pthread_cond_wait@LIBC`
- `0x000870ec pthread_cond_timedwait@LIBC`
- `0x0008710c pthread_cond_destroy@LIBC`
- `0x00087ee8 pthread_join@LIBC`
- `0x00087eec pthread_detach@LIBC`

### VLC libmla

- `0x007f87cc pthread_self@LIBC`
- `0x007fb4dc pthread_equal@LIBC`
- `0x007fbef4 pthread_create@LIBC`
- `0x00803bac pthread_join@LIBC`

### VLC libvlc

- `0x025fbaa4 pthread_attr_init@LIBC`
- `0x025fbacc clock_gettime@LIBC`
- `0x025fbaa8 pthread_attr_setdetachstate@LIBC`
- `0x025fbaac pthread_create@LIBC`
- `0x025fbab0 pthread_attr_destroy@LIBC`
- `0x025fbab4 pthread_join@LIBC`
- `0x025fbab8 pthread_exit@LIBC`
- `0x025fbd88 pthread_attr_getstacksize@LIBC`
- `0x025fbd8c pthread_attr_setstacksize@LIBC`
- `0x025fbd90 pthread_self@LIBC`
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
- `pthread_cond_init`
- `pthread_cond_destroy`
- `pthread_cond_wait`
- `pthread_cond_timedwait`
- `pthread_cond_signal`
- `pthread_cond_broadcast`

No supplied ARM32 artifact imports pthread condattr functions, so the accepted
condvar slice keeps default condition attributes only. VLC's
`libc++_shared.so` and `libvlc.so` also import `clock_gettime`; that
guest-visible libc API is evidence for separate utility work, while the condvar
implementation uses an internal deterministic clock seam.

The bounded compatibility surface additionally includes
`pthread_attr_getdetachstate` as the read side of the accepted detach-state
attribute pair. `pthread_join` is directly imported by multiple supplied VLC
ARMv7 libraries; `pthread_detach` is directly imported by the shipped ARMv7
`libc++_shared.so`. Unrelated scheduling attributes, explicit-stack attrs,
cancellation, condition variables, and rwlocks remain outside this slice.

A supplied AArch64 `libemu32.so`
(`sha256:a467c34bc1543a2a193191ad42c4ac3a8e4a00181e83fa223abf7d42bc421119`)
also imports `pthread_attr_getdetachstate`, which is supporting evidence only;
the primary acceptance evidence for this slice remains the ARM32 FMOD/VLC set.

Static imports prove required symbol resolution and ABI reachability. The
repository's generated ARM32 lifecycle fixture separately exercises blocked
join/wake/resume and detach reclamation; focused host/ARM execution tests cover
TLS destructor iteration. None of this evidence implies host-thread behavior or
general scheduler fairness.
