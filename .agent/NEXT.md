# NEXT

## JNI evidence discovery

Continue from validated explicit JNI_OnUnload revision
`c599cd09b19847b7366b0ee21cd839ed569276bd`.

The supplied VLC ARMv7 artifacts now have bounded coverage for their observed
JNI_OnLoad/JNI_OnUnload lifecycle hooks and the directly evidenced JNIEnv /
JavaVM seams implemented so far.

Do not wire JNI_OnUnload directly to generic `dlclose` merely because both are
unload-shaped. JNI defines JNI_OnUnload as a VM/class-loader native-library
lifecycle hook; ordinary OS/ELF handle release is not by itself the same event.
Any automatic invocation needs an explicit Java-library ownership trigger and
ordering contract.

For the next selected seam:

- require direct supplied-binary or accepted lifecycle evidence;
- record exact ABI/slot/callsite or lifecycle trigger evidence;
- define the smallest logical ownership/state contract;
- add focused regressions before broadening implementation;
- keep unrelated Java/framework behavior outside scope.

Local frames, NewLocalRef-from-jweak, IsSameObject, GC/weak clearing, and
automatic class-loader unload remain unselected until evidence proves a bounded
need.

## Validation

After implementation, use exact-head commit checks once. Escalate only a
failing or ambiguous check and leave CI immediately on terminal success.
