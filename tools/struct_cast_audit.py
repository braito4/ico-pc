#!/usr/bin/env python3
"""tools/struct_cast_audit.py [--selftest] [--allow FILE] [PATH...]

Looks in the game sources (ico2/ by default) for a fixed buffer cast to a
struct that is larger on the host than the buffer.  The decompiled code sized
a buffer from the EE record it holds (a static `float work[12][4]` for a
0xC0-byte ClipWork); the host records are wider wherever they hold pointers
(8 bytes, not 4: WallCfg, ClipWork, ...), so the cast struct reaches past the
buffer and every write through it lands on the next object in memory.  Issue
71 found one: avoidInsideOfWall cast the 192-byte itemWork to the 240-byte
ClipWork and wrote over itemDropOfs and lodManager's tables.

A hit is a cast `(T *)NAME`, `(T *)&NAME` (also through a (char *) or (void *)
in between) where NAME is a fixed array, or a non-pointer variable of another
type under `&`, declared in the same file (file scope or inside the function),
and sizeof(T) is larger than NAME's size on the host.  The sizes come from the
host compiler: the file is compiled with probe objects appended (gcc -S, the
game's include dirs and defines), so no layout is guessed.  A cast of `&NAME[i]`
or of a member is not looked at.

A checked false positive goes into the allow file (tools/struct_cast_allow.txt),
one per line:

    path:NAME:T   reason

Prints one line per hit (path:line: NAME (N bytes) cast to T (M bytes)) and
exits 1 when any hit is not allowed, or when an allow entry matches nothing.
--selftest runs the check on tools/test/struct_cast/ and compares the hits
with the fixture's EXPECT markers.
"""

import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack_overread_audit import functions, strip  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DEFAULT_ALLOW = os.path.join(ROOT, "tools", "struct_cast_allow.txt")
FIXTURES = os.path.join(ROOT, "tools", "test", "struct_cast")

PLAIN = {"char", "short", "int", "long", "float", "double", "void", "unsigned", "signed", "u_char",
         "u_short", "u_int", "u_long", "u_long128", "uchar", "ushort", "uint", "ulong", "ICO_WORD",
         "size_t", "sceVu0FVECTOR", "sceVu0IVECTOR", "sceVu0FMATRIX"}
KEYWORDS = {"return", "else", "case", "goto", "sizeof", "typedef", "extern", "static", "const",
            "volatile", "register", "struct", "union", "enum", "if", "while", "for", "do", "switch"}

CAST = re.compile(
    r"\(\s*(?:const\s+)?((?:struct\s+|union\s+)?[A-Za-z_]\w*)\s*\*\s*\)\s*"
    r"(?:\(\s*(?:unsigned\s+)?(?:char|void|u_char|int|float)\s*\*\s*\)\s*)?(&\s*)?([A-Za-z_]\w*)\b"
    r"(?!\s*[\[(.]|\s*->)")
DECL = re.compile(
    r"(?:(?<=[;{}])|^)\s*((?:(?:static|const|volatile|register|unsigned|signed|struct|union|long|short)\s+)*)"
    r"([A-Za-z_]\w*)\s+((?:[A-Za-z_]\w*\s*(?:\[[^\]]*\]\s*)*(?:=[^;{}]*)?,\s*)*"
    r"[A-Za-z_]\w*\s*(?:\[[^\]]*\]\s*)*)(?==|;)", re.M)
PTRDECL = re.compile(r"\b(?:struct\s+|union\s+)?[A-Za-z_]\w*\s*\*+\s*(?:const\s+)?([A-Za-z_]\w*)\s*(?=[=;,)\[])")
ONE = re.compile(r"([A-Za-z_]\w*)\s*((?:\[[^\]]*\]\s*)*)$")
FLAGS = ["-DICO_HOST=1", "-DICO_HEADLESS=1", "-std=gnu11", "-fsigned-char", "-fgnu89-inline",
         "-fno-strict-aliasing", "-fno-common", "-w", "-Wno-error=implicit-function-declaration",
         "-Wno-error=implicit-int", "-Wno-error=int-conversion", "-Wno-error=incompatible-pointer-types",
         "-Wno-error=return-mismatch", "-Wno-error=declaration-missing-parameter-type", "-O0", "-S", "-o", "-"]


def include_flags():
    out = []
    for d in sorted(os.listdir(os.path.join(ROOT, "ico2"))):
        p = os.path.join(ROOT, "ico2", d, "include")
        if os.path.isdir(p):
            out.append("-I" + p)
    return out + ["-I" + os.path.join(ROOT, "port", "compat"), "-I" + os.path.join(ROOT, "port", "include"),
                "-I" + os.path.join(ROOT, "port", "ui")]


