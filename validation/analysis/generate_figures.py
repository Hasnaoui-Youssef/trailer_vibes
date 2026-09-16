#!/usr/bin/env python3
"""Phase 11: renders the campaign's figures from the CSVs in validation/analysis/
into vector PDFs under validation/figures/. Black/white/grey only, with a
restrained blue/red accent pair reserved for two-series plots (dataviz skill's
form/mark rules, per the user's explicit black-and-white-first direction) -
never used for bars or the fault-case-study diagram."""

import csv
import statistics
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

ENGINE_DIR = Path(__file__).resolve().parents[2]
ANALYSIS_DIR = ENGINE_DIR / "validation" / "analysis"
FIGURES_DIR = ENGINE_DIR / "validation" / "figures"

INK = "#0b0b0b"
GREY_DARK = "#4a4a4a"
GREY_MID = "#8a8a8a"
GREY_LIGHT = "#c8c8c8"
GRID = "#e1e0d9"
BLUE = "#2a78d6"
RED = "#c0392b"

WORKLOAD_LABEL = {
    "w1_baseline": "W1 baseline",
    "w2_callchain": "W2 call-chain",
    "w3_branchdense": "W3 branch-dense",
    "w4_isr": "W4 ISR",
    "w5_dma": "W5 DMA",
    "w6_overflow": "W6 overflow",
}
DENSITY_ORDER = ["w2_callchain", "w5_dma", "w4_isr", "w1_baseline", "w3_branchdense", "w6_overflow"]


def style():
    plt.rcParams.update({
        "font.size": 9,
        "axes.edgecolor": GREY_MID,
        "axes.labelcolor": INK,
        "text.color": INK,
        "xtick.color": GREY_DARK,
        "ytick.color": GREY_DARK,
        "axes.spines.top": False,
        "axes.spines.right": False,
        "axes.grid": True,
        "grid.color": GRID,
        "grid.linewidth": 0.8,
        "axes.axisbelow": True,
        "svg.fonttype": "none",
    })


def read_csv(name: str) -> list:
    with open(ANALYSIS_DIR / name, newline="") as f:
        return list(csv.DictReader(f))


def fig01_density_by_workload():
    rows = {r["workload"]: r for r in read_csv("density_summary.csv")}
    labels = [WORKLOAD_LABEL[w] for w in DENSITY_ORDER]
    means = [float(rows[w]["mean_instr_per_byte"]) for w in DENSITY_ORDER]
    stdevs = [float(rows[w]["stdev_instr_per_byte"]) for w in DENSITY_ORDER]
    n = int(rows[DENSITY_ORDER[0]]["n"])

    fig, ax = plt.subplots(figsize=(6.3, 3.4))
    x = range(len(labels))
    ax.bar(x, means, width=0.55, color=GREY_DARK, zorder=3)
    ax.errorbar(x, means, yerr=stdevs, fmt="none", ecolor=INK, elinewidth=1.0, capsize=3, zorder=4)
    for xi, m, s in zip(x, means, stdevs):
        ax.text(xi, m + s + 0.10, f"{m:.2f}", ha="center", va="bottom", fontsize=8, color=INK)

    ax.set_xticks(list(x))
    ax.set_xticklabels(labels, rotation=20, ha="right")
    ax.set_ylabel("instructions reconstructed / byte")
    ax.set_title(f"Instruction density by workload (mean ± stdev, n={n} captures each)")
    ax.set_ylim(0, max(m + s for m, s in zip(means, stdevs)) * 1.35)
    fig.tight_layout()
    fig.savefig(FIGURES_DIR / "fig01_density_by_workload.pdf")
    plt.close(fig)


