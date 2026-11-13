#!/usr/bin/env python3
"""
Name clash audit (project rule: no name clashes with Zephyr).

Collects every public name of the application (functions, extern variables, types, enum
constants and macros declared in src/*.h and the non-static globals of src/*.c) and looks for
each of them as a whole word in the Zephyr tree and the STM32 HAL headers.

Usage (workspace with the venv activated, or give the paths):
    python tools/check_names.py                       # uses %ZEPHYR_BASE%
    python tools/check_names.py C:\\path\\to\\zephyr C:\\path\\to\\modules\\hal\\stm32

Output: one line per name that was found elsewhere (file:line); no output = no hit.
Names that only appear in the application itself are not reported.
"""
import os
import re
import sys

APP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SKIP_NAMES = {"main", "NULL", "BIT", "ARRAY_SIZE", "MIN", "MAX", "CLAMP", "ARG_UNUSED"}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def app_names():
    names = {}

    def add(n, where):
        if n not in SKIP_NAMES and len(n) > 2:
            names.setdefault(n, where)

    for fn in sorted(os.listdir(os.path.join(APP, "src"))):
        path = os.path.join(APP, "src", fn)
        if not fn.endswith((".h", ".c")):
            continue
        text = strip_comments(open(path, encoding="utf-8", errors="replace").read())
        where = "src/" + fn
        if fn.endswith(".h"):
            for m in re.finditer(r"#define\s+([A-Za-z_]\w*)", text):
                add(m.group(1), where)
            for m in re.finditer(r"\b(?:struct|enum|union)\s+([A-Za-z_]\w*)\s*\{", text):
                add(m.group(1), where)
            for m in re.finditer(r"\benum\b[^{;]*\{([^}]*)\}", text):
                for c in re.finditer(r"\b([A-Za-z_]\w*)\s*(?:=[^,}]*)?(?:,|$)", m.group(1)):
                    add(c.group(1), where)
            # function declarations / extern variables
            for m in re.finditer(r"^[\w\s\*]+?\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*;", text, flags=re.M):
                add(m.group(1), where)
            for m in re.finditer(r"^extern\s[^;(]*?\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*;", text, flags=re.M):
                add(m.group(1), where)
            for m in re.finditer(r"static\s+inline\s+[\w\s\*]+?\b([A-Za-z_]\w*)\s*\(", text):
                add(m.group(1), where)
        else:
            # non-static definitions at file scope (functions and variables)
            for m in re.finditer(r"^(?!static|typedef|#|\s)[\w\s\*]+?\b([A-Za-z_]\w*)\s*(?:\(|=|;|\[)", text, flags=re.M):
                add(m.group(1), where)
            for m in re.finditer(r"^(?:RING_BUF_DECLARE|K_SEM_DEFINE|K_MSGQ_DEFINE|K_MUTEX_DEFINE)\s*\(\s*(\w+)", text, flags=re.M):
                add(m.group(1), where)
    return names


def main():
    roots = sys.argv[1:] or [os.environ.get("ZEPHYR_BASE", "")]
    roots = [r for r in roots if r and os.path.isdir(r)]
    if not roots:
        sys.exit("give the Zephyr tree (and the stm32 HAL module) or set ZEPHYR_BASE")
    names = app_names()
    pattern = re.compile(r"\b(" + "|".join(map(re.escape, sorted(names))) + r")\b")
    subdirs = ("include", "drivers", "subsys", "lib", "kernel", "soc", "arch", "dts", "cmsis", "stm32cube", "drivers/include")
    hits = 0
    for root in roots:
        walk_roots = [os.path.join(root, d) for d in subdirs if os.path.isdir(os.path.join(root, d))] or [root]
        for wr in walk_roots:
            for dp, dn, fns in os.walk(wr):
                for fn in fns:
                    if not fn.endswith((".h", ".c", ".dtsi", ".yaml")):
                        continue
                    p = os.path.join(dp, fn)
                    try:
                        lines = open(p, encoding="utf-8", errors="replace").read().splitlines()
                    except OSError:
                        continue
                    for i, line in enumerate(lines, 1):
                        for m in pattern.finditer(line):
                            if fn.endswith(".h") or m.group(1) in line:
                                print(f"{m.group(1):32s} (app: {names[m.group(1)]})  {p}:{i}: {line.strip()[:90]}")
                                hits += 1
    print(f"{len(names)} application names checked, {hits} hits", file=sys.stderr)


if __name__ == "__main__":
    main()