def compile_probe(path, name):
    """Compile the probe file with $ICO_CC, else $CC, else gcc.  A failure to start the compiler
    or to compile is shown (the first lines of stderr, once per cause) so the cause is visible."""
    cc = os.environ.get("ICO_CC") or os.environ.get("CC") or "gcc"
    cmd = [cc] + include_flags() + ["-I" + os.path.dirname(path)] + FLAGS + [name]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True)
    except OSError as e:
        r = subprocess.CompletedProcess(cmd, 127, "", "cannot run %s: %s" % (cc, e))
    if r.returncode != 0:
        LAST_ERR[0] = "%s failed on %s (exit %d):\n%s" % (
            cc, os.path.relpath(path, ROOT), r.returncode, "\n".join(r.stderr.splitlines()[:8]))
    return r


LAST_ERR = [""]


def read(path):
    with open(path, "rb") as f:
        return f.read().decode("latin-1")


def literal_dims(dims):
    n = 1
    for d in re.findall(r"\[([^\]]*)\]", dims):
        d = d.strip()
        if not re.fullmatch(r"(0[xX][0-9a-fA-F]+|\d+)", d):
            return None
        n *= int(d, 0)
    return n


def declarations(code, spans):
    """name -> list of (pos, type, dims string, file scope, has initializer) in code."""
    out = {}
    for m in DECL.finditer(code):
        words = m.group(1).split()
        typ = m.group(2)
        if typ in KEYWORDS or "typedef" in code[max(0, m.start() - 8):m.start() + 8]:
            continue
        if "struct" in words or "union" in words:
            pass
        pos = m.start(3)
        scope = not any(a <= pos < b for a, b in spans)
        for d in re.split(r",(?![^\[]*\])", m.group(3)):
            init = "=" in d
            d = d.split("=", 1)[0].strip()
            one = ONE.match(d)
            if one:
                out.setdefault(one.group(1), []).append((pos, typ, one.group(2).strip(), scope, init))
    return out


def probe_at(path, text, line, expr):
    """sizeof(expr) as seen at a line of the file: a type declared inside a function is only in
    scope there, so the probe object is declared on the line itself."""
    lines = text.split("\n")
    lines[line - 1] = "static char __ico_probe_0[sizeof(%s)]; " % expr + lines[line - 1]
    with tempfile.NamedTemporaryFile("wb", suffix=".c", delete=False) as t:
        t.write("\n".join(lines).encode("latin-1"))
        name = t.name
    try:
        r = compile_probe(path, name)
    finally:
        os.unlink(name)
    m = re.search(r"\.size\s+__ico_probe_0(?:\.\d+)?,\s*(\d+)", r.stdout) if r.returncode == 0 else None
    return int(m.group(1)) if m else None


