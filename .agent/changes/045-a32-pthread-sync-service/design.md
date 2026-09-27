# Design — bounded pthread mutex/semaphore synchronization

## Identity and ownership

The service borrows three finite spans: mutex records, semaphore records, and
waiter records. A guest object address is only a logical identity key. Address
zero is invalid and no bionic private object layout is read or synthesized.

The embedding sets one non-zero logical current-thread ID before executing each
guest thread.

## Mutexes

Explicit init accepts a null attr only. lock/trylock on an unknown non-zero
address lazily creates the default mutex, covering static-initializer usage.

Free lock ownership is assigned synchronously. trylock on an owned mutex returns
EBUSY=16. Blocking lock on an owned mutex enqueues the current logical thread,
sets r0=0 for its eventual successful return, and returns Suspended.

Owner unlock grants the oldest waiter by assigning mutex ownership to that
thread before marking the waiter ready. If no waiter exists the mutex becomes
unowned. Destroy of an owned/waited mutex returns EBUSY.

## Semaphores

sem_init supports only pshared=0 and counts through 0x7fffffff. sem_wait consumes
a positive count or enqueues/suspends at zero. sem_post grants the oldest waiter
without incrementing the count, otherwise increments. sem_destroy requires no
waiters.

## Wake contract

Ready records carry wait kind, object address, and logical thread ID. The
embedding drains them, locates the matching feature-044 suspended execution
snapshot, and resumes that snapshot with a new finite instruction budget. The
blocking SVC is not replayed.

## Real partial-libc path

The existing libc shim adds nine direct SVC stubs and the freestanding consumer
adds nine wrappers. The dedicated integration resolves thirty-nine symbols,
requires thirty-nine eager JUMP_SLOT targets, and executes all thirty-nine
wrappers through the namespace-gated finite platform catalog.
