# Compatibility spec delta — ARM32 JNI modified-UTF-8 strings

The JNIEnv table publishes:

- NewStringUTF at slot 167 / byte offset 0x29c;
- GetStringUTFChars at slot 169 / byte offset 0x2a4;
- ReleaseStringUTFChars at slot 170 / byte offset 0x2a8.

Each uses one distinct private ARM service stub.

The bounded JNI registry owns finite logical jstring entries. NewStringUTF reads
one bounded NUL-terminated guest byte string, copies it into owned storage,
allocates a collision-free logical 32-bit handle from a caller-configurable
base/stride range, and retains one local reference. Exhaustion returns null.

The VM layout includes one caller-owned writable guest scratch region for UTF
chars. GetStringUTFChars requires a known live string and no outstanding lease,
copies the stored bytes plus NUL to that region, writes JNI_TRUE through a
non-null isCopy pointer, returns the logical scratch address, and records the
lease. ReleaseStringUTFChars succeeds only for the exact leased string/pointer
pair and clears that lease.

The slice preserves byte payloads as supplied and does not claim full
modified-UTF-8 validation, UTF-16 conversion, GetStringUTFLength, region APIs,
multiple simultaneous leases, or general Java String behavior.
