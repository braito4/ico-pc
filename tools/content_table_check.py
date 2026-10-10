#!/usr/bin/env python3
"""tools/content_table_check.py: the content table (port/render/rd_content_hide.c)
against saved frames.

The renderer draws the native frustum at every aspect: outside the Original
preset a triangle that fails a VU program's region test is drawn, clipped by
the GPU, where the PS2 drops it.  The stage geometry made for the PS2's 4:3
picture is listed in the content table with a verb, HIDE (drawn nowhere) or
HIDE43 (drawn only inside the 4:3 columns, as the PS2 draws it).  This tool
drives port/render/rd_replay_tool over dumps; the table's verbs are
overridden per run through ICO_RD_CONTENT_HIDE (rd_mesh.c
rd__content_hide_set: "N=off", "N=hide", "N=hide43").

  decide  for every entry of the table, on every 4:3 dump whose list draws
          the entry's mesh: the frame at 4:3 (every target and the present,
          Original 1x and Enhanced 2x) with the entry off, HIDE43 and HIDE.
          off and HIDE43 must be the same, byte for byte (at 4:3 HIDE43 is
          the PS2's rule: the table changes nothing there).  HIDE the same
          as off on every such dump -> HIDE is enough (the triangles never
          reach the 4:3 picture); else HIDE43.  Exit 1 when off and HIDE43
          differ, or the table lists HIDE where a dump shows HIDE43 is
          needed.
  report  the meshes that are NOT listed and are expected not to need an
          entry: by default st26a_p1 and st26a_near (the opening's cliff
          and tree card) on the opening dumps and the waterfall mist
          wf_smoke1 (issue 57) on the patched waterfall dump.  For each
          dump at each aspect it holds (a v10 dump: its own aspect; an
          aspect sweep zip, Shift+F12 in photo mode: 4:3, 16:9, 32:9, 48:9
          and 60:9; a dump before v10 is 4:3 only, the wider aspects are
          reported as needing a sweep) and each mesh it draws: SCENE at 1x
          in Enhanced and in Original, each with and without the mesh's
          draws (--nop).  The pixels the mesh adds in Enhanced but not in
          Original are what its clipped triangles draw: their count inside
          the 4:3 columns and beside them, their box and mean colour, and a
          picture with them in magenta (OUT/report/...png).  A report, not
          a gate: a mesh whose clipped pixels show what the original never
          showed (rock, a trunk across the picture) gets an entry.
  all     decide over the dump list, then report (the default)

  python3 -I tools/content_table_check.py [--tool T] [--out DIR] [--dumps LIST]
          [decide | report [--mesh NAME ...] [SOURCE ...] | all]

The dump list is tools/native_frame_dumps.txt (or ICO_DUMPS, or --dumps):
machine-local paths, dumps are never committed.  The report's default
sources are the list's v722, v934 and v1251 dumps (opening) and its
fwdump/patched dump (waterfall), plus any sweep zip given as a SOURCE.  The
whole run holds the shared replay lock (build-host/tmp/replay.lock, or
ICO_REPLAY_LOCK).  Python 3 only (a PNG reader of its own).
Exit: 0 done, 1 a decide failure or an error, 2 usage, 77 nothing to run.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABLE = os.path.join(ROOT, "port", "render", "rd_content_hide.c")
DEFAULT_LIST = os.path.join(ROOT, "tools", "native_frame_dumps.txt")
DEFAULT_TOOL = os.path.join(ROOT, "build-host", "linux-x64", "port", "render", "rd_replay_tool")
DECIDE_SETTINGS = {
    "original_4x3_1x": ["--aspect", "4:3", "--present", "1440x1080"],
    "enhanced_4x3_2x": ["--enhanced", "--aspect", "4:3", "--resolution", "2x", "--present",
                        "2880x2160"],
}
REPORT_MESHES = {"opening": ["st26a_p1", "st26a_near"], "waterfall": ["wf_smoke1"]}
OPENING_DUMPS = ("v722", "v934", "v1251")
WATERFALL_DUMPS = ("fwdump/patched",)
SWEEP_ASPECTS = ["4x3", "16x9", "32x9", "48x9", "60x9"]
REPORT_ASPECTS = ["4x3", "16x9", "32x9", "48x9"]
ASPECT_VALUE = {"4x3": 4 / 3, "16x9": 16 / 9, "32x9": 32 / 9, "48x9": 48 / 9, "60x9": 60 / 9}
EXIT_SKIP = 77


def say(msg):
    print("content_table_check: " + msg, flush=True)


# ------------------------------------------------------------------ the table

def read_table(path=TABLE):
    """[(index, mesh, note, verb)] in the table's order."""
    src = open(path, encoding="utf-8").read()
    body = src[src.index("rd__content_hide[] = {"):]
    out = []
    for m in re.finditer(r'\{\s*"([^"]*)"\s*,\s*RD_(HIDE43|HIDE)\s*,', body):
        note = m.group(1)
        out.append((len(out), note.split(":", 1)[0].strip(), note, m.group(2)))
    return out


