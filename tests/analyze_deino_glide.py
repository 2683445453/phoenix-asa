"""Summarize a client glide capture; never label discontinuities as proven corrections."""
import csv
import json
import math
import sys
from pathlib import Path


def summarize(path):
    with Path(path).open(encoding="utf-8") as source:
        lines = source.readlines()
    rows = list(csv.DictReader(line for line in lines if not line.startswith("#")))
    if not rows:
        return {"file": str(path), "samples": 0, "notes": "No samples; inspect end reason."}
    result = {"file": str(path), "samples": len(rows), "duration_s": float(rows[-1]["elapsed_ms"]) / 1000,
              "end": next((line.strip() for line in reversed(lines) if line.startswith("# end=")), "missing_end_marker"),
              "max_sample_gap_ms": 0, "input_edges": [], "state_edges": [], "trajectory_residuals": [],
              "limits": "Client observations only. Residuals can include collision, camera tick timing, attachment, origin shifts or correction. No server acceptance is inferred."}
    for prev, cur in zip(rows, rows[1:]):
        gap = float(cur["elapsed_ms"]) - float(prev["elapsed_ms"])
        result["max_sample_gap_ms"] = max(result["max_sample_gap_ms"], gap)
        t = float(cur["elapsed_ms"]) / 1000
        if cur["space"] != prev["space"]:
            result["input_edges"].append({"t": t, "space": cur["space"], "held_jump": cur["held_jump"],
                                          "mode": cur["mode"], "vz_m_s": float(cur["vz"]) / 100})
        keys = ("mode", "custom_mode", "held_jump", "slow_falling", "latch_state", "latched_jump_requested")
        changed = {k: [prev[k], cur[k]] for k in keys if prev[k] != cur[k]}
        if changed:
            result["state_edges"].append({"t": t, "changes": changed})
        if 0 < gap <= 200:
            dt = gap / 1000
            residual = math.sqrt(sum((float(cur[p]) - float(prev[p]) -
                                     .5 * (float(prev[v]) + float(cur[v])) * dt) ** 2
                                    for p, v in (("x", "vx"), ("y", "vy"), ("z", "vz")))) / 100
            if math.isfinite(residual) and residual > 1:
                result["trajectory_residuals"].append({"t": t, "residual_m": round(residual, 3)})
    return result


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("Usage: python tests/analyze_deino_glide.py capture.csv [capture.csv ...]")
    print(json.dumps([summarize(path) for path in sys.argv[1:]], ensure_ascii=False, indent=2))
