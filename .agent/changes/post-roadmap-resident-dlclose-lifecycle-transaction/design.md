# Design — resident dlclose lifecycle transaction

A caller-owned finite binding associates each teardown-capable object index with
one exact guest DSO-handle word.

Non-final references decrement only. On the last reference, the target object
must have Complete constructors and Pending destructors.

The transaction decodes only the target FINI_ARRAY, executes it in reverse
through bounded service-aware lifecycle execution, requires matching registered
destructors to be Complete, then executes DT_FINI. Full success latches
destructors Complete and clears the handle.

Failures before guest teardown preserve state and ownership. Failures after
guest teardown begins latch Failed and preserve the handle so partial side
effects cannot be silently replayed.

A32LibDlService optionally borrows the transaction and delegates dlclose to it.
No recursive dependency teardown, object removal, or unmapping occurs.
