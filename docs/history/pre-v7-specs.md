# Retired pre-v7 specifications

The repository previously carried a root `specs/` tree containing the
pre-v7 feature-era packages `000-current-baseline` through
`014-elf32-symbol-versioning`.

Those packages were workflow snapshots, not current contract authority. By
2026-09-28 the tree contained 45 Markdown files (about 274 KiB), including
historical status lines that could read as active or unverified even when later
work had already superseded them. Keeping that duplicate specification surface
in the working tree created more ambiguity than value.

The last integration revision containing the complete root `specs/` tree is:

```text
56a438e426408465ec23bac6c09960a792be090b
```

Nothing was erased from Git history. To inspect the retired tree:

```sh
git ls-tree -r 56a438e426408465ec23bac6c09960a792be090b specs/
git show 56a438e426408465ec23bac6c09960a792be090b:specs/010-elf32-gnu-relro/spec.md
```

Current accepted project contracts live in `docs/contracts/`. Active substantial
work, status, acceptance criteria, validation requirements, and handoffs live in
GitHub Issues. Historical workflow snapshots remain recoverable from Git history
and are not a current authority surface.

Architecture and research evidence that remain relevant to the implemented
system live under `docs/architecture/` and `docs/research/`.
