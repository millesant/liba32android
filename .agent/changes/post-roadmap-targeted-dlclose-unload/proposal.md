# Proposal — targeted final-close unload

Add a higher ownership transaction for the final synthetic libdl handle
reference.

Before teardown, compute the exact object set that would become unreachable
after removing the corresponding persistent root while preserving every other
persistent root and live handle. Finalize only that set in deterministic
requester-before-dependency order.

Reuse the accepted exact-object Android teardown ordering for each selected
object: FINI_ARRAY reverse order, exact registered-finalization completion, then
DT_FINI. Preserve handle/root ownership on teardown or reclamation failure.

After all selected lifecycle state is Complete, release the exact persistent
root through the accepted physical reclamation transaction and only then clear
the final synthetic handle.

Automatic discovery of each object's opaque DSO handle remains outside this
slice; the embedding continues to supply stable object-to-DSO bindings.
