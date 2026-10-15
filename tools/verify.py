#!/usr/bin/env python3
"""
DtBlkFx port verification: generates test WAVs, runs them through DtBlkFxHarness with several
parameter sets and checks the results (finite output, null against the delayed input, effects
visibly changing the spectrum).

usage: python3 -I tools/verify.py [build dir]
       python3 -I tools/verify.py --write-fixtures   (rewrite tests/fixtures/*.fxp|fxb)

DTBLKFX_HARNESS_ARCH=x86_64 runs the harness through "arch -x86_64" (Rosetta on Apple Silicon), so
the whole check list runs on the Intel slice of a universal build. On an arm64 Mac with a universal
harness, section 8 also runs every factory program on both slices and compares the outputs.
"""
import os
import struct
import subprocess
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import correlate

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WRITE_FIXTURES = "--write-fixtures" in sys.argv
_args = [a for a in sys.argv[1:] if not a.startswith("--")]
BUILD = _args[0] if _args else os.path.join(ROOT, "build")
FIXTURES = os.path.join(ROOT, "tests", "fixtures")
HARNESS = os.path.join(BUILD, "DtBlkFxHarness_artefacts", "Release", "DtBlkFxHarness")
ARCH = os.environ.get("DTBLKFX_HARNESS_ARCH", "")
RUN = (["arch", "-" + ARCH] if ARCH else []) + [HARNESS]
OUT = os.path.join(BUILD, "verify" + ("-" + ARCH if ARCH else ""))
SR = 44100
os.makedirs(OUT, exist_ok=True)


def write(name, x):
    path = os.path.join(OUT, name)
    wavfile.write(path, SR, x.astype(np.float32))
    return path


def gen():
    t = np.arange(int(SR * 6.0)) / SR
    # log sine sweep 30 Hz -> 18 kHz, slightly different phase per channel
    f0, f1, T = 30.0, 18000.0, t[-1]
    k = np.log(f1 / f0)
    ph = 2 * np.pi * f0 * T / k * (np.exp(t / T * k) - 1)
    sweep = 0.5 * np.stack([np.sin(ph), np.sin(ph + 0.3)], axis=1)
    rng = np.random.default_rng(7)
    noise = 0.25 * rng.standard_normal((len(t), 2))
    # C major chord, each note with a few harmonics
    chord = np.zeros(len(t))
    for f in (261.63, 329.63, 392.0):
        for h in range(1, 6):
            chord += (0.12 / h) * np.sin(2 * np.pi * f * h * t)
    chord = np.stack([chord, chord], axis=1)
    return {"sweep": write("sweep.wav", sweep), "noise": write("noise.wav", noise), "chord": write("chord.wav", chord)}


def run(src, dst, *opts):
    r = subprocess.run([*RUN, "process", src, dst, *opts], capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout, r.stderr)
        raise SystemExit(f"harness failed ({r.returncode})")
    sr, y = wavfile.read(dst)
    return y.astype(np.float64), r.stdout.strip()


def best_lag(x, y, max_lag=SR):
    c = correlate(y[:, 0], x[:, 0], mode="full", method="fft")
    mid = len(x) - 1
    c = c[mid: mid + max_lag]
    return int(np.argmax(np.abs(c)))