def fig02_branch_density_scatter():
    rows = {r["workload"]: r for r in read_csv("density_summary.csv")}
    xs = [float(rows[w]["branch_density"]) for w in DENSITY_ORDER]
    ys = [float(rows[w]["mean_instr_per_byte"]) for w in DENSITY_ORDER]

    mx, my = statistics.mean(xs), statistics.mean(ys)
    cov = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    sx = sum((x - mx) ** 2 for x in xs) ** 0.5
    sy = sum((y - my) ** 2 for y in ys) ** 0.5
    r = cov / (sx * sy)

    fig, ax = plt.subplots(figsize=(5.2, 4.0))
    ax.scatter(xs, ys, s=42, color=INK, zorder=3)
    offsets = {
        "w1_baseline": (6, 4), "w2_callchain": (6, 4), "w3_branchdense": (6, -12),
        "w4_isr": (6, 4), "w5_dma": (6, 4), "w6_overflow": (8, -14),
    }
    for w, x, y in zip(DENSITY_ORDER, xs, ys):
        dx, dy = offsets[w]
        ax.annotate(WORKLOAD_LABEL[w], (x, y), textcoords="offset points", xytext=(dx, dy), fontsize=7.5)

    ax.set_xlabel("static branch density (branch-classified / total instructions)")
    ax.set_ylabel("mean instructions reconstructed / byte")
    ax.set_title("Branch density vs. instruction density (illustrative, N=6)")
    ax.text(0.98, 0.95, f"Pearson r = {r:.3f}\nnot an inferential regression",
             transform=ax.transAxes, ha="right", va="top", fontsize=7.5, color=GREY_DARK)
    ax.set_xlim(0.08, 0.30)
    ax.set_ylim(0.65, 3.05)
    fig.tight_layout()
    fig.savefig(FIGURES_DIR / "fig02_branch_density_vs_density.pdf")
    plt.close(fig)


def fig03_etm_overhead():
    rows = read_csv("etm_overhead_summary.csv")
    by_key = {(r["workload"], r["config"]): int(r["instructions_reconstructed"]) for r in rows}

    def pct(workload, config):
        base = by_key[(workload, "baseline")]
        val = by_key[(workload, config)]
        return (val - base) / base * 100.0

    labels = [WORKLOAD_LABEL[w] for w in DENSITY_ORDER]
    x = list(range(len(labels)))

    fig, (ax_a, ax_b) = plt.subplots(1, 2, figsize=(9.0, 3.4))

    retstack_pct = [pct(w, "retstack") for w in DENSITY_ORDER]
    ax_a.bar(x, retstack_pct, width=0.55, color=GREY_DARK,
             edgecolor=INK, linewidth=0.4, zorder=3)
    ax_a.axhline(0, color=GREY_MID, linewidth=0.8)
    ax_a.set_xticks(x)
    ax_a.set_xticklabels(labels, rotation=20, ha="right")
    ax_a.set_ylabel("instructions reconstructed,\nchange vs. baseline (%)")
    ax_a.set_ylim(-6, 2)
    ax_a.set_title("(a) +retstack", fontsize=9)
    w4_idx = DENSITY_ORDER.index("w4_isr")
    ax_a.annotate(f"{retstack_pct[w4_idx]:.1f}%", (w4_idx, retstack_pct[w4_idx]),
                  textcoords="offset points", xytext=(0, -12), ha="center", fontsize=7.5)

    ts_pct = [pct(w, "timestamp_cyclecount") for w in DENSITY_ORDER]
    ax_b.bar(x, ts_pct, width=0.55, color=GREY_DARK, zorder=3)
    for xi, v in zip(x, ts_pct):
        ax_b.text(xi, -6, f"{v:.1f}%", ha="center", va="top", fontsize=7.5, color="white")
    ax_b.set_xticks(x)
    ax_b.set_xticklabels(labels, rotation=20, ha="right")
    ax_b.set_ylim(-108, 5)
    ax_b.set_ylabel("instructions reconstructed,\nchange vs. baseline (%)")
    ax_b.set_title("(b) +timestamp+cyclecount", fontsize=9)

    fig.suptitle("ETM configuration effect on instructions decoded from a fixed 2064-byte buffer", fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, 0.94))
    fig.savefig(FIGURES_DIR / "fig03_etm_overhead.pdf")
    plt.close(fig)


