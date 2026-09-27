# Proposal — resident-object ARM32 libdl compatibility

Add the five libdl functions required by the supplied FMOD/VLC ARM32 targets
without coupling the compatibility service to Android pathname/APK search or
premature unload lifecycle.

Use the accepted persistent Elf32LinkMap as the resident object set. dlopen
creates/refcounts synthetic logical guest handles only for objects already in
that map; dlsym reuses bounded graph symbol lookup; dladdr maps guest addresses
back to resident module/dynamic-symbol metadata; dlerror uses caller-provided
guest scratch; dlclose releases only the synthetic handle.

Generate a freestanding ARM32 libdl.so SVC shim and prove it through a real
consumer plus separate resident provider DSO under the namespace-gated platform
catalog.
