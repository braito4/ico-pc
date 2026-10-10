#!/usr/bin/env python3
"""tools/native_frame_check.py: the native-frame gate harness (v0.6.0).

Drives port/render/rd_replay_tool over saved frame dumps (.rddump or the F12
.zip) and checks two things.

  baseline  render every dump of a dump list at the gate settings, every named
            target, and write a manifest of SHA-256 hashes (docs/native_frame_baseline.json)
  gate-a    render again and compare with the manifest, exactly; every
            mismatch is reported with dump, settings, target and, when the
            baseline picture is kept (--ref-dir), the first differing pixel;
            exit 1 on any mismatch.  Original at 4:3 stays byte-identical.
  gate-b    one game state captured at 4:3, 16:9, 32:9, 48:9 and 60:9 (an
            "aspect sweep": a zip, a folder, or PNG files; the aspect is a
            token in the file or folder name: 4x3, 16x9, 32x9, 48x9, 60x9):
            the centre columns [(W/2 - 256) * s, + 512 * s) of every aspect
            equal the 4:3 picture byte for byte.  --effects R allows a band
            of R pixels at each old edge (a kernel radius) and reports the
            largest difference inside it; a continuity check compares the
            gradient at each old edge column with its +-16 column
            neighbourhood.  An allow-list json suppresses explained
            differences.
  selftest  synthetic checks of the gate-b logic (no dumps, no GPU)

The dump list is a text file of paths, one per line ('#' comments, blank lines
ignored; relative paths are relative to the list file).  It is machine-local:
dumps are never committed.  ICO_DUMPS names the list (or a folder of
.rddump / .zip files) for the ctests.

The replay tool is slow to start, so a run renders with a few workers; the
whole run holds the shared replay lock (build-host/tmp/replay.lock, or
ICO_REPLAY_LOCK) so two runs never compete for the GPU/CPU.

Exit: 0 pass, 1 mismatch or error, 2 usage, 77 skipped (nothing to run).
Needs Python 3 and Pillow.  Run it as `python3 -I tools/native_frame_check.py`.
"""
import argparse
import concurrent.futures
import fnmatch
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_LIST = os.path.join(ROOT, "tools", "native_frame_dumps.txt")
DEFAULT_MANIFEST = os.path.join(ROOT, "docs", "native_frame_baseline.json")
DEFAULT_TOOL = os.path.join(ROOT, "build-host", "linux-x64", "port", "render", "rd_replay_tool")
MANIFEST_VERSION = 1

# every named target of rd_replay_tool (its kNames), then the presented picture
TARGETS = [
    "SCENE", "DISPLAY", "SHADOW0", "SHADOW1", "SHADOW2", "WORK0", "WORK1", "WORK2",
    "WORK3", "AA0", "AA1", "FEED128", "AURA_WORK", "AURA_TAP", "WORK2_PAD", "FEED_HELD",
    "DISPLAY_HELD",
]
PRESENT = "PRESENT"  # pseudo target: --present into a 4:3 box (the Original presenter)

# The gate settings: argument lists added to "rd_replay_tool DUMP OUT".  A dump
# carries no display options, so the replay takes them here.
SETTINGS = {
    "original_4x3_1x": ["--aspect", "4:3"],
    "enhanced_4x3_2x": ["--enhanced", "--aspect", "4:3", "--resolution", "2x"],
}
PRESENT_BOX = {"original_4x3_1x": "1440x1080", "enhanced_4x3_2x": "2880x2160"}

ASPECTS = ["4x3", "16x9", "32x9", "48x9", "60x9"]
ASPECT_RE = re.compile(r"(?<![0-9])(4[x_:-]3|16[x_:-]9|32[x_:-]9|48[x_:-]9|60[x_:-]9)(?![0-9])")
EXIT_SKIP = 77


def die(msg, code=1):
    print("native_frame_check: " + msg, file=sys.stderr)
    sys.exit(code)


def need_pillow():
    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        print("native_frame_check: SKIP: Pillow is not installed")
        sys.exit(EXIT_SKIP)