def fig04_pipeline_scaling():
    rows = read_csv("pipeline_metrics.csv")
    instr = [int(r["instructionsReconstructed"]) for r in rows]
    decode_ms = [float(r["decodeMs"]) for r in rows]
    xform_instr_ms = [float(r["transformInstructionsMs"]) for r in rows]
    xform_blocks_ms = [float(r["transformBlocksMs"]) for r in rows]

    fig, (ax_a, ax_b) = plt.subplots(1, 2, figsize=(9.0, 3.6))

    ax_a.scatter(instr, decode_ms, s=24, color=INK, zorder=3)
    ax_a.set_xlabel("instructions reconstructed")
    ax_a.set_ylabel("decode time (ms)")
    ax_a.set_title("(a) OpenCSD decode", fontsize=9)

    ax_b.scatter(instr, xform_instr_ms, s=24, color=BLUE, zorder=3, label="instruction reconstruction")
    ax_b.scatter(instr, xform_blocks_ms, s=24, color=RED, zorder=3, label="function-block transform")
    ax_b.set_xlabel("instructions reconstructed")
    ax_b.set_ylabel("transform time (ms)")
    ax_b.legend(frameon=False, fontsize=7.5, loc="upper left")
    ax_b.set_title("(b) offline transforms", fontsize=9)

    fig.suptitle(f"Pipeline timing vs. instruction count (n={len(rows)} density-matrix captures, "
                 f"fixed 2064-byte input)", fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, 0.92))
    fig.savefig(FIGURES_DIR / "fig04_pipeline_scaling.pdf")
    plt.close(fig)


def fig05_fault_case_study():
    fig, (ax_a, ax_b) = plt.subplots(1, 2, figsize=(9.0, 4.2))
    for ax in (ax_a, ax_b):
        ax.axis("off")
        ax.set_xlim(0, 1)
        ax.set_ylim(0, 1)

    ax_a.add_patch(mpatches.FancyBboxPatch((0.03, 0.03), 0.94, 0.94, boxstyle="round,pad=0.02",
                                            linewidth=1.0, edgecolor=GREY_MID, facecolor="none"))
    ax_a.text(0.5, 0.92, "State at halt alone", ha="center", fontsize=10, weight="bold")
    halt_lines = [
        "CFSR  = 0x00020000  (UFSR bit 1 = INVSTATE)",
        "HFSR  = 0x40000000  (FORCED)",
        "ICSR & 0x1FF = 3    (VECTACTIVE = HardFault)",
        "",
        "record.on_complete = 0x00000000",
        "",
        "Fault type: legible.",
        "Corrupted pointer's real value: not legible",
        "(D-cache write-back masks it - see below).",
        "Which instruction wrote it: not visible.",
    ]
    for i, line in enumerate(halt_lines):
        ax_a.text(0.08, 0.80 - i * 0.075, line, fontsize=8.3, family="monospace", color=INK)

    ax_b.add_patch(mpatches.FancyBboxPatch((0.03, 0.03), 0.94, 0.94, boxstyle="round,pad=0.02",
                                            linewidth=1.0, edgecolor=GREY_MID, facecolor="none"))
    ax_b.text(0.5, 0.92, "With instruction trace", ha="center", fontsize=10, weight="bold")
    trace_lines = [
        "main.c:38  Process() copy loop",
        "main.c:40      (repeated)",
        "main.c:42  Process() returns",
        "main.c:58  record.on_complete();  <- fault here",
        "",
        "Process() is directly implicated: it is the",
        "only code that ran between a known-good",
        "assignment and the faulting call.",
    ]
    for i, line in enumerate(trace_lines):
        weight = "bold" if "fault here" in line else "normal"
        ax_b.text(0.08, 0.80 - i * 0.075, line, fontsize=8.3, family="monospace", color=INK, weight=weight)

    fig.suptitle("Finding 07 — fault root-cause: state at halt vs. instruction trace", fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, 0.94))
    fig.savefig(FIGURES_DIR / "fig05_fault_case_study.pdf")
    plt.close(fig)


def main() -> int:
    style()
    FIGURES_DIR.mkdir(parents=True, exist_ok=True)
    fig01_density_by_workload()
    fig02_branch_density_scatter()
    fig03_etm_overhead()
    fig04_pipeline_scaling()
    fig05_fault_case_study()
    print(f"wrote 5 figures to {FIGURES_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
