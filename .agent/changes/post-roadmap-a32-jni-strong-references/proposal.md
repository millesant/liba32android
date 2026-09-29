# Proposal — ARM32 JNI strong/local references

## Intent

Add the smallest JNI reference-lifetime surface directly demonstrated by the
supplied ARMv7 VLC library without inventing a general Java object runtime.

## Scope

Publish NewGlobalRef, DeleteGlobalRef, and DeleteLocalRef at their observed
JNIEnv slots. Track bounded local/global reference counts for logical guest
object handles. FindClass establishes a local reference for the returned class
handle so the reference APIs have a real producer.

This slice intentionally leaves NewWeakGlobalRef, DeleteWeakGlobalRef,
NewLocalRef, IsSameObject, general object allocation, GC, and cross-thread local
reference semantics outside the boundary.
