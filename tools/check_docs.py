"""Check local Markdown link targets without requesting external URLs."""

import html
import re
import sys
import unicodedata
from pathlib import Path
from urllib.parse import unquote, urlsplit


def heading_anchors(markdown: str) -> set[str]:
    """Return GitHub-style anchors for Markdown headings, including duplicate suffixes."""
    anchors: set[str] = set()
    counts: dict[str, int] = {}
    in_fence = False
    fence_marker = ""
    for line in markdown.splitlines():
        fence = re.match(r"^\s{0,3}(```+|~~~+)", line)
        if fence:
            marker = fence.group(1)[0]
            if not in_fence:
                in_fence = True
                fence_marker = marker
            elif marker == fence_marker:
                in_fence = False
            continue
        if in_fence:
            continue
        heading = re.match(r"^\s{0,3}#{1,6}\s+(.+?)\s*#*\s*$", line)
        if not heading:
            continue
        text = heading.group(1)
        text = re.sub(r"!\[([^]]*)\]\([^)]*\)", r"\1", text)
        text = re.sub(r"\[([^]]+)\]\([^)]*\)", r"\1", text)
        text = re.sub(r"`([^`]*)`", r"\1", text)
        text = re.sub(r"<[^>]*>", "", text)
        text = unicodedata.normalize("NFC", html.unescape(text)).lower()
        slug = "".join(
            "-"
            if character.isspace()
            else character
            for character in text
            if character in "-_" or character.isspace() or unicodedata.category(character)[0] in "LN"
        ).strip("-")
        duplicate = counts.get(slug, 0)
        counts[slug] = duplicate + 1
        anchors.add(slug if duplicate == 0 else f"{slug}-{duplicate}")
    return anchors


def link_failure(source: Path, target: str) -> str | None:
    """Return a diagnostic when a local Markdown target or its heading anchor is missing."""
    url = urlsplit(target)
    if url.scheme or url.netloc:
        return None
    destination = source.parent / unquote(url.path) if url.path else source
    if not destination.exists():
        return f"{source}: missing {target}"
    if url.fragment and destination.is_file() and unquote(url.fragment) not in heading_anchors(
        destination.read_text(encoding="utf-8")
    ):
        return f"{source}: missing fragment {target}"
    return None


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    failures: list[str] = []
    count = 0
    files = [root / "README.md", root / "CONTRIBUTING.md", root / "SECURITY.md"]
    files.extend((root / "docs").rglob("*.md"))
    files.extend((root / "host").rglob("README.md"))
    for file in files:
        for target in re.findall(r"\[[^\]]*\]\(([^)\s]+)\)", file.read_text()):
            url = urlsplit(target)
            failure = link_failure(file, target)
            if failure:
                failures.append(failure.removeprefix(f"{root}/") if failure.startswith(f"{root}/") else failure)
            elif not url.scheme and not url.netloc and (url.path or url.fragment):
                count += 1
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"PASS: {count} local documentation link targets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
