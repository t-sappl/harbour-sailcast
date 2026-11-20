#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""(347) GitHub release text from CHANGELOG.md - one source for the
changelog, the GitHub release and the app ("What's new" of an update).

    python3 tools/release_notes.py 1.5          # writes release-notes-1.5.md

Takes the section "## 1.5 (...)" of CHANGELOG.md, adds the install
instructions and the credit line. The app leaves the "Install" section and
the credit line out when it shows the text (core/releasenotes.cpp).
(365) Appends the German section of CHANGELOG.de.md as a folded
<details><summary>Deutsch</summary> block - German phones show that one."""
import re
import sys
from pathlib import Path

INSTALL = """### Install
Sailcast tells you about new versions itself (About page, from 1.5 on). By hand: allow untrusted software once (Settings → Untrusted software), download the RPM for your device and tap it in Downloads – `aarch64` for 64-bit devices (Jolla Phone (2026), Jolla C2), `armv7hl` for 32-bit installations (older Xperias), `i486` for the Jolla Tablet. Updating keeps your podcasts, settings and listening state."""

CREDIT = "Developed by Thomas Sappl together with Claude (Anthropic) – analysis, design, code, reviews and tests."


def section(changelog, version):
    lines = changelog.splitlines()
    head = re.compile(r"^##\s+v?" + re.escape(version) + r"(\s|\(|$)")
    start = next((i for i, l in enumerate(lines) if head.match(l)), None)
    if start is None:
        return None
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("## ")), len(lines))
    return "\n".join(lines[start + 1:end]).strip()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    version = sys.argv[1].lstrip("v")
    # "1.5.0" finds "## 1.5" too (the app compares versions the same way)
    if version.endswith(".0") and version.count(".") == 2:
        short = version[:-2]
    else:
        short = None
    root = Path(__file__).resolve().parent.parent
    changelog = (root / "CHANGELOG.md").read_text(encoding="utf-8")
    text = section(changelog, version)
    if text is None and short:
        text = section(changelog, short)
    if text is None:
        sys.exit("CHANGELOG.md has no section '## %s'" % version)
    if not text:
        sys.exit("The section '## %s' in CHANGELOG.md is empty" % version)
    # (365) The German notes in a folded block: the app shows them on German
    # phones as "What's new" of an update (core/releasenotes.cpp localized())
    german = ""
    de_file = root / "CHANGELOG.de.md"
    if de_file.exists():
        de_text = section(de_file.read_text(encoding="utf-8"), version)
        if de_text is None and short:
            de_text = section(de_file.read_text(encoding="utf-8"), short)
        if de_text:
            german = "\n\n<details>\n<summary>Deutsch</summary>\n\n" + de_text + "\n\n</details>\n"
        else:
            print("note: CHANGELOG.de.md has no section '## %s' - no German block" % version)
    out = root / ("release-notes-%s.md" % version)
    out.write_text(text + "\n\n" + INSTALL + "\n\n---\n\n" + CREDIT + "\n" + german, encoding="utf-8")
    print("written", out.name)


if __name__ == "__main__":
    main()
