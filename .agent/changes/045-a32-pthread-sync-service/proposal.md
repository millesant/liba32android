# Proposal — bounded pthread mutex/semaphore synchronization

Use feature 044's generic suspension boundary to implement the smallest
target-backed pthread/semaphore compatibility slice that actually needs
scheduling.

Cover default mutex init/destroy/lock/trylock/unlock and process-local
sem_init/destroy/wait/post. Keep guest synchronization object pointers opaque:
the service keys finite compatibility state by logical address and never exposes
or mirrors host pthread object layout.

A contended blocking operation records a finite waiter and suspends. Unlock/post
commits the grant before publishing a logical thread as ready, allowing the
embedding to resume the saved post-SVC A32 state without replaying the call.

Extend the existing partial libc fixture/provider with those nine functions.
Do not implement thread creation, TLS, condition/rwlock/once APIs, process-shared
semaphores, futex internals, or scheduling policy.
