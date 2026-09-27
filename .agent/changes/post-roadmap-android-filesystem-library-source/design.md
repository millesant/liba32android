# Design — bounded Android filesystem library source

## Input contract

`A32FilesystemLibrarySource` receives an exact path string and the generic
loader's `max_image_bytes` ceiling. A separate `max_path_bytes` option bounds
the source path itself.

Empty paths, embedded NUL, zero ceilings, or over-ceiling paths fail before I/O.

## I/O mapping

The source opens the exact path read-only with close-on-exec.

- ENOENT/ENOTDIR -> NotFound.
- Other open failures -> Failed.
- fstat failure, non-regular file, or empty file -> Failed.
- Observed size above max_image_bytes or host size_t -> Failed.
- Allocation failure -> Failed.
- EINTR while reading -> retry.
- Other read error or premature EOF -> Failed.

The source reads exactly the size observed by fstat. It performs no
canonicalization, package lookup, root containment inference, or archive
interpretation.

## Identity

Success publishes the exact candidate path as the opaque dependency identity
and owns the copied file bytes.

## Composition

The existing requester-aware search provider remains responsible for accepting
only bare SONAME requests and constructing `root/name`. This prevents the new
source from becoming a new path-search policy layer.

The real ARM32 app-search integration passes the generated child fixture's
directory as a requester root. The child is therefore available to the generic
ELF loader only through filesystem source acquisition.
