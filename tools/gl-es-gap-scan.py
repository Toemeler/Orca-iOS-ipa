#!/usr/bin/env python3
"""Find GL entry points Orca calls that OpenGL ES does not have.

Orca's GUI is written against <glad/gl.h>, the desktop loader, so calls into
functions that only exist in desktop GL compile fine and then resolve to a null
pointer on iOS, where the loader pulls names out of OpenGLES.framework. Calling
through one jumps to address zero.

This is how patch step3/0328's coverage was derived, and it is the thing to
re-run whenever a new crash lands in a GL frame or the upstream pin moves.

    python3 tools/gl-es-gap-scan.py /path/to/orca-checkout [--es 1|0]

--es selects the value of SLIC3R_OPENGL_ES the scan should assume (default 1,
which is what patch step3/0327 makes true). Sites inside a preprocessor branch
that the macro decides are reported as fenced off rather than live.

The authoritative list of what ES actually has is the entry-point set declared
by src/libvgcode/glad/include/glad/gles2.h, which ships in the Orca tree.
"""

import argparse
import collections
import os
import re
import sys


def glad_entry_points(header_path):
    """The gl* names a glad header maps onto a glad_gl* pointer."""
    text = open(header_path, encoding="utf-8", errors="replace").read()
    return set(re.findall(r"#define\s+(gl[A-Za-z0-9_]+)\s+glad_\1\b", text))


def branch_value(condition, es):
    """True/False when SLIC3R_OPENGL_ES alone decides the branch, else None."""
    cond = re.sub(r"/\*.*?\*/", "", condition)
    cond = re.sub(r"//.*$", "", cond).strip()
    if not re.search(r"\bSLIC3R_OPENGL_ES\b", cond):
        return None
    if re.fullmatch(r"SLIC3R_OPENGL_ES", cond):
        return bool(es)
    if re.fullmatch(r"!\s*SLIC3R_OPENGL_ES", cond):
        return not es
    if re.fullmatch(r"defined\s*\(?\s*SLIC3R_OPENGL_ES\s*\)?", cond):
        return bool(es)
    if re.fullmatch(r"!\s*defined\s*\(?\s*SLIC3R_OPENGL_ES\s*\)?", cond):
        return not es
    return None  # anything more involved: treat as undecided, i.e. live


def scan(orca_root, es):
    desktop = glad_entry_points(os.path.join(orca_root, "src/glad/include/glad/gl.h"))
    es_only = glad_entry_points(
        os.path.join(orca_root, "src/libvgcode/glad/include/glad/gles2.h")
    )
    absent_from_es = desktop - es_only

    live, fenced = [], []
    for root, _, files in os.walk(os.path.join(orca_root, "src/slic3r")):
        for name in sorted(files):
            if not name.endswith((".cpp", ".hpp", ".h", ".mm")):
                continue
            path = os.path.join(root, name)
            stack = []
            for lineno, line in enumerate(
                open(path, encoding="utf-8", errors="replace"), 1
            ):
                stripped = line.strip()
                directive = re.match(
                    r"#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", stripped
                )
                if directive:
                    keyword, rest = directive.group(1), directive.group(2)
                    if keyword == "if":
                        stack.append(branch_value(rest, es))
                    elif keyword == "elif":
                        if stack:
                            stack[-1] = branch_value(rest, es)
                    elif keyword == "ifdef":
                        stack.append(
                            bool(es) if "SLIC3R_OPENGL_ES" in rest else None
                        )
                    elif keyword == "ifndef":
                        stack.append(
                            (not es) if "SLIC3R_OPENGL_ES" in rest else None
                        )
                    elif keyword == "else":
                        if stack:
                            top = stack[-1]
                            stack[-1] = (not top) if isinstance(top, bool) else top
                    elif keyword == "endif":
                        if stack:
                            stack.pop()
                    continue

                dead = any(value is False for value in stack)
                for call in re.finditer(r"\b(gl[A-Z]\w+)\s*\(", line):
                    fn = call.group(1)
                    if fn in absent_from_es:
                        rel = os.path.relpath(path, orca_root)
                        (fenced if dead else live).append((rel, lineno, fn))
    return live, fenced


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("orca_root", help="path to an OrcaSlicer checkout")
    parser.add_argument("--es", type=int, default=1, choices=(0, 1))
    args = parser.parse_args()

    live, fenced = scan(args.orca_root, args.es)
    print(
        f"SLIC3R_OPENGL_ES={args.es}: {len(live)} live calls into entry points "
        f"OpenGL ES does not have ({len(fenced)} fenced off by the macro)\n"
    )
    by_fn = collections.Counter(fn for _, _, fn in live)
    for fn, count in by_fn.most_common():
        print(f"{fn}  ({count})")
        for path, lineno, name in live:
            if name == fn:
                print(f"    {path}:{lineno}")
    print("\nby file:")
    for path, count in collections.Counter(p for p, _, _ in live).most_common():
        print(f"  {count:4d}  {path}")
    return 1 if live else 0


if __name__ == "__main__":
    sys.exit(main())