def null_db(x, y, lag, skip=SR):
    """residual of y against x delayed by lag, over the steady part (skip the first second)"""
    n = len(x)
    xs = np.zeros_like(x)
    xs[lag:] = x[: n - lag]
    a, b = xs[skip + lag: n - SR // 4], y[skip + lag: n - SR // 4]
    res = np.sqrt(np.mean((b - a) ** 2))
    ref = np.sqrt(np.mean(a ** 2))
    return 20 * np.log10(max(res, 1e-30) / ref)


def band_db(y, lo, hi, skip=SR):
    seg = y[skip:, 0]
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    f = np.fft.rfftfreq(len(seg), 1 / SR)
    m = (f >= lo) & (f < hi)
    return 10 * np.log10(np.sum(spec[m]) + 1e-30)


def spectral_change_db(x, y, lag, skip=SR):
    """RMS difference in dB between the 1/3-octave spectra of the (delayed) input and output"""
    n = len(x)
    xs = np.zeros_like(x)
    xs[lag:] = x[: n - lag]
    edges = 31.25 * 2 ** (np.arange(0, 29) / 3.0)
    dx = np.array([band_db(xs, edges[i], edges[i + 1], skip) for i in range(len(edges) - 1)])
    dy = np.array([band_db(y, edges[i], edges[i + 1], skip) for i in range(len(edges) - 1)])
    m = dx > dx.max() - 50  # only bands where the input has energy
    return float(np.sqrt(np.mean((dy[m] - dx[m]) ** 2))), dx, dy, edges


def check_basic(name, y, stdout):
    finite = np.all(np.isfinite(y))
    rms = np.sqrt(np.mean(y[SR:] ** 2))
    print(f"  [{name}] finite={finite} rms_dBFS={20*np.log10(max(rms,1e-30)):.1f}  ({stdout.splitlines()[-1]})")
    return finite and rms > 1e-4


# --- VST2 preset files (.fxp / .fxb) ----------------------------------------------------------------
# Written here straight from the VST 2.4 SDK layout (vstfxstore.h), independently of the C++ code,
# so loading them tests the port against the spec rather than against itself.
FXID = b"h526"        # original registered unique ID (stereo build)
FXVERSION = 1200      # original getVendorVersion()
NPARAMS = 44
CHUNK_TAG = 0x99887766


def name_bytes(name, n):
    b = name.encode("ascii")[: n - 1]
    return b + b"\0" * (n - len(b))


def be_floats(vals):
    return b"".join(struct.pack(">f", v) for v in vals)


def fx_program_params(name, vals):
    """'FxCk': parameter list program"""
    body = b"FxCk" + struct.pack(">i", 1) + FXID + struct.pack(">ii", FXVERSION, len(vals)) + name_bytes(name, 28) + be_floats(vals)
    return b"CcnK" + struct.pack(">i", len(body)) + body


def engine_chunk(curr_name, curr_vals, programs, curr_prog):
    """DtBlkFx::getChunk layout: LE tag, version 101, num programs, current program, then 32-byte
    name + 44 LE floats for the current settings and for each program"""
    out = struct.pack("<Iiii", CHUNK_TAG, 101, len(programs), curr_prog)
    for nm, vals in [(curr_name, curr_vals)] + programs:
        out += name_bytes(nm, 32) + b"".join(struct.pack("<f", v) for v in vals)
    return out


def fx_program_chunk(name, chunk):
    body = b"FPCh" + struct.pack(">i", 1) + FXID + struct.pack(">ii", FXVERSION, NPARAMS) + name_bytes(name, 28)
    body += struct.pack(">i", len(chunk)) + chunk
    return b"CcnK" + struct.pack(">i", len(body)) + body


def fx_bank_header(magic, n_programs, curr, version=2):
    h = magic + struct.pack(">i", version) + FXID + struct.pack(">ii", FXVERSION, n_programs)
    return h + struct.pack(">i", curr if version >= 2 else 0) + b"\0" * 124


def fx_bank_params(programs, curr):
    body = fx_bank_header(b"FxBk", len(programs), curr) + b"".join(fx_program_params(n, v) for n, v in programs)
    return b"CcnK" + struct.pack(">i", len(body)) + body


def fx_bank_chunk(chunk, n_programs, curr):
    body = fx_bank_header(b"FBCh", n_programs, curr) + struct.pack(">i", len(chunk)) + chunk
    return b"CcnK" + struct.pack(">i", len(body)) + body


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def fixture_values(seed):
    rng = np.random.default_rng(seed)
    return [f32(v) for v in rng.uniform(0.0, 1.0, NPARAMS)]


def make_fixtures():
    """name -> (bytes, expected current program, expected params, expected names {index: name})"""
    fx = {}
    v = fixture_values(1)
    fx["fixture_params.fxp"] = (fx_program_params("Fixture FxCk", v), 0, v, {0: "Fixture FxCk"})
    v = fixture_values(2)
    chunk = engine_chunk("Fixture FPCh", v, [], 0)
    fx["fixture_chunk.fxp"] = (fx_program_chunk("Fixture FPCh", chunk), 0, v, {0: "Fixture FPCh"})
    progs = [(f"FxBk prog {i}", fixture_values(100 + i)) for i in range(44)]
    names = {i: progs[i][0] for i in range(43)}  # slot 43 is "> reset current <" and is never overwritten
    names[43] = "> reset current <"
    fx["fixture_params.fxb"] = (fx_bank_params(progs, 2), 2, progs[2][1], names)
    progs = [(f"FBCh prog {i}", fixture_values(200 + i)) for i in range(44)]
    curr = fixture_values(3)
    chunk = engine_chunk("3", curr, progs, 3)
    names = {i: progs[i][0] for i in range(43)}
    names[43] = "> reset current <"
    fx["fixture_chunk.fxb"] = (fx_bank_chunk(chunk, 44, 3), 3, curr, names)
    return fx


def parse_harness_state(stdout):
    cur, params, names = None, None, {}
    for line in stdout.splitlines():
        if line.startswith("current "):
            cur = int(line.split()[1])
        elif line.startswith("params "):
            params = [f32(float(x)) for x in line.split()[1:]]  # %.9g text -> exact float32
        elif line.startswith("program "):
            i, _, nm = line[len("program "):].partition("\t")
            names[int(i)] = nm
    return cur, params, names


def harness(*a):
    r = subprocess.run([*RUN, *a], capture_output=True, text=True)
    return r.returncode, r.stdout


def parse_fx_file(b):
    """returns a dict of the big-endian header fields and the chunk (chunk forms only)"""
    d = {"magic": b[0:4], "byteSize": struct.unpack(">i", b[4:8])[0], "fxMagic": b[8:12],
         "version": struct.unpack(">i", b[12:16])[0], "fxID": b[16:20], "fxVersion": struct.unpack(">i", b[20:24])[0],
         "count": struct.unpack(">i", b[24:28])[0]}
    if d["fxMagic"] == b"FPCh":
        d["name"] = b[28:56].split(b"\0")[0].decode()
        off = 56
    else:
        d["currentProgram"] = struct.unpack(">i", b[28:32])[0]
        off = 156
    n = struct.unpack(">i", b[off: off + 4])[0]
    d["chunk"] = b[off + 4: off + 4 + n]
    d["chunkSize"] = n
    return d


def check_presets():
    ok = True
    pdir = os.path.join(OUT, "presets")
    os.makedirs(pdir, exist_ok=True)
    fixtures = make_fixtures()
    print("6) VST2 presets: load spec-exact .fxp/.fxb fixtures (FxCk, FPCh, FxBk, FBCh), by file and by drop")
    for name, (data, exp_cur, exp_params, exp_names) in fixtures.items():
        committed = os.path.join(FIXTURES, name)
        if os.path.exists(committed):
            same = open(committed, "rb").read() == data
            ok &= same
            if not same:
                print(f"  tests/fixtures/{name} differs from the generator (rerun with --write-fixtures?)")
        path = os.path.join(pdir, name)
        open(path, "wb").write(data)
        for how in ([], ["--drop"]):
            rc, so = harness("preset-load", path, *how)
            cur, params, names = parse_harness_state(so)
            res = next((l for l in so.splitlines() if l.startswith("result")), "")
            perr = max(abs(a - b) for a, b in zip(params, exp_params)) if params else float("nan")
            name_ok = all(names.get(i) == n for i, n in exp_names.items())
            good = rc == 0 and cur == exp_cur and perr == 0.0 and name_ok
            ok &= good
            print(f"  {'ok  ' if good else 'FAIL'} {name:20s} {'drop' if how else 'file'}: {res[7:]} | current={cur} "
                  f"max|param error|={perr:.1e} names={'ok' if name_ok else 'WRONG'}")

    print("7) save and round trip: .fxp/.fxb written by the port, header checked against the spec, reloaded")
    for src, out in (("fixture_chunk.fxp", "rt.fxp"), ("fixture_params.fxp", "rt2.fxp"),
                     ("fixture_chunk.fxb", "rt.fxb"), ("fixture_params.fxb", "rt2.fxb")):
        _, exp_cur, exp_params, exp_names = fixtures[src]
        dst = os.path.join(pdir, out)
        rc, so = harness("preset-save", dst, "--load", os.path.join(pdir, src))
        b = open(dst, "rb").read()
        h = parse_fx_file(b)
        bank = out.endswith(".fxb")
        hdr_ok = (h["magic"] == b"CcnK" and h["byteSize"] == len(b) - 8 and h["fxID"] == FXID
                  and h["fxVersion"] == FXVERSION and h["fxMagic"] == (b"FBCh" if bank else b"FPCh")
                  and h["version"] == (2 if bank else 1) and h["count"] == 44)
        tag, vers, nprog, cprog = struct.unpack("<Iiii", h["chunk"][:16])
        exp_len = 16 + (32 + 4 * 44) * (1 + (44 if bank else 0))
        chunk_ok = tag == CHUNK_TAG and vers == 101 and nprog == (44 if bank else 0) and h["chunkSize"] == exp_len
        if bank:
            chunk_ok &= cprog == exp_cur and h["currentProgram"] == exp_cur
        else:
            chunk_ok &= cprog == 0 and h["name"] == exp_names[0]
        # the chunk's current settings are exactly the loaded values
        cvals = list(struct.unpack("<44f", h["chunk"][16 + 32: 16 + 32 + 176]))
        vals_ok = cvals == exp_params
        rc2, so2 = harness("preset-load", dst)
        cur, params, names = parse_harness_state(so2)
        rt_ok = rc == 0 and rc2 == 0 and params == exp_params and cur == exp_cur and all(names.get(i) == n for i, n in exp_names.items())
        # byte comparison with the fixture's chunk (both are getChunk layout, zero padded names). Only
        # for the .fxp: in a bank the original names the current settings after the current program,
        # while the fixture wrote "3" there, so those 32 bytes differ by design
        same_chunk = h["chunk"] == parse_fx_file(fixtures[src][0])["chunk"] if src == "fixture_chunk.fxp" else None
        good = hdr_ok and chunk_ok and vals_ok and rt_ok
        ok &= good
        print(f"  {'ok  ' if good else 'FAIL'} {src} -> {out}: {len(b)} bytes, header={'ok' if hdr_ok else 'BAD'} "
              f"chunk={'ok' if chunk_ok else 'BAD'} values={'exact' if vals_ok else 'DIFF'} reload={'ok' if rt_ok else 'BAD'}"
              + ("" if same_chunk is None else f" chunk bytes identical to fixture={same_chunk}"))

    # a preset for another plug-in and a truncated file are refused without touching anything
    other = bytearray(fixtures["fixture_chunk.fxp"][0])
    other[16:20] = b"XXXX"
    bad = os.path.join(pdir, "other_plugin.fxp")
    open(bad, "wb").write(other)
    rc, so = harness("preset-load", bad)
    trunc = os.path.join(pdir, "truncated.fxb")
    open(trunc, "wb").write(fixtures["fixture_chunk.fxb"][0][:300])
    rc2, so2 = harness("preset-load", trunc)
    good = rc != 0 and rc2 != 0
    ok &= good
    print(f"  {'ok  ' if good else 'FAIL'} refused: other plug-in ID ({so.splitlines()[0][7:]}) and truncated bank ({so2.splitlines()[0][7:]})")
    return ok


def check_arches(w):
    """every factory program (plus the dry and 0 dB paths) on the arm64 and x86_64 slices"""
    archs = subprocess.run(["lipo", "-archs", HARNESS], capture_output=True, text=True).stdout.split()
    if ARCH or os.uname().machine != "arm64" or "x86_64" not in archs:
        print("8) arm64 vs x86_64: skipped (needs a universal harness on an arm64 Mac)")
        return True
    if subprocess.run(["arch", "-x86_64", "/usr/bin/true"]).returncode != 0:
        print("8) arm64 vs x86_64: skipped (Rosetta not installed)")
        return True
    print("8) arm64 vs x86_64 (Rosetta): same input and settings on both slices of the universal harness")
    ok = True
    worst = 0.0
    worst_rel = -999.0
    cases = [(f"program {p:2d}", ["--program", str(p)]) for p in range(44)]
    cases.insert(0, ("dry 100%", ["--set", "0=0.5"]))
    for name, opts in cases:
        ys = []
        for a in ("arm64", "x86_64"):
            dst = os.path.join(OUT, f"arch_{a}.wav")
            r = subprocess.run(["arch", "-" + a, HARNESS, "process", w["chord"], dst, *opts], capture_output=True, text=True)
            ok &= r.returncode == 0
            ys.append(wavfile.read(dst)[1].astype(np.float64))
        d = float(np.max(np.abs(ys[0] - ys[1])))
        rms = np.sqrt(np.mean(ys[0] ** 2))
        rel = 20 * np.log10(max(np.sqrt(np.mean((ys[0] - ys[1]) ** 2)), 1e-30) / max(rms, 1e-30))
        worst, worst_rel = max(worst, d), max(worst_rel, rel)
        fin = bool(np.all(np.isfinite(ys[1])))
        ok &= fin
        print(f"  {name}: max|arm64 - x86_64|={d:.2e}  difference rms re output={rel:7.1f} dB  x86_64 finite={fin}")
    print(f"  worst max|diff|={worst:.2e}, worst difference rms={worst_rel:.1f} dB re output")
    return ok


def check_editor():
    """simulated mouse on the editor, and the Fine / Coarse popup sliders at 1x, 1.5x and 2x"""
    ok = True
    print("9) editor: simulated drags, clicks and wheel set the right parameters")
    rc, so = harness("ui")
    for line in so.splitlines():
        print("  " + line.strip())
    ok &= rc == 0 and "UI OK" in so
    popdir = os.path.join(OUT, "popup")
    print("10) popup sliders: 7 cells (top row, effect rows 1, 4 and 8, first and last columns) at 1x, 1.5x, 2x:")
    print("    inside the editor, lined up with the cell, clear of the clicked value, dragging past the")
    print("    edges keeps them inside and still reaches 0 and 1, no jump switching bars, and after closing")
    print("    the snapshot is pixel-identical to the one before opening (snapshots in " + os.path.relpath(popdir, ROOT) + ")")
    rc, so = harness("popup", popdir)
    for line in so.splitlines():
        if "FAIL" in line or "@1x" in line or "POPUP" in line:
            print("  " + line.strip())
    ok &= rc == 0 and "POPUP OK" in so
    return ok


def main():
    if WRITE_FIXTURES:
        os.makedirs(FIXTURES, exist_ok=True)
        for name, (data, *_rest) in make_fixtures().items():
            open(os.path.join(FIXTURES, name), "wb").write(data)
            print("wrote", os.path.join("tests", "fixtures", name), len(data), "bytes")
        return 0
    if not os.path.exists(HARNESS):
        raise SystemExit("build DtBlkFxHarness first")
    w = gen()
    ok = True
    sr_x = {k: wavfile.read(v)[1].astype(np.float64) for k, v in w.items()}

    # Delay param: >= 0.5 is msec (0..6000 ms over 0.5..1.0). 100 ms = 4410 samples.
    delay_100ms = 0.5 + (100.0 / 6000.0) * 0.5
    print("1) dry null: Mixbk = 100% original (param 0.5, power match), Delay = 100 ms")
    for key in ("sweep", "noise", "chord"):
        for blocks in (["--block", "512"], ["--random-blocks"]):
            y, so = run(w[key], os.path.join(OUT, f"dry_{key}.wav"), "--set", "0=0.5", "--set", f"1={delay_100ms}", *blocks)
            ok &= check_basic(f"dry {key} {' '.join(blocks)}", y, so)
            lag = best_lag(sr_x[key], y)
            nd = null_db(sr_x[key], y, lag)
            # 100 ms as a 0..1 float lands on 99.99.. ms, which the engine truncates to 4409
            print(f"      lag={lag} samples (expected 4409), null={nd:.1f} dB")
            ok &= lag in (4409, 4410) and nd < -90

    print("2) wet, neutral: program 0 'user' (8 x Filter at -inf over 0..0 Hz = DC only), Mixbk 0%, Delay 100 ms")
    for key in ("sweep", "noise", "chord"):
        y, so = run(w[key], os.path.join(OUT, f"wet_{key}.wav"), "--program", "0", "--set", f"1={delay_100ms}")
        ok &= check_basic(f"wet {key}", y, so)
        lag = best_lag(sr_x[key], y)
        print(f"      lag={lag} samples, null vs delayed input={null_db(sr_x[key], y, lag):.1f} dB (FFT resynthesis, DC bin removed)")

    print("2b) wet, every slot Filter at 0 dB (amp param 0.6) = FFT -> IFFT -> block crossfade only")
    neutral = []
    for slot in range(8):
        neutral += ["--set", f"{4 + slot * 5 + 2}=0.6"]
    for key in ("sweep", "noise", "chord"):
        y, so = run(w[key], os.path.join(OUT, f"wet0db_{key}.wav"), "--program", "0", "--set", f"1={delay_100ms}", *neutral)
        ok &= check_basic(f"wet 0dB {key}", y, so)
        lag = best_lag(sr_x[key], y)
        nd = null_db(sr_x[key], y, lag)
        print(f"      lag={lag} samples, null vs delayed input={nd:.1f} dB")
        ok &= nd < -100

    print("3) effects change the spectrum (noise in, slot 0 only, Mixbk 0%, Delay 100 ms)")

    def p_freq(hz):
        # inverse of BlkFxParam::getHzNonZero: param * 127.5 notes above C0 (16.3516 Hz)
        return float(np.clip(12 * np.log2(hz / 16.3516) / 127.5, 0, 1))

    amp0 = 0.6  # 0 dB
    tests = {
        # Filter: -60 dB (amp 0) between 1 kHz and 4 kHz
        "filter_notch_1k_4k": ["--fx", "0:0", "--set", f"4={p_freq(1000)}", "--set", f"5={p_freq(4000)}", "--set", "6=0.0"],
        # Contrast on the full range, strong value
        "contrast": ["--fx", "0:1", "--set", "4=0", "--set", "5=1", "--set", f"6={amp0}", "--set", "8=0.9"],
        # HarmShift up a few notes over the full range
        "harmshift": ["--fx", "0:11", "--set", "4=0", "--set", "5=1", "--set", f"6={amp0}", "--set", "8=0.7"],
        # Shift (frequency shift) over the full range
        "shift": ["--fx", "0:7", "--set", "4=0", "--set", "5=1", "--set", f"6={amp0}", "--set", "8=0.8"],
        # Smear (phase randomise)
        "smear": ["--fx", "0:2", "--set", "4=0", "--set", "5=1", "--set", f"6={amp0}", "--set", "8=1.0"],
    }
    base = ["--set", "0=0", "--set", f"1={delay_100ms}"]
    # neutral other slots: amp at -inf over 0..0 Hz (same as program 0)
    for key, src in (("noise", w["noise"]), ("chord", w["chord"])):
        for name, opts in tests.items():
            y, so = run(src, os.path.join(OUT, f"fx_{name}_{key}.wav"), "--program", "0", *base, *opts)
            ok &= check_basic(f"{name} {key}", y, so)
            ch, dx, dy, edges = spectral_change_db(sr_x[key], y, 4409)
            extra = ""
            if name.startswith("filter"):
                i1k = np.argmin(np.abs(edges - 1600))
                i100 = np.argmin(np.abs(edges - 250))
                extra = f" | band@1.6k in={dx[i1k]:.1f} out={dy[i1k]:.1f} dB, band@250 in={dx[i100]:.1f} out={dy[i100]:.1f} dB"
                if key == "noise":
                    ok &= (dx[i1k] - dy[i1k]) > 30 and abs(dx[i100] - dy[i100]) < 3
            print(f"      1/3-oct spectral change vs input: {ch:.1f} dB rms{extra}")

    print("4) factory programs: every program on chord + noise, with 512-sample blocks and with random")
    print("   1..2048-sample blocks (finite and non-silent; the engine's block timing depends on host")
    print("   block boundaries, as the VST2 did, so the two runs can differ)")
    for prog in range(0, 44):
        for key in ("chord", "noise"):
            y, so = run(w[key], os.path.join(OUT, "prog.wav"), "--program", str(prog))
            y2, _ = run(w[key], os.path.join(OUT, "prog2.wav"), "--program", str(prog), "--random-blocks")
            fin = bool(np.all(np.isfinite(y)) and np.all(np.isfinite(y2)))
            rms = np.sqrt(np.mean(y[SR:] ** 2))
            same = np.max(np.abs(y - y2)) if fin else float("nan")
            ok &= fin
            flag = "" if (fin and rms > 1e-4) else "  <-- check"
            print(f"  program {prog:2d} {key}: finite={fin} rms_dBFS={20*np.log10(max(rms,1e-30)):6.1f} "
                  f"max|fixed-block - random-block|={same:.2e}{flag}")

    print("5) run-to-run determinism: program 39 (WarpMix) on the chord, 5 runs, identical output")
    ref = None
    for i in range(5):
        y, _ = run(w["chord"], os.path.join(OUT, "det.wav"), "--program", "39")
        if ref is None:
            ref = y
        same = bool(np.all(np.isfinite(y)) and np.array_equal(y, ref))
        ok &= same
    print(f"   identical: {same}")

    ok &= check_presets()
    ok &= check_arches(w)
    ok &= check_editor()

    print("ALL OK" if ok else "SOME CHECKS FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
