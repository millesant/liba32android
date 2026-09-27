# Proposal — A32 service suspension boundary

Add one game-agnostic scheduler handoff to the existing bounded A32 host-service
dispatcher.

Future pthread/semaphore compatibility cannot safely emulate contended waits by
blocking the host executor or spinning inside a synchronous service. A service
therefore needs to be able to stop the current guest execution after its SVC,
return the exact resumable machine state to the embedding, and continue later
under a new finite budget.

This change adds that boundary only. It does not implement pthread object
semantics, futexes, thread creation, TLS, wake queues, or scheduling policy.
