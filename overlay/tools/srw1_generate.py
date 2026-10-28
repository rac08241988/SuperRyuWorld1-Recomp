#!/usr/bin/env python3
"""One-click generate for Super Ryu World 1 (codegen-host CLI contract).

The launcher's builder runs this as `<python> tools/srw1_generate.py generate
--rom R --cfg-dir ... --out-dir ... [--funcs-h ...] [--json-progress] ...`.
It performs exactly what tools/regen_srw1.sh does -- merge the stock bank
cfgs with the SRW1 overlay, then generate C from the player's own ROM into
src/gen-srw1 -- through the snesrecomp SDK `generate` command, so progress
JSON and exit codes (0 ok, 1 error, 2 usage, 3 ROM verify failed) are the
SDK's own. Nothing ROM-derived exists until this runs on the player's PC.
"""
import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys

EXPECT_SHA256 = "d7c20cdf42e66a9b81d49359affd4e3548de5855444faceccfac8a1c845bf57f"
ROOT = pathlib.Path(__file__).resolve().parent.parent

DROP_LINES = {
    "*": ["name 0086df GameMode14_InLevel_0086DF"],
    "bank00.cfg": ["hle_spc_upload 8079 live"],
}
OVERLAY_HEADER = re.compile(r"^(bank|includes|comment)[ \t]*=")


def load_strip_patterns(path):
    """recomp/srw1/strip.sed holds `/regex/d` lines (GNU BRE); translate."""
    pats = []
    if not path.is_file():
        return pats
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if not (line.startswith("/") and line.endswith("/d")):
            raise SystemExit("unsupported strip.sed line: " + line)
        bre = line[1:-2]
        ere = (bre.replace(r"\(", "\x00").replace(r"\)", "\x01").replace(r"\|", "\x02")
                  .replace("(", r"\(").replace(")", r"\)").replace("|", r"\|")
                  .replace("\x00", "(").replace("\x01", ")").replace("\x02", "|"))
        pats.append(re.compile(ere))
    return pats


def merge_cfgs(cfg_out):
    if cfg_out.exists():
        shutil.rmtree(cfg_out)
    cfg_out.mkdir(parents=True)
    strip = load_strip_patterns(ROOT / "recomp/srw1/strip.sed")
    for src in sorted((ROOT / "recomp").glob("bank*.cfg")):
        lines = src.read_text(encoding="utf-8").split("\n")
        drop = set(DROP_LINES["*"]) | set(DROP_LINES.get(src.name, []))
        kept = [l for l in lines if l not in drop and not any(p.search(l) for p in strip)]
        (cfg_out / src.name).write_text("\n".join(kept), encoding="utf-8", newline="\n")
    for ov in sorted((ROOT / "recomp/srw1").glob("bank*.cfg")):
        dest = cfg_out / ov.name
        text = ov.read_text(encoding="utf-8")
        if dest.exists():
            body = "\n".join(l for l in text.split("\n") if not OVERLAY_HEADER.match(l))
            with open(dest, "a", encoding="utf-8", newline="\n") as f:
                f.write("\n" + body)
        else:
            shutil.copyfile(ov, dest)


def source_roots():
    roots = [ROOT / "recomp/renderer_aot_roots.c"]
    found = []
    for dp, dn, fn in os.walk(ROOT / "src"):
        rel = pathlib.Path(dp).relative_to(ROOT).as_posix()
        dn[:] = [d for d in dn
                 if not (rel == "src" and (d in ("gen", "gen-coop", "gen-srw1") or d.startswith(".gen")))]
        for f in fn:
            if f.endswith(".c") and f != "coop_patch.c":
                found.append((pathlib.Path(rel) / f).as_posix())
    # match `find src ... | sort` under LC_ALL=C-ish byte order
    roots += [ROOT / p for p in sorted(found)]
    return roots


def funcs_h_only(out):
    """Setup build: derive funcs.h from the cfgs alone (no ROM involved)."""
    import tempfile
    with tempfile.TemporaryDirectory(prefix="srw1-cfg-") as tmp:
        cfg = pathlib.Path(tmp) / "cfg"
        merge_cfgs(cfg)
        rc = subprocess.call([sys.executable,
                              str(ROOT / "snesrecomp/tools/v2_sync_funcs_h.py"),
                              "--cfg-dir", str(cfg), "--out", str(out)],
                             cwd=str(ROOT), stdout=subprocess.DEVNULL)
    return rc


def main(argv):
    if len(argv) == 2 and argv[0] == "funcs-h":
        return funcs_h_only(pathlib.Path(argv[1]).resolve())
    if not argv or argv[0] != "generate":
        print("usage: srw1_generate.py generate --rom ROM [...]", file=sys.stderr)
        return 2
    ap = argparse.ArgumentParser(prog="srw1_generate.py generate")
    ap.add_argument("--rom", required=True)
    ap.add_argument("--json-progress", action="store_true")
    ap.add_argument("--expected-sha256")
    # Accepted for the host contract; the SRW1 layout is fixed.
    for opt in ("--cfg-dir", "--out-dir", "--funcs-h", "--project-root",
                "--expected-crc32", "--profile-manifest", "--host-alias-header"):
        ap.add_argument(opt)
    ap.add_argument("--cfg-roots", action="store_true")
    ap.add_argument("--no-host-root-scan", action="store_true")
    ap.add_argument("--source-root", action="append")
    a, _unknown = ap.parse_known_args(argv[1:])

    cfg_out = ROOT / ".build/recomp-srw1"
    try:
        merge_cfgs(cfg_out)
    except Exception as e:  # noqa: BLE001
        print("srw1_generate: cfg merge failed: %s" % e, file=sys.stderr)
        return 1
    cmd = [sys.executable, str(ROOT / "snesrecomp/snesrecomp_cli.py"), "generate",
           "--rom", os.path.abspath(a.rom),
           "--cfg-dir", str(cfg_out),
           "--out-dir", str(ROOT / "src/gen-srw1"),
           "--funcs-h", str(cfg_out / "funcs.h"),
           "--cfg-roots",
           "--expected-sha256", a.expected_sha256 or EXPECT_SHA256]
    for r in source_roots():
        cmd += ["--source-root", str(r)]
    if a.json_progress:
        cmd.append("--json-progress")
    sys.stdout.flush()
    rc = subprocess.call(cmd, cwd=str(ROOT))
    if rc != 0:
        return rc
    dst = ROOT / "recomp/srw1/funcs.h"
    shutil.copyfile(cfg_out / "funcs.h", dst)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