def git_info():
    def run(*a):
        try:
            return subprocess.run(["git", "-C", ROOT] + list(a), capture_output=True, text=True,
                                  timeout=30).stdout.strip()
        except (OSError, subprocess.SubprocessError):
            return ""
    return {"commit": run("rev-parse", "HEAD"), "dirty": bool(run("status", "--porcelain", "--", "tools/native_frame_check.py"))}


# ---------------------------------------------------------------- dump lists

def read_list(path):
    """Paths from a list file, a folder of dumps, or a single dump."""
    if os.path.isdir(path):
        return sorted(os.path.join(path, f) for f in os.listdir(path)
                      if f.endswith((".rddump", ".zip")))
    if path.endswith((".rddump", ".zip")):
        return [path]
    base = os.path.dirname(os.path.abspath(path))
    out = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            if "*" in line or "?" in line:
                import glob
                pat = line if os.path.isabs(line) else os.path.join(base, line)
                out.extend(sorted(glob.glob(pat)))
            else:
                out.append(line if os.path.isabs(line) else os.path.join(base, line))
    return out


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for blk in iter(lambda: f.read(1 << 20), b""):
            h.update(blk)
    return h.hexdigest()


# ------------------------------------------------------------------ rendering

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


def render_args(setting, target):
    a = list(SETTINGS[setting])
    if target == PRESENT:
        a += ["--present", PRESENT_BOX[setting]]
    else:
        a += ["--target", target]
    return a


def pixel_hash(img):
    img = img.convert("RGBA")
    h = hashlib.sha256()
    h.update(("%dx%d\n" % img.size).encode())
    h.update(img.tobytes())
    return {"w": img.size[0], "h": img.size[1], "sha256": h.hexdigest()}


def render_one(tool, dump, setting, target, keep_to=None):
    """Render one target; returns (record, PIL image or None)."""
    from PIL import Image
    with tempfile.TemporaryDirectory(prefix="nfc") as td:
        out = os.path.join(td, "o.png")
        cmd = [tool, dump, out] + render_args(setting, target)
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
        except subprocess.TimeoutExpired:
            return {"error": "timeout"}, None
        if r.returncode == EXIT_SKIP:
            return {"error": "skip"}, None
        if r.returncode != 0 or not os.path.exists(out):
            msg = (r.stdout + r.stderr).strip().splitlines()
            return {"error": "no picture: " + (msg[-1] if msg else "rc=%d" % r.returncode)}, None
        img = Image.open(out)
        img.load()
        rec = pixel_hash(img)
        if keep_to:
            os.makedirs(os.path.dirname(keep_to), exist_ok=True)
            img.convert("RGBA").save(keep_to)
        return rec, img


def render_all(tool, dumps, jobs, ref_dir, targets, settings):
    """{dumpname: {setting: {target: record}}} for the dumps that exist."""
    from PIL import Image  # noqa: F401
    work = []
    for d in dumps:
        for s in settings:
            for t in targets + [PRESENT]:
                work.append((d, s, t))
    results = {}

    def one(w):
        d, s, t = w
        keep = None
        if ref_dir:
            keep = os.path.join(ref_dir, os.path.basename(d), s, t + ".png")
        rec, _ = render_one(tool, d, s, t, keep)
        return w, rec

    done = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        for w, rec in ex.map(one, work):
            d, s, t = w
            results.setdefault(os.path.basename(d), {}).setdefault(s, {})[t] = rec
            done += 1
            if done % 50 == 0 or done == len(work):
                print("  rendered %d / %d" % (done, len(work)), flush=True)
    return results


