# Compatibility spec delta — feature 045

Add private SVC IDs 0xB3-0xBB for default
pthread_mutex_init/destroy/lock/trylock/unlock and process-local
sem_init/destroy/wait/post.

The service borrows finite mutex/semaphore/waiter metadata, keys state only by
logical guest object address, and uses a caller-selected non-zero logical thread
ID. Contended lock and zero-count wait return Suspended after enqueueing one
finite waiter and preparing r0=0. Unlock/post grants the oldest waiter before
publishing that thread ready, so a feature-044 continuation resumes after the
original SVC.

The partial libc fixture expands from thirty to thirty-nine exports/imports and
executes all thirty-nine wrappers with eager JUMP_SLOT relocation evidence.

Broader pthread/thread/TLS, mutex attr/type, condition/rwlock/once,
process-shared semaphore, futex, cancellation, robust-mutex, and scheduler
semantics remain deferred.
