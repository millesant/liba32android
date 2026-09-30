# ARM32 JNI_OnUnload entrypoint evidence — 2026-09-30

## Supplied artifacts

The VLC ARMv7 APK contains:

- `libmla.so`, SHA-256
  `4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962`;
- `libvlcjni.so`, SHA-256
  `e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`.

Both export `JNI_OnUnload(JavaVM*, void*)`.

## libmla.so

The symbol value is Thumb `0x002614d1`, size 132 bytes (code address
`0x002614d0`). The function passes its JavaVM to the library's GetEnv helper
for JNI 1.2 before releasing JNI-owned state.

## libvlcjni.so

The symbol value is Thumb `0x00007b5d`, size 500 bytes (code address
`0x00007b5c`).

At `0x00007b74` it dereferences the JavaVM invoke table, loads byte offset
`0x18` (slot 6, `GetEnv`), passes JNI 1.2, and calls the function pointer.
After successful JNIEnv acquisition, the function repeatedly loads JNIEnv byte
offset `0x58` (slot 22, `DeleteGlobalRef`) to release stored global
references before additional native cleanup.

## Boundary

These binaries prove the unload entrypoint ABI and show that existing GetEnv
and DeleteGlobalRef coverage is directly useful during unload. They do not prove
when a compatibility runtime should automatically trigger JNI_OnUnload relative
to dlclose, class-loader collection, process exit, or general DSO teardown.
