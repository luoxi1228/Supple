#!/usr/bin/env python3

from pathlib import Path
import matplotlib.pyplot as plt


def main():
    base = Path(__file__).resolve().parent
    use_tex = True

    if use_tex:
        plt.rcParams.update({
            "text.usetex": True,
            "font.family": "serif",
            "text.latex.preamble": r"\usepackage[T1]{fontenc}",
        })

    series = [
        ("S&T", "#1f77b4", "^", 3),
        ("PSQF", "#2ca02c", "s", 3),
        ("OSBSubsample", "#ff7f0e", "v", 3),
        ("ReSubsample", "#9467bd", "D", 3),
        ("OMBSubsample", "#d62728", "*", 5),
    ]

    n = len(series)

    fig, ax = plt.subplots(figsize=(0.7, 4.5), dpi=300)
    ax.set_xlim(0, 1.0)
    ax.axis("off")

    gap = 0.8
    top = n - 0.15
    ys = [top - i * gap for i in range(n)]
    ax.set_ylim(0, top + 0.9)

    def format_label(text: str) -> str:
        if not use_tex:
            return text
        if text == "S&T":
            return r"S\&T"
        if text == "PSQF":
            return r"PSQF"
        return rf"\textsc{{{text.replace('&', r'\&')}}}"

    for y, (label, color, marker, marker_size) in zip(ys, series):
        ax.plot(
            [0.1, 0.9],
            [y, y],
            color=color,
            linewidth=1.0,
        )
        ax.plot(
            0.50,
            y,
            marker=marker,
            color=color,
            markersize=marker_size,
            markeredgewidth=1.0,
            linestyle="None",
        )
        ax.text(
            0.50,
            y - 0.15,
            format_label(label),
            ha="center",
            va="center",
            fontsize=6,
        )

    fig.subplots_adjust(left=0.01, right=0.99, bottom=0.01, top=0.99)

    fig.savefig(base / "legend.pdf", bbox_inches="tight", pad_inches=0.01)
    fig.savefig(base / "legend.png", dpi=600, bbox_inches="tight", pad_inches=0.01)
    print(f"Saved: {base / 'legend.pdf'}")
    print(f"Saved: {base / 'legend.png'}")


if __name__ == "__main__":
    main()