def pointer_names(code, funcs):
    """name -> positions where a pointer of that name is declared or is a parameter."""
    out = {}
    for m in PTRDECL.finditer(code):
        out.setdefault(m.group(1), []).append(m.start(1))
    for _, b, _ in funcs:
        head = code[max(0, b - 800):b]
        head = head[max(head.rfind(";"), head.rfind("}")) + 1:]
        i = head.find("(")
        j = head.rfind(")")
        for part in head[i + 1:j].split(","):
            n = re.search(r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*$", part.strip())
            if n:
                out.setdefault(n.group(1), []).append(b)
    return out


def probe(path, text, exprs):
    """expr -> host size of sizeof(expr), by compiling text plus one probe object each."""
    sizes = {}
    exprs = sorted(exprs)
    if not exprs:
        return sizes

    def run(es):
        src = text + "\n" + "".join("char __ico_probe_%d[sizeof(%s)];\n" % (i, e) for i, e in enumerate(es))
        with tempfile.NamedTemporaryFile("wb", suffix=".c", delete=False) as t:
            t.write(src.encode("latin-1"))
            name = t.name
        try:
            r = compile_probe(path, name)
        finally:
            os.unlink(name)
        return r

    r = run(exprs)
    if r.returncode != 0:
        # some expression is not a sizeable type: probe them one at a time
        for e in exprs:
            r1 = run([e])
            if r1.returncode == 0:
                m = re.search(r"\.size\s+__ico_probe_0,\s*(\d+)", r1.stdout)
                if m:
                    sizes[e] = int(m.group(1))
        return sizes
    for i, e in enumerate(exprs):
        m = re.search(r"\.size\s+__ico_probe_%d,\s*(\d+)" % i, r.stdout)
        if m:
            sizes[e] = int(m.group(1))
    return sizes


def audit_file(path, rel):
    text = read(path)
    code = strip(text)
    funcs = list(functions(code))
    spans = [(b, e) for _, b, e in funcs]
    decls = declarations(code, spans)
    ptrs = pointer_names(code, funcs)
    cands = []
    for m in CAST.finditer(code):
        t, amp, name = " ".join(m.group(1).split()), m.group(2), m.group(3)
        if t in PLAIN or t in KEYWORDS or t.endswith((" char", " int", " void")) or name not in decls:
            continue
        pos = m.start()
        infunc = next(((a, b) for a, b in spans if a <= pos < b), None)
        vis = [d for d in decls[name] if (d[3] or (infunc and infunc[0] <= d[0] < infunc[1])) and d[0] < pos]
        if not vis:
            continue
        d = max(vis, key=lambda x: x[0])
        if any(d[0] < q < pos and (infunc and infunc[0] - 800 <= q < infunc[1]) for q in ptrs.get(name, [])):
            continue
        typ, dims = d[1], d[2]
        if typ in KEYWORDS or typ == t:
            continue
        if not dims and not amp:
            continue
        cands.append((code.count("\n", 0, pos) + 1, t, name, typ, dims, d[3], d[4]))
    need = set()
    for _, t, name, typ, dims, scope, init in cands:
        need.add(t)
        if dims and (scope or literal_dims(dims) is None):
            need.add(name)
        elif not dims:
            need.add(typ)
        else:
            need.add(typ)
    sizes = probe(path, text, need) if cands else {}
    hits = []
    unsized = []
    for line, t, name, typ, dims, scope, init in cands:
        local = {}
        if t not in sizes or (typ not in sizes and not (dims and (scope or literal_dims(dims) is None))):
            for e in (t, typ):
                if e not in sizes and e not in local:
                    v = probe_at(path, text, line, e)
                    if v is not None:
                        local[e] = v
        if local:
            sizes = dict(sizes)
            sizes.update(local)
        if t not in sizes:
            unsized.append((rel, line, name, t))
            continue
        if dims and (scope or literal_dims(dims) is None):
            have = sizes.get(name)
        elif dims:
            have = sizes.get(typ, 0) * literal_dims(dims) if typ in sizes else None
        else:
            have = sizes.get(typ)
        if have is None or sizes[t] <= have:
            continue
        hits.append((rel, line, name, have, t, sizes[t]))
    return hits, unsized


def source_files(paths):
    for p in paths:
        p = os.path.join(ROOT, p) if not os.path.isabs(p) else p
        if os.path.isfile(p):
            yield p
            continue
        for d, _, fs in os.walk(p):
            for f in sorted(fs):
                if f.endswith(".c"):
                    yield os.path.join(d, f)


def read_allow(path):
    out = {}
    if os.path.exists(path):
        for ln in read(path).splitlines():
            if ln.strip() and not ln.startswith("#"):
                key, _, why = ln.partition(" ")
                out[key] = why.strip()
    return out


def run(paths, allow_path):
    allow = read_allow(allow_path)
    used, bad, unsized_all = set(), [], []
    for f in sorted(source_files(paths)):
        rel = os.path.relpath(f, ROOT).replace(os.sep, "/")
        hits, unsized = audit_file(f, rel)
        if unsized and LAST_ERR[0]:
            print("struct_cast_audit: " + LAST_ERR[0], file=sys.stderr)
        for u in unsized:
            print("%s:%d: %s cast to %s: the host size of %s cannot be probed" % (u[0], u[1], u[2], u[3], u[3]))
            unsized_all.append(u)
        for h in hits:
            key = "%s:%s:%s" % (h[0], h[2], h[4])
            if key in allow:
                used.add(key)
                continue
            bad.append(h)
    for h in bad:
        print("%s:%d: %s (%d bytes) cast to %s (%d bytes on the host)" % h)
    return bad, unsized_all, [k for k in allow if k not in used]


def selftest():
    bad, _, _ = run([FIXTURES], os.devnull)
    got = sorted((h[1], h[2], h[4]) for h in bad)
    want = []
    for f in source_files([FIXTURES]):
        for i, ln in enumerate(read(f).splitlines(), 1):
            m = re.search(r"EXPECT (\w+) (\w+)", ln)
            if m:
                want.append((i, m.group(1), m.group(2)))
    want.sort()
    if got != want:
        print("selftest: got %s, wanted %s" % (got, want), file=sys.stderr)
        return 1
    print("struct_cast_audit selftest ok (%d hits)" % len(got))
    return 0


def main():
    args = sys.argv[1:]
    if "--selftest" in args:
        return selftest()
    allow = DEFAULT_ALLOW
    if "--allow" in args:
        i = args.index("--allow")
        allow = args[i + 1]
        del args[i:i + 2]
    bad, unsized, stale = run(args or ["ico2"], allow)
    for k in stale:
        print("%s: stale allow entry (matches nothing)" % k)
    return 1 if bad or unsized or (stale and not args) else 0


if __name__ == "__main__":
    sys.exit(main())
