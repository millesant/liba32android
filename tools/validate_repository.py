#!/usr/bin/env python3
"""Validate the public repository/documentation surface without extra deps."""

from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]

REQUIRED = (
    "README.md",
    "README.pt-BR.md",
    "LICENSE",
    "CONTRIBUTING.md",
    "SECURITY.md",
    "CODE_OF_CONDUCT.md",
    "ROADMAP.md",
    "AGENTS.md",
    "docs/README.md",
    "docs/README.pt-BR.md",
    "docs/contracts/runtime.md",
    "docs/contracts/elf32.md",
    "docs/contracts/compat.md",
    "docs/architecture/decisions.md",
    "docs/development/build-and-test.md",
    "docs/development/public-api-quickstart.md",
    "docs/history/pre-v7-specs.md",
    ".github/ISSUE_TEMPLATE/bug_report.md",
    ".github/ISSUE_TEMPLATE/feature_request.md",
    ".github/pull_request_template.md",
    ".github/CODEOWNERS",
)

MARKDOWN_ROOT_FILES = (
    ROOT / "README.md",
    ROOT / "README.pt-BR.md",
    ROOT / "CONTRIBUTING.md",
    ROOT / "SECURITY.md",
    ROOT / "CODE_OF_CONDUCT.md",
    ROOT / "ROADMAP.md",
)

LINK_RE = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")


def markdown_files() -> list[Path]:
    files = list(MARKDOWN_ROOT_FILES)
    files.extend(sorted((ROOT / "docs").rglob("*.md")))
    return files


def normalize_target(source: Path, raw: str) -> Path | None:
    target = raw.strip()
    if not target or target.startswith(("#", "http://", "https://", "mailto:")):
        return None
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1]
    target = unquote(target.split("#", 1)[0].split("?", 1)[0])
    if not target:
        return None
    return (source.parent / target).resolve()


def main() -> int:
    errors: list[str] = []

    for rel in REQUIRED:
        if not (ROOT / rel).is_file():
            errors.append(f"missing required repository file: {rel}")

    if (ROOT / "specs").exists():
        errors.append(
            "legacy root specs/ should remain retired; see docs/history/pre-v7-specs.md"
        )

    if (ROOT / ".agent").exists():
        errors.append(
            "retired repository-local .agent control/state tree must not be present"
        )

    root_resolved = ROOT.resolve()
    for source in markdown_files():
        text = source.read_text(encoding="utf-8")
        if "\\n-" in text:
            errors.append(
                f"{source.relative_to(ROOT)} contains a literal escaped newline marker"
            )
        for match in LINK_RE.finditer(text):
            target = normalize_target(source, match.group(1))
            if target is None:
                continue
            try:
                target.relative_to(root_resolved)
            except ValueError:
                errors.append(
                    f"{source.relative_to(ROOT)} link escapes repository: {match.group(1)}"
                )
                continue
            if not target.exists():
                errors.append(
                    f"{source.relative_to(ROOT)} has missing link target: {match.group(1)}"
                )

    if errors:
        for error in errors:
            print(f"repo-hygiene: ERROR: {error}", file=sys.stderr)
        return 1

    print(
        "repo-hygiene: PASS "
        f"required={len(REQUIRED)} markdown_files={len(markdown_files())}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
