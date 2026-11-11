#!/usr/bin/env python3
"""Checks docs/theory: every statement has a page section and a figure, every link and figure exists,
every referenced function / test token is still in the source, and every public function is listed.

Run: make theory-check   (standard library only)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
THEORY = ROOT / "docs/theory"
errors = []


def fail(message):
    errors.append(message)


def refs(cell):
    return re.findall(r"`([^`#\s]+)#([^`\n]+)`", cell)


index = (THEORY / "README.md").read_text(encoding="utf-8")
pages = {p.name: p.read_text(encoding="utf-8") for p in sorted(THEORY.glob("0*.md"))}
values = (THEORY / "figures/values.md").read_text(encoding="utf-8") if (THEORY / "figures/values.md").exists() else ""

# 1. coverage table rows
rows = {}
for line in index.splitlines():
    m = re.match(r"\| (S\d\d) \| (.+?) \| \[\d\]\(([^)]+)\) \| (.+?) \| (.+?) \| (.+?) \|$", line)
    if m:
        rows[m.group(1)] = dict(page=m.group(3), figure=m.group(4), code=m.group(5), test=m.group(6))
if not rows:
    fail("coverage table not found in docs/theory/README.md")

# 2. page sections: each "## Sxx" has a figure (image or mermaid) before the next section, and the listed parts in order
sections = {}
for name, text in pages.items():
    for m in re.finditer(r"^## (S\d\d) — .*$", text, flags=re.M):
        end = text.find("\n## ", m.end())
        sections[m.group(1)] = (name, text[m.end(): end if end > 0 else len(text)])
for sid in sorted(set(rows) | set(sections)):
    if sid not in rows:
        fail(f"{sid}: section exists but is missing from the coverage table")
        continue
    if sid not in sections:
        fail(f"{sid}: in the coverage table but has no section")
        continue
    name, body = sections[sid]
    row = rows[sid]
    if row["page"] != name:
        fail(f"{sid}: table says {row['page']}, section is in {name}")
    images = re.findall(r"!\[[^\]]*\]\(([^)]+)\)", body)
    has_mermaid = "```mermaid" in body
    if not images and not has_mermaid:
        fail(f"{sid}: no figure (neither an image nor a mermaid diagram)")
    for image in images:
        if not (THEORY / image).is_file():
            fail(f"{sid}: figure {image} does not exist (run make theory-figs)")
    listed = re.findall(r"`(figures/[^`]+)`", row["figure"])
    if "mermaid" in row["figure"] and not has_mermaid:
        fail(f"{sid}: table says mermaid, section has none")
    for figure in listed:
        if figure not in images:
            fail(f"{sid}: table lists {figure}, the section does not show it")
    positions = [body.find(f"**{part}") for part in ("Statement.", "Assumptions", "Derivation.", "Code.", "Tests.")]
    if min(positions) < 0 or positions != sorted(positions):
        fail(f"{sid}: needs Statement, Assumptions, Derivation, Code, Tests in this order")
    for cell in (row["code"], row["test"]):
        if not refs(cell):
            fail(f"{sid}: a code/test cell has no `file#token` reference")
        for file, token in refs(cell):
            path = ROOT / file
            if not path.is_file():
                fail(f"{sid}: {file} does not exist")
            elif file == "tests/theory_figs.c":
                if f'"{token}"' not in path.read_text(encoding="utf-8") or f"`{token}`" not in values:
                    fail(f"{sid}: measured value {token} is not produced by tests/theory_figs.c / values.md")
            elif token not in path.read_text(encoding="utf-8"):
                fail(f"{sid}: {token} is no longer in {file}")

# 3. measured-value ids quoted in the pages exist
for name, text in pages.items():
    for token in re.findall(r"\(`([a-z0-9.-]+)`\)|`([a-z]+-[a-z0-9.-]+)`", text):
        key = token[0] or token[1]
        if re.fullmatch(r"[a-z]+(-[a-z0-9.]+)+", key) and "/" not in key and f"`{key}`" not in values and not key.startswith("make"):
            if key in ("stateDiagram-v2",):
                continue
            fail(f"{name}: quotes value `{key}` that figures/values.md does not have")

# 4. links between Markdown files
for name, text in {**pages, "README.md": index}.items():
    for target in re.findall(r"\]\(([^)#]+)(?:#[^)]*)?\)", text):
        if "://" not in target and not (THEORY / target).exists():
            fail(f"{name}: broken link {target}")

# 5. every public function of src/*.h is in the coverage table or in the plumbing table
mentioned = {token.rstrip("(") for _, token in refs(index)}
for header in sorted((ROOT / "src").glob("*.h")):
    for fn in re.findall(r"^[A-Za-z_][\w \*]*?\b([a-z]+_[a-z0-9_]+)\(", header.read_text(encoding="utf-8"), flags=re.M):
        if fn not in mentioned:
            fail(f"{header.name}: public function {fn} is in neither table of docs/theory/README.md")

if errors:
    print("\n".join(errors))
    print(f"theory-check: {len(errors)} problem(s)")
    sys.exit(1)
print(f"theory-check: OK ({len(rows)} statements, each with a figure; {sum(1 for _ in THEORY.glob('figures/*.svg'))} generated figures; "
      f"{len(mentioned)} functions/tests/values referenced)")