def pick_dumps(args):
    lst = args.dumps or os.environ.get("ICO_DUMPS") or DEFAULT_LIST
    if not os.path.exists(lst):
        print("native_frame_check: SKIP: no dump list (%s)" % lst)
        sys.exit(EXIT_SKIP)
    paths = read_list(lst)
    have = [p for p in paths if os.path.isfile(p)]
    for p in paths:
        if p not in have:
            print("  missing dump: %s" % p)
    if args.only:
        have = [p for p in have if args.only in os.path.basename(p)]
    if not have:
        print("native_frame_check: SKIP: none of the list's dumps exists")
        sys.exit(EXIT_SKIP)
    names = [os.path.basename(p) for p in have]
    if len(set(names)) != len(names):
        die("two dumps share a file name; the manifest keys on the name")
    return have


def pick_tool(args):
    tool = args.tool or os.environ.get("ICO_REPLAY_TOOL") or DEFAULT_TOOL
    if not os.access(tool, os.X_OK):
        print("native_frame_check: SKIP: no rd_replay_tool at %s" % tool)
        sys.exit(EXIT_SKIP)
    return tool


# ------------------------------------------------------------------- baseline

def cmd_baseline(args):
    need_pillow()
    tool = pick_tool(args)
    dumps = pick_dumps(args)
    settings = list(SETTINGS)
    with Lock():
        res = render_all(tool, dumps, args.jobs, args.ref_dir, TARGETS, settings)
    bad = [(d, s, t, r["error"]) for d, ss in res.items() for s, tt in ss.items()
           for t, r in tt.items() if "error" in r]
    for b in bad:
        print("  error: %s %s %s: %s" % b)
    if bad and not args.keep_going:
        die("%d targets did not render (--keep-going records them as errors)" % len(bad))
    g = git_info()
    man = {
        "version": MANIFEST_VERSION,
        "what": "SHA-256 of the RGBA pixels (with the size) of every rd_replay_tool target; hashes only",
        "harness_commit": g["commit"],
        "harness_dirty": g["dirty"],
        "settings": {s: {"args": SETTINGS[s], "present_box": PRESENT_BOX[s]} for s in settings},
        "targets": TARGETS + [PRESENT],
        "dumps": {},
    }
    for p in dumps:
        n = os.path.basename(p)
        man["dumps"][n] = {"size": os.path.getsize(p), "sha256": file_sha256(p), "results": res[n]}
    out = args.manifest
    with open(out, "w", encoding="utf-8") as f:
        json.dump(man, f, indent=1, sort_keys=False)
        f.write("\n")
    print("wrote %s: %d dumps, %d settings, %d targets" % (out, len(dumps), len(settings), len(TARGETS) + 1))
    return 0


# --------------------------------------------------------------------- gate A

def bbox_all(im):
    """Bounding box of the non-zero pixels over all four channels (getbbox looks at alpha only)."""
    try:
        return im.getbbox(alpha_only=False)
    except TypeError:
        r, g, b, a = im.split()
        from PIL import ImageChops
        return ImageChops.lighter(ImageChops.lighter(r, g), ImageChops.lighter(b, a)).getbbox()


def first_diff(a, b):
    """(x, y, pixel_a, pixel_b) of the first differing pixel, or None."""
    from PIL import ImageChops
    a = a.convert("RGBA")
    b = b.convert("RGBA")
    if a.size != b.size:
        return None
    box = bbox_all(ImageChops.difference(a, b))
    if not box:
        return None
    pa = a.load()
    pb = b.load()
    for y in range(box[1], box[3]):
        for x in range(box[0], box[2]):
            if pa[x, y] != pb[x, y]:
                return x, y, pa[x, y], pb[x, y]
    return None


def ctest_guard(args):
    if args.ctest and not os.environ.get("ICO_DUMPS"):
        print("native_frame_check: SKIP: ICO_DUMPS is not set")
        sys.exit(EXIT_SKIP)


