#!/usr/bin/env python3

from pathlib import Path
from shutil import which

import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch


def main():
    base = Path(__file__).resolve().parent
    use_tex = which("latex") is not None

    if use_tex:
        plt.rcParams.update({
            "text.usetex": True,
            "font.family": "serif",
            "text.latex.preamble": r"\usepackage[T1]{fontenc}",
        })
    else:
        plt.rcParams.update({
            "font.family": "serif",
        })

    series = [
        ("ReSubsample Online", "#1f77b4"),
        ("ReSubsample Offline", "#aec7e8"),
        ("OMBSusSample Online", "#d62728"),
        ("OMBSubsample Offline", "#ff9896"),
    ]

    n = len(series)

    fig, ax = plt.subplots(figsize=(0.7, 1.8), dpi=300)
    ax.set_xlim(0, 1.0)
    gap = 0.8
    top = n - 0.5
    ys = [top - i * gap for i in range(n)]
    ax.set_ylim(0, top + 0.23)
    ax.axis("off")

    def format_label(text: str) -> str:
        if text.endswith(" Online"):
            alg_name, mode = text[:-7], "Online"
        elif text.endswith(" Offline"):
            alg_name, mode = text[:-8], "Offline"
        else:
            alg_name, mode = text, ""

        if not mode:
            return rf"\textsc{{{alg_name}}}" if use_tex else alg_name

        if use_tex:
            return (
                r"$\begin{array}{c}"
                + rf"\textsc{{{alg_name}}}"
                + r"\\[-2pt]"
                + rf"\mathrm{{{mode}}}"
                + r"\end{array}$"
            )
        else:
            return f"{alg_name}\n{mode}"

    sw_w = 1.0
    sw_h = 0.30

    for y, (label, color) in zip(ys, series):
        swatch = FancyBboxPatch(
            (0.5 - sw_w / 2, y - sw_h / 2),
            sw_w,
            sw_h,
            boxstyle="round,pad=0.0,rounding_size=0.00",
            linewidth=0.0,
            edgecolor="none",
            facecolor=color,
            zorder=2,
        )
        ax.add_patch(swatch)

        ax.text(
            0.50,
            y - 0.34,
            format_label(label),
            ha="center",
            va="center",
            multialignment="center",
            fontsize=6,
        )

    fig.subplots_adjust(left=0.01, right=0.99, bottom=0.01, top=0.99)

    fig.savefig(base / "legend_com.pdf", bbox_inches="tight", pad_inches=0.01, transparent=False)
    fig.savefig(base / "legend_com.png", dpi=600, bbox_inches="tight", pad_inches=0.01, transparent=False)
    print(f"Saved: {base / 'legend_com.pdf'}")
    print(f"Saved: {base / 'legend_com.png'}")


if __name__ == "__main__":
    main()