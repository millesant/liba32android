# Compatibility spec delta — ARM32 JNI object arrays

The JNIEnv table publishes:

- NewObjectArray at slot 172 / byte offset `0x2b0`;
- GetObjectArrayElement at slot 173 / `0x2b4`;
- SetObjectArrayElement at slot 174 / `0x2b8`.

Each targets one distinct private ARM service stub.

NewObjectArray creates a bounded synthetic logical array handle, validates a
registered element-class handle and signed nonnegative length, initializes all
entries to the supplied null/live logical jobject handle, adds generic
array-length metadata, and retains one local reference for the array.

GetObjectArrayElement validates signed bounds. A null stored entry returns null.
A non-null stored logical identity receives one local reference before its same
opaque handle is returned.

SetObjectArrayElement validates signed bounds and stores null or a currently
live logical jobject identity. Array element storage itself does not alter JNI
local/global reference counts.

Java class assignability, ArrayStoreException, inheritance, object
construction/method invocation, local frames, and full GC remain outside this
slice.