def cmd_gate_a(args):
    ctest_guard(args)
    need_pillow()
    from PIL import Image
    if not os.path.exists(args.manifest):
        die("no manifest %s (run baseline)" % args.manifest)
    with open(args.manifest, encoding="utf-8") as f:
        man = json.load(f)
    tool = pick_tool(args)
    dumps = pick_dumps(args)
    known = [p for p in dumps if os.path.basename(p) in man["dumps"]]
    for p in dumps:
        if p not in known:
            print("  not in the manifest (skipped): %s" % os.path.basename(p))
    if not known:
        print("native_frame_check: SKIP: no listed dump is in the manifest")
        return EXIT_SKIP
    settings = [s for s in SETTINGS if s in man["settings"]]
    mism = 0
    with Lock():
        cur = render_all(tool, known, args.jobs, None, man["targets"][:-1], settings)
    checked = 0
    for p in known:
        n = os.path.basename(p)
        ent = man["dumps"][n]
        if ent["size"] != os.path.getsize(p) or ent["sha256"] != file_sha256(p):
            print("MISMATCH %s: the dump file differs from the one the manifest was made from" % n)
            mism += 1
            continue
        for s in settings:
            for t in man["targets"]:
                want = ent["results"][s][t]
                got = cur[n][s][t]
                checked += 1
                if want == got:
                    continue
                mism += 1
                line = "MISMATCH %s [%s] %s: " % (n, s, t)
                if "error" in want or "error" in got:
                    line += "baseline %s, now %s" % (want.get("error", "renders"), got.get("error", "renders"))
                else:
                    line += "baseline %dx%d %s.., now %dx%d %s.." % (
                        want["w"], want["h"], want["sha256"][:12], got["w"], got["h"], got["sha256"][:12])
                print(line)
                ref = os.path.join(args.ref_dir, n, s, t + ".png") if args.ref_dir else None
                if ref and os.path.exists(ref) and "error" not in got:
                    rec, img = render_one(tool, p, s, t)
                    old = Image.open(ref)
                    if old.size != img.size:
                        print("    size changed: %s -> %s" % (old.size, img.size))
                    else:
                        d = first_diff(old, img)
                        if d:
                            print("    first differing pixel (%d, %d): baseline %s, now %s" % d)
                else:
                    print("    (no kept baseline picture: re-run baseline with --ref-dir to get the pixel)")
    print("gate-a: %d targets checked, %d mismatches" % (checked, mism))
    return 1 if mism else 0


# --------------------------------------------------------------------- gate B

def aspect_token(s):
    m = ASPECT_RE.search(s)
    if not m:
        return None, s
    tok = re.sub(r"[_:-]", "x", m.group(1))
    return tok, (s[:m.start()] + s[m.end():])


def load_sweep(src):
    """{group: {aspect: PIL image}} from a zip, a folder or one PNG."""
    from PIL import Image
    files = []  # (name, loader)
    if os.path.isdir(src):
        for dp, _, fs in os.walk(src):
            for f in fs:
                if f.lower().endswith(".png"):
                    p = os.path.join(dp, f)
                    files.append((os.path.relpath(p, src), p))
    elif src.lower().endswith(".zip"):
        z = zipfile.ZipFile(src)
        for n in z.namelist():
            if n.lower().endswith(".png"):
                files.append((n, (z, n)))
    elif src.lower().endswith(".png"):
        files.append((os.path.basename(src), src))
    groups = {}
    for name, ld in files:
        tok, rest = aspect_token(name)
        if not tok:
            continue
        rest = re.sub(r"[/\\._-]+", "/", rest.rsplit(".", 1)[0]).strip("/")
        if isinstance(ld, tuple):
            img = Image.open(io.BytesIO(ld[0].read(ld[1])))
        else:
            img = Image.open(ld)
        img.load()
        groups.setdefault(rest, {})[tok] = img.convert("RGBA")
    return groups