# -------------------------------------------------------------------- PNGs

def load_png(path):
    """(w, h, rows of RGB bytes) of an 8-bit RGB or RGBA PNG."""
    data = open(path, "rb").read()
    at, idat, w, h, ctype = 8, b"", 0, 0, 6
    while at < len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        chunk = data[at + 8:at + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", chunk[:10])
            if depth != 8 or ctype not in (2, 6):
                raise SystemExit("%s: not an 8-bit RGB(A) PNG" % path)
        elif kind == b"IDAT":
            idat += chunk
        at += 12 + n
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, k = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[k]
        k += 1
        r = bytearray(raw[k:k + stride])
        k += stride
        if f == 1:
            for x in range(bpp, stride):
                r[x] = (r[x] + r[x - bpp]) & 255
        elif f == 2:
            for x in range(stride):
                r[x] = (r[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                a = r[x - bpp] if x >= bpp else 0
                r[x] = (r[x] + (a + prev[x]) // 2) & 255
        elif f == 4:
            for x in range(stride):
                a = r[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                r[x] = (r[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(r))
        prev = r
    if bpp == 4:
        rows = [bytes(b for i, b in enumerate(row) if i % 4 != 3) for row in rows]
    return w, h, rows


def save_png(path, w, h, rows):
    """rows of RGB bytes to an 8-bit RGB PNG."""
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def same_png(a, b):
    if open(a, "rb").read() == open(b, "rb").read():
        return True
    return load_png(a) == load_png(b)


# -------------------------------------------------------------- the replays

class Lock:
    """The shared replay lock, held for the whole run."""
    def __init__(self):
        self.f = None

    def __enter__(self):
        p = os.environ.get("ICO_REPLAY_LOCK") or os.path.join(ROOT, "build-host", "tmp", "replay.lock")
        try:
            import fcntl
            os.makedirs(os.path.dirname(p), exist_ok=True)
            self.f = open(p, "a")
            fcntl.flock(self.f, fcntl.LOCK_EX)
        except (ImportError, OSError):
            self.f = None
        return self

    def __exit__(self, *a):
        if self.f:
            self.f.close()


def run_tool(tool, args, hide=None):
    env = dict(os.environ)
    env.pop("ICO_RD_CONTENT_HIDE", None)
    if hide:
        env["ICO_RD_CONTENT_HIDE"] = hide
    r = subprocess.run([tool] + args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                       env=env)
    return r.returncode, r.stdout


class DumpInfo:
    def __init__(self, path, version, gs_w, aspect, lines):
        self.path, self.version, self.gs_w, self.aspect, self.lines = path, version, gs_w, aspect, lines

    def draws_of(self, mesh):
        """the list:index of every draw of a mesh whose name holds `mesh`"""
        pat = re.compile(r"mesh (\S*%s\S*) \(" % re.escape(mesh))
        return [ln.split()[0] for ln in self.lines if "MESH" in ln or "SKINNED" in ln
                if pat.search(ln)]


def dump_info(tool, path):
    rc, out = run_tool(tool, [path, os.devnull, "--list", "--no-device"])
    m = re.search(r"dump: version (\d+), GS (\d+)x(\d+), aspect ([0-9.]+)", out)
    if rc != 0 or not m:
        say("%s: --list failed:\n%s" % (path, out[-2000:]))
        return None
    return DumpInfo(path, int(m.group(1)), int(m.group(2)), float(m.group(4)), out.splitlines())


def sweep_members(src, tmp):
    """[(aspect token, dump path)] of a source: an .rddump or F12 zip is one
    member (its own aspect, found later); a sweep zip (members named
    4x3.rddump ...) or a folder of them one per aspect."""
    names = []
    if os.path.isdir(src):
        names = [(f, os.path.join(src, f)) for f in sorted(os.listdir(src))]
    elif src.endswith(".zip"):
        z = zipfile.ZipFile(src)
        rd = [n for n in z.namelist() if n.endswith(".rddump")]
        if len(rd) > 1:
            d = tempfile.mkdtemp(dir=tmp)
            for n in rd:
                z.extract(n, d)
            names = [(n, os.path.join(d, n)) for n in rd]
        else:
            return [(None, src)]
    else:
        return [(None, src)]
    out = []
    for n, p in names:
        for a in SWEEP_ASPECTS:
            if re.search(r"(?<![0-9])%s(?![0-9])" % a, os.path.basename(n)) and p.endswith(".rddump"):
                out.append((a, p))
    return out


def aspect_token(value):
    best = min(ASPECT_VALUE, key=lambda a: abs(ASPECT_VALUE[a] - value))
    return best if abs(ASPECT_VALUE[best] - value) < 0.01 else "%.3f" % value


# ------------------------------------------------------------------ decide

def cmd_decide(args, dumps):
    table = read_table()
    if not table:
        say("the table is empty: nothing to decide")
        return 0
    os.makedirs(args.out, exist_ok=True)
    tmp = tempfile.mkdtemp(dir=args.out)
    evidence = {i: [] for i, _, _, _ in table}  # (dump, setting, same hide/off)
    fails = 0
    for src in dumps:
        for tok, path in sweep_members(src, tmp):
            if tok not in (None, "4x3"):
                continue
            info = dump_info(args.tool, path)
            if not info:
                fails += 1
                continue
            if aspect_token(info.aspect) != "4x3":
                continue  # a wide frame: the 4:3 picture is a sweep's 4x3 member
            for i, mesh, _, _ in table:
                if not info.draws_of(mesh):
                    continue
                for setting, sargs in DECIDE_SETTINGS.items():
                    dirs = {}
                    for verb in ("off", "hide43", "hide"):
                        d = os.path.join(tmp, "%d-%s-%s-%s" % (i, os.path.basename(path), setting, verb))
                        os.makedirs(d, exist_ok=True)
                        rc, out = run_tool(args.tool, [path, "-", "--pass-dump", d] + sargs,
                                           "%d=%s" % (i, verb))
                        if rc != 0:
                            say("%s %s %s: replay failed:\n%s" % (path, setting, verb, out[-2000:]))
                            fails += 1
                        dirs[verb] = d
                    pngs = sorted(f for f in os.listdir(dirs["off"]) if f.endswith(".png"))
                    diff43 = [f for f in pngs if not same_png(os.path.join(dirs["off"], f),
                                                              os.path.join(dirs["hide43"], f))]
                    diffh = [f for f in pngs if not same_png(os.path.join(dirs["off"], f),
                                                             os.path.join(dirs["hide"], f))]
                    if diff43:
                        say("FAIL entry %d (%s) %s %s: HIDE43 changes the 4:3 frame: %s" %
                            (i, mesh, os.path.basename(path), setting, " ".join(diff43)))
                        fails += 1
                    evidence[i].append((os.path.basename(path), setting, not diffh, diffh))
    shutil.rmtree(tmp, ignore_errors=True)
    for i, mesh, note, verb in table:
        ev = evidence[i]
        if not ev:
            say("entry %d %s (%s): no 4:3 dump draws it; no evidence" % (i, verb, mesh))
            continue
        need43 = [e for e in ev if not e[2]]
        verdict = "HIDE43" if need43 else "HIDE"
        for d, setting, same, diff in ev:
            say("entry %d (%s) %s %s: HIDE %s" %
                (i, mesh, d, setting, "is the same picture" if same else "changes " + " ".join(diff)))
        say("entry %d (%s): listed %s, the dumps say %s%s" %
            (i, mesh, verb, verdict, "" if verdict == verb else
             (" (HIDE would do)" if verdict == "HIDE" else " FAIL")))
        if verb == "HIDE" and verdict == "HIDE43":
            fails += 1
    return 1 if fails else 0


# ------------------------------------------------------------------ report

def scene(args, path, extra, nops, out):
    a = [path, out, "--target", "SCENE", "--resolution", "1x"] + extra
    for n in nops:
        a += ["--nop", n]
    rc, text = run_tool(args.tool, a)
    if rc != 0 or not os.path.exists(out):
        say("%s: replay failed (%s):\n%s" % (path, " ".join(a[2:]), text[-2000:]))
        return None
    return load_png(out)


def report_one(args, info, tok, mesh, rdir):
    nops = info.draws_of(mesh)
    if not nops:
        return None
    base = "%s-%s-%s" % (os.path.splitext(os.path.basename(info.path))[0], tok, mesh)
    imgs = {}
    for key, extra, n in (("E", ["--enhanced"], []), ("En", ["--enhanced"], nops),
                          ("O", [], []), ("On", [], nops)):
        imgs[key] = scene(args, info.path, extra, n, os.path.join(rdir, base + "-" + key + ".png"))
        if not imgs[key]:
            return "replay failed"
    w, h, e = imgs["E"]
    if any(imgs[k][0] != w or imgs[k][1] != h for k in imgs):
        return "the four pictures differ in size"
    en, o, on = imgs["En"][2], imgs["O"][2], imgs["On"][2]
    s = w / float(info.gs_w)
    c0, c1 = int(w / 2 - 256 * s), int(w / 2 + 256 * s + 0.999)
    n_e = n_o = n_in = n_out = 0
    box = [w, h, -1, -1]
    acc = [0, 0, 0]
    hl = []
    for y in range(h):
        re_, rn, ro, rp = e[y], en[y], o[y], on[y]
        row = bytearray(re_)
        for x in range(w):
            i = 3 * x
            add_e = re_[i:i + 3] != rn[i:i + 3]
            add_o = ro[i:i + 3] != rp[i:i + 3]
            n_e += add_e
            n_o += add_o
            if add_e and not add_o:
                if c0 <= x < c1:
                    n_in += 1
                else:
                    n_out += 1
                box = [min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)]
                for k in range(3):
                    acc[k] += re_[i + k]
                row[i:i + 3] = b"\xff\x00\xff"
        hl.append(bytes(row))
    clip = n_in + n_out
    for k in ("E", "En", "O", "On"):
        os.remove(os.path.join(rdir, base + "-" + k + ".png"))
    msg = ("%d draws; Enhanced adds %d pixels, Original %d; drawn only clipped: %d (%d in the 4:3 "
           "columns %d..%d, %d beside)" % (len(nops), n_e, n_o, clip, n_in, c0, c1 - 1, n_out))
    if clip:
        save_png(os.path.join(rdir, base + "-clipped.png"), w, h, hl)
        msg += ", box (%d,%d)-(%d,%d), mean colour %d,%d,%d -> %s-clipped.png" % (
            box[0], box[1], box[2], box[3], acc[0] // clip, acc[1] // clip, acc[2] // clip, base)
    return msg


def cmd_report(args, groups):
    rdir = os.path.join(args.out, "report")
    os.makedirs(rdir, exist_ok=True)
    tmp = tempfile.mkdtemp(dir=args.out)
    ran = 0
    for meshes, sources in groups:
        for src in sources:
            members = sweep_members(src, tmp)
            seen = set()
            for tok, path in members:
                info = dump_info(args.tool, path)
                if not info:
                    continue
                tok = aspect_token(info.aspect) if tok is None else tok
                seen.add(tok)
                for mesh in meshes:
                    msg = report_one(args, info, tok, mesh, rdir)
                    ran += msg is not None
                    say("%s %s %s: %s" % (os.path.basename(src), tok, mesh,
                                          msg if msg else "not drawn in this frame"))
            missing = [a for a in REPORT_ASPECTS if a not in seen]
            if missing:
                say("%s: no frame at %s: capture an aspect sweep (Shift+F12 in photo mode) of "
                    "this state%s" % (os.path.basename(src), ", ".join(missing),
                                      " (a dump before v10 is 4:3 only)" if seen == {"4x3"} else ""))
    shutil.rmtree(tmp, ignore_errors=True)
    say("report: %d mesh pictures in %s" % (ran, rdir))
    return 0


# -------------------------------------------------------------------- main

def read_list(path):
    if os.path.isdir(path):
        return sorted(os.path.join(path, f) for f in os.listdir(path)
                      if f.endswith((".rddump", ".zip")))
    if path.endswith((".rddump", ".zip")):
        return [path]
    import glob
    base = os.path.dirname(os.path.abspath(path))
    out = []
    for line in open(path, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        p = line if os.path.isabs(line) else os.path.join(base, line)
        out.extend(sorted(glob.glob(p)) if ("*" in p or "?" in p) else [p])
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--tool", default=os.environ.get("ICO_REPLAY_TOOL", DEFAULT_TOOL))
    p.add_argument("--out", default=os.path.join(ROOT, "build-host", "tmp", "content_table_check"))
    p.add_argument("--dumps", default=os.environ.get("ICO_DUMPS") or DEFAULT_LIST)
    p.add_argument("--mesh", action="append", help="report: a mesh name (repeatable)")
    p.add_argument("mode", nargs="?", default="all", choices=["decide", "report", "all"])
    p.add_argument("sources", nargs="*", help="report: dumps, F12 zips or aspect sweep zips")
    args = p.parse_args()
    if not os.path.exists(args.tool):
        say("no replay tool at %s (build rd_replay_tool, or --tool)" % args.tool)
        return 2
    dumps = [d for d in read_list(args.dumps) if os.path.exists(d)]
    if args.mesh or args.sources:
        groups = [(args.mesh or REPORT_MESHES["opening"] + REPORT_MESHES["waterfall"],
                   args.sources or dumps)]
    else:
        opening = [d for d in dumps if any(re.search(r"-%s\.(rddump|zip)$" % t, d) for t in OPENING_DUMPS)]
        water = [d for d in dumps if any(t in d for t in WATERFALL_DUMPS)]
        groups = [(REPORT_MESHES["opening"], opening), (REPORT_MESHES["waterfall"], water)]
    if not dumps and not args.sources:
        say("SKIP: no dump of %s exists" % args.dumps)
        return EXIT_SKIP
    rc = 0
    with Lock():
        if args.mode in ("decide", "all"):
            rc |= cmd_decide(args, dumps)
        if args.mode in ("report", "all"):
            rc |= cmd_report(args, groups)
    say("ok" if rc == 0 else "FAILED")
    return rc


if __name__ == "__main__":
    sys.exit(main())
