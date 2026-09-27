# Proposal — bounded resident dlclose lifecycle transaction

Replace final synthetic-handle release with an optional exact-object teardown
transaction built on the accepted persistent lifecycle and service-aware FINI
seams.

Keep dependency ownership and mapping reclamation separate. The transaction
must never unmap an object merely because one resident synthetic handle reaches
zero.