def check_group(name, imgs, effects, allow, out):
    """One group (a target of one sweep).  Returns the number of failures."""
    from PIL import ImageChops
    if "4x3" not in imgs:
        out.append("FAIL %s: no 4:3 picture in the sweep" % name)
        return 1
    ref = imgs["4x3"]
    w43, h = ref.size
    if w43 % 512:
        out.append("FAIL %s: the 4:3 picture is %d wide, not a multiple of 512" % (name, w43))
        return 1
    s = w43 // 512
    fails = 0
    for asp in ASPECTS[1:]:
        if asp not in imgs:
            continue
        img = imgs[asp]
        W, H = img.size
        # (W/2 - 256) * s with W the native width: half the width difference
        x0 = (W - w43) // 2 if (W - w43) % 2 == 0 else None
        if H != h or x0 is None or x0 < 0 or x0 + w43 > W:
            out.append("FAIL %s %s: picture %dx%d does not fit a 4:3 picture of %dx%d at scale %d"
                       % (name, asp, W, H, w43, h, s))
            fails += 1
            continue
        centre = img.crop((x0, 0, x0 + w43, h))
        diff = ImageChops.difference(centre, ref)
        r = effects
        # the allowed region: the band at each old edge (the 4:3 picture's own edges)
        bad_cols = []
        box = bbox_all(diff)
        if box:
            px = diff.load()
            for x in range(box[0], box[2]):
                if r and (x < r or x >= w43 - r):
                    continue
                for y in range(box[1], box[3]):
                    if px[x, y] != (0, 0, 0, 0):
                        bad_cols.append((x, y))
                        break
        # allow-list: ranges of 4:3 picture columns (and optional rows) explained
        unexplained = []
        explained = {}
        for (x, y) in bad_cols:
            why = allowed(allow, name, asp, x, y)
            if why:
                explained[why] = explained.get(why, 0) + 1
            else:
                unexplained.append((x, y))
        if r:
            inband = 0
            mx = 0
            px = diff.load()
            for x in list(range(0, min(r, w43))) + list(range(max(w43 - r, 0), w43)):
                for y in range(h):
                    v = max(px[x, y])
                    if v:
                        inband += 1
                        mx = max(mx, v)
            out.append("info  %s %s: band %d px at each old edge: %d differing pixels, max channel difference %d"
                       % (name, asp, r, inband, mx))
        for why, n in explained.items():
            out.append("info  %s %s: %d columns differ, allowed (%s)" % (name, asp, n, why))
        if unexplained:
            x, y = unexplained[0]
            out.append("FAIL %s %s: centre differs from 4:3 in %d columns; first at 4:3 column %d row %d: 4:3 %s, %s %s"
                       % (name, asp, len(unexplained), x, y, ref.getpixel((x, y)), asp,
                          centre.getpixel((x, y))))
            fails += 1
        else:
            out.append("ok    %s %s: centre columns [%d, %d) equal the 4:3 picture" % (name, asp, x0, x0 + w43))
        # continuity at the old edges
        for label, e in (("left", x0), ("right", x0 + w43)):
            msg = continuity(img, e, args_frac=allow.get("_continuity_frac", 0.02))
            if msg:
                key = allowed_cont(allow, name, asp, label)
                if key:
                    out.append("info  %s %s: %s edge continuity: %s (allowed: %s)" % (name, asp, label, msg, key))
                else:
                    out.append("FAIL %s %s: %s edge (column %d) continuity: %s" % (name, asp, label, e, msg))
                    fails += 1
    return fails


def continuity(img, e, args_frac):
    """None if |grad| at the edge column is within the 99th percentile of the
    +-16 neighbourhood for all but args_frac of the rows, per channel."""
    W, H = img.size
    lo = max(e - 16, 0)
    hi = min(e + 16, W - 1)
    if hi - lo < 4 or e <= 0 or e >= W:
        return None
    px = img.load()
    msgs = []
    for ch, cname in enumerate("RGB"):
        # gradient at column x: |p[x] - p[x-1]|; the edge column e has p[e] - p[e-1]
        vals = []
        for x in range(lo + 1, hi + 1):
            if x == e:
                continue
            for y in range(H):
                vals.append(abs(px[x, y][ch] - px[x - 1, y][ch]))
        vals.sort()
        p99 = vals[min(len(vals) - 1, int(len(vals) * 0.99))]
        over = 0
        for y in range(H):
            if abs(px[e, y][ch] - px[e - 1, y][ch]) > p99:
                over += 1
        frac = over / float(H)
        # the 99th percentile of the neighbourhood lets 1 % of its rows through;
        # the edge column may exceed it for the same share, plus the slack
        if frac > 0.01 + args_frac:
            msgs.append("%s: %.1f%% of rows exceed the neighbourhood's 99th percentile (%d)"
                        % (cname, frac * 100, p99))
    return "; ".join(msgs) if msgs else None


