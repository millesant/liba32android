# Proposal — bounded Android filesystem library source

Feature 048 established requester-scoped Android application-library search but
kept concrete byte acquisition behind `A32AndroidLibrarySource`.

Close one production-readiness gap by adding a regular-file source that can
consume caller-supplied extracted/native-library roots. Keep path selection in
the existing search provider and keep APK/ZIP/package-manager policy out of the
generic core.

Prove the concrete path end-to-end by making the existing real ARM32 app-search
fixture acquire its child DSO through this source rather than a synthetic
in-memory source.
