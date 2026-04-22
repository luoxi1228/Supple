#!/usr/bin/env python3

import csv
from pathlib import Path
import matplotlib as mpl
import matplotlib.pyplot as plt
from mpl_toolkits.axes_grid1.inset_locator import mark_inset

mpl.rcParams.update({
    "font.size": 7,
    "axes.titlesize": 8,
    "axes.labelsize": 8,
    "xtick.labelsize": 7,
    "ytick.labelsize": 7,
    "legend.fontsize": 8,
})


def load_xy(file_path: Path):
	xs, ys = [], []
	with file_path.open("r", encoding="utf-8") as f:
		reader = csv.reader(f)
		for row in reader:
			if not row:
				continue
			x = int(float(row[0]))
			y = float(row[-3]) / float(row[3]) / 1000.0
			xs.append(x)
			ys.append(y)
	return xs, ys


def to_pow2_label(x: int) -> str:
	exp = int(x).bit_length() - 1
	return rf"$2^{{{exp}}}$"


def main():
	base = Path(__file__).resolve().parent

	series = [
		("01_S&T.lg", "S&T", "#1f77b4", "^", 3.0, 3.0),
		("02_Single.lg", "Cuble-Single", "#ff7f0e", "v", 3.0, 3.0),
		("03_PSQF.lg", "PSQF", "#2ca02c", "s", 3.0, 3.0),
		("04_Batch.lg", "Cuble-Batch", "#9467bd", "D", 3.0, 3.0),
		("05_OptBatch.lg", "Cuble-OptBatch", "#d62728", "*", 5.0, 5.0),
	]

	PANEL_W = 2.40
	PANEL_H = 1.80
	fig, ax = plt.subplots(figsize=(PANEL_W, PANEL_H), dpi=300)

	x_ref = None
	x_positions = None
	all_series_y = []

	for file_name, label, color, marker, marker_size, inset_marker_size in series:
		xs, ys = load_xy(base / file_name)
		all_series_y.append(ys)

		if x_ref is None:
			x_ref = xs
			x_positions = list(range(len(xs)))

		ax.plot(
			x_positions,
			ys,
			label=label,
			color=color,
			marker=marker,
			linewidth=1.0,
			markersize=marker_size,
		)

	ax.set_xticks(x_positions)
	ax.set_xticklabels([to_pow2_label(x) for x in x_ref])

	# ax.set_title("Average subsampling time per sample")
	ax.set_xlabel("Block size in bytes",labelpad=1.0)
	ax.set_ylabel("Time (s)",labelpad=1.0)
	ax.set_ylim(top=52)
	ax.grid(True, linestyle="--", alpha=0.6)
	ax.tick_params(axis="both", direction="in")

	zoom_idx = [0, 1, 2]
	zoom_labels = [to_pow2_label(x_ref[i]) for i in zoom_idx]

	inset = ax.inset_axes([0.15, 0.35, 0.52, 0.48])
	for (_, _, color, marker, _, inset_marker_size), ys in zip(series, all_series_y):
		inset.plot(
			zoom_idx,
			[ys[i] for i in zoom_idx],
			color=color,
			marker=marker,
			linewidth=1.0,
			markersize=inset_marker_size,
		)

	inset.set_xlim(-0.2, 2.2)
	inset.set_xticks(zoom_idx)
	inset.set_xticklabels(zoom_labels)
	inset.set_ylim(-0.1, 1.25)
	inset.set_yticks([int(0), 0.5, 1.0])
	inset.tick_params(axis="both", pad=1, length=2)
	inset.grid(True, linestyle="--", alpha=0.4)

	# mark_inset(ax, inset, loc1=3, loc2=4, fc="none", ec="0.55")
	zoom_box, conn1, conn2 = mark_inset(ax, inset, loc1=3, loc2=4, fc="none", ec="0.55")
	zoom_box.set_visible(False)
	conn1.set_visible(False)
	conn2.set_visible(False)

	fig.tight_layout(pad=0.4)

	fig.savefig(base / "plot_b.pdf", bbox_inches="tight", pad_inches=0.01)
	fig.savefig(base / "plot_b.png", dpi=600, bbox_inches="tight", pad_inches=0.01)
 

if __name__ == "__main__":
	main()