def allowed(allow, name, asp, x, y):
    for e in allow.get("diffs", []):
        if not fnmatch.fnmatch(name, e.get("group", "*")):
            continue
        if e.get("aspect") not in (None, asp):
            continue
        c = e.get("cols", [0, 1 << 30])
        r = e.get("rows", [0, 1 << 30])
        if c[0] <= x < c[1] and r[0] <= y < r[1]:
            return e.get("reason", "listed")
    return None


def allowed_cont(allow, name, asp, label):
    for e in allow.get("continuity", []):
        if fnmatch.fnmatch(name, e.get("group", "*")) and e.get("aspect") in (None, asp) \
                and e.get("edge") in (None, label):
            return e.get("reason", "listed")
    return None


def cmd_gate_b(args):
    ctest_guard(args)
    need_pillow()
    srcs = list(args.sweep)
    env = os.environ.get("ICO_SWEEPS")
    if not srcs and env:
        srcs = [p for p in env.split(os.pathsep) if p]
    srcs = [s for s in srcs if os.path.exists(s)]
    if not srcs:
        print("native_frame_check: SKIP: no aspect sweep (give files, or ICO_SWEEPS)")
        return EXIT_SKIP
    allow = {}
    if args.allow:
        with open(args.allow, encoding="utf-8") as f:
            allow = json.load(f)
    fails = 0
    groups = 0
    for src in srcs:
        for name, imgs in sorted(load_sweep(src).items()):
            out = []
            fails += check_group(os.path.basename(src.rstrip("/")) + ":" + name, imgs, args.effects, allow, out)
            groups += 1
            print("\n".join(out))
    if not groups:
        print("native_frame_check: SKIP: the sweeps hold no picture with an aspect token")
        return EXIT_SKIP
    print("gate-b: %d pictures sets, %d failures" % (groups, fails))
    return 1 if fails else 0


# ------------------------------------------------------------------- selftest

