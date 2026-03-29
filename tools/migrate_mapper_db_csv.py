#!/usr/bin/env python3
"""One-shot / repeatable: convert mapper_db.csv to sha1,mapper,basic|none,e|j."""
from __future__ import annotations

import sys
from collections import OrderedDict
from typing import Optional, Tuple

HEX = set("0123456789abcdef")


def norm_sha(s: str) -> str:
    s = s.strip().lower()
    return s if len(s) == 40 and all(c in HEX for c in s) else ""


def migrate_cols(cols: list[str]) -> Optional[Tuple[str, str, str, str]]:
    sha = norm_sha(cols[0])
    if not sha:
        return None
    mapper = cols[1].strip() if len(cols) > 1 else "NONE"
    t2 = cols[2].strip().lower() if len(cols) > 2 else ""
    if len(cols) >= 4 and t2 in ("basic", "none"):
        fj = cols[3].strip().lower()[:1] if len(cols) > 3 else "e"
        if fj == "k":
            fj = "j"
        if fj not in ("e", "j"):
            fj = "e"
        b = "basic" if t2 == "basic" else "none"
        return (sha, mapper, b, fj)
    if len(cols) >= 3 and len(t2) == 1 and t2[0] in "012345":
        bm = int(t2[0])
        fr = cols[3].strip().lower()[:1] if len(cols) > 3 else "e"
        fj = "j" if fr in ("j", "k") else "e"
        if bm == 0:
            return (sha, mapper, "none", "e")
        if bm == 1:
            return (sha, mapper, "none", fj)
        if bm == 2:
            return (sha, mapper, "basic", "e")
        if bm == 3:
            return (sha, mapper, "none", "e")
        if bm == 4:
            return (sha, mapper, "basic", "j")
        if bm == 5:
            return (sha, mapper, "none", "j")
    return (sha, mapper, "none", "e")


def main() -> int:
    path = sys.argv[1] if len(sys.argv) > 1 else "mapper_db.csv"
    seen: OrderedDict[str, tuple[str, str, str, str]] = OrderedDict()
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("#"):
                continue
            line = line.strip()
            if not line:
                continue
            cols = [x.strip() for x in line.split(",")]
            if len(cols) < 2:
                continue
            row = migrate_cols(cols)
            if row:
                seen[row[0]] = row
    hdr = (
        "# sha1,mapper,basic_or_none,font  "
        "basic+e=VG8020 basic+j=HB-10 none+e=C-BIOS none+j=C-BIOS JP\n"
    )
    keys = sorted(seen.keys())
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(hdr)
        for k in keys:
            sha, mapper, b, fj = seen[k]
            f.write(f"{sha},{mapper},{b},{fj}\n")
    print(f"{path}: {len(keys)} unique rows", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