def cmd_selftest(args):
    need_pillow()
    from PIL import Image
    s = 1
    h = 24

    # smooth centre so the edge columns continue smoothly into smooth sides
    centre = {}
    for y in range(h):
        for x in range(512):
            v = x // 4 + y + 40
            centre[(x, y)] = (v, v, v, 255)
    ref = Image.new("RGBA", (512 * s, h))
    ref.putdata([centre[(x, y)] for y in range(h) for x in range(512)])

    def smooth_sides(W):
        im = Image.new("RGBA", (W, h))
        px = im.load()
        x0 = W // 2 - 256
        for y in range(h):
            for x in range(W):
                if x0 <= x < x0 + 512:
                    px[x, y] = centre[(x - x0, y)]
                else:
                    v = (x - x0) // 4 + y + 40
                    px[x, y] = (v, v, v, 255)
        return im

    ok = True

    def expect(label, cond):
        nonlocal ok
        print(("ok   " if cond else "FAIL ") + label)
        ok = ok and cond

    good = {"4x3": ref, "16x9": smooth_sides(912)}
    out = []
    expect("identical centre passes", check_group("t", good, 0, {}, out) == 0)
    bad = smooth_sides(912)
    bad.putpixel((912 // 2 - 256 + 100, 5), (1, 2, 3, 255))
    out = []
    expect("one differing centre pixel fails", check_group("t", {"4x3": ref, "16x9": bad}, 0, {}, out) == 1)
    expect("the difference is named", any("column 100 row 5" in o for o in out))
    allow = {"diffs": [{"group": "t", "cols": [100, 101], "reason": "test"}]}
    out = []
    expect("allow-list suppresses it", check_group("t", {"4x3": ref, "16x9": bad}, 0, allow, out) == 0)
    edge = smooth_sides(912)
    edge.putpixel((912 // 2 - 256 + 1, 9), (200, 200, 200, 255))
    out = []
    expect("a difference inside the effects band passes", check_group("t", {"4x3": ref, "16x9": edge}, 4, {}, out) == 0)
    out = []
    expect("the same difference fails without a band", check_group("t", {"4x3": ref, "16x9": edge}, 0, {}, out) == 1)
    # a hard step at the old edge: sides black, centre grey
    step = smooth_sides(912)
    px = step.load()
    x0 = 912 // 2 - 256
    for y in range(h):
        for x in range(x0 - 16, x0):
            px[x, y] = (0, 0, 0, 255)
        for x in range(x0 + 1, x0 + 16):
            px[x, y] = (128, 128, 128, 255)
        px[x0, y] = (128, 128, 128, 255)
    st = {"4x3": Image.new("RGBA", (512, h), (128, 128, 128, 255)), "16x9": step}
    for x in range(x0 + 1, x0 + 512):
        for y in range(h):
            px[x, y] = (128, 128, 128, 255)
    out = []
    expect("a step at the old edge fails the continuity check", check_group("t", st, 0, {}, out) >= 1)
    expect("aspect tokens", aspect_token("shots/16x9/WORK0.png")[0] == "16x9"
           and aspect_token("a-4_3-b.png")[0] == "4x3" and aspect_token("x.png")[0] is None)
    return 0 if ok else 1


# ----------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="mode", required=True)

    def common(p):
        p.add_argument("--tool", help="rd_replay_tool (default: ICO_REPLAY_TOOL or build-host/linux-x64/...)")
        p.add_argument("--dumps", help="dump list file or folder (default: ICO_DUMPS, then tools/native_frame_dumps.txt)")
        p.add_argument("--ctest", action="store_true",
                       help="skip (77) unless ICO_DUMPS is set (the ctests)")
        p.add_argument("--only", help="only dumps whose file name contains this text")
        p.add_argument("--manifest", default=DEFAULT_MANIFEST)
        p.add_argument("--jobs", type=int, default=int(os.environ.get("ICO_NFC_JOBS", "3")))
        p.add_argument("--ref-dir", default=os.environ.get("ICO_NFC_REF_DIR"),
                       help="keep (baseline) or read (gate-a) the baseline pictures here, for the pixel report")

    p = sub.add_parser("baseline", help="write the manifest")
    common(p)
    p.add_argument("--keep-going", action="store_true", help="record targets that do not render as errors")
    p = sub.add_parser("gate-a", help="compare with the manifest, exactly")
    common(p)
    p = sub.add_parser("gate-b", help="centre columns of an aspect sweep against its 4:3 picture")
    p.add_argument("sweep", nargs="*", help="zip, folder or PNG files of one state (default: ICO_SWEEPS)")
    p.add_argument("--ctest", action="store_true", help="skip (77) unless ICO_DUMPS is set (the ctests)")
    p.add_argument("--effects", type=int, default=0, metavar="R",
                   help="Effects on: allow a band of R pixels at each old edge (a kernel radius)")
    p.add_argument("--allow", help="allow-list json: {\"diffs\": [{group, aspect, cols, rows, reason}], "
                                   "\"continuity\": [{group, aspect, edge, reason}]}")
    sub.add_parser("selftest", help="synthetic checks of the gate-b logic")
    args = ap.parse_args()
    if args.mode == "baseline":
        rc = cmd_baseline(args)
    elif args.mode == "gate-a":
        rc = cmd_gate_a(args)
    elif args.mode == "gate-b":
        rc = cmd_gate_b(args)
    else:
        rc = cmd_selftest(args)
    if rc == EXIT_SKIP:
        print("native_frame_check: SKIP")
    sys.exit(rc)


if __name__ == "__main__":
    main()
