#!/usr/bin/env python3

import csv
import math
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


def read_rows(file_path: Path):
	rows = []
	with file_path.open("r", encoding="utf-8") as f:
		reader = csv.reader(f)
		for row in reader:
			if row:
				rows.append(row)
	return rows


def to_pow2_label(x: int) -> str:
	exp = int(x).bit_length() - 1
	return rf"$2^{{{exp}}}$"


def draw_psqf_segmented(axis, x_pos, x_vals, base_val, base_k, color, marker, linewidth, markersize, label=None):
	def value_to_pos(v):
		if v <= x_vals[0]:
			return x_pos[0]
		if v >= x_vals[-1]:
			return x_pos[-1]
		for i in range(len(x_vals) - 1):
			x0, x1 = x_vals[i], x_vals[i + 1]
			if x0 <= v <= x1:
				t = (v - x0) / (x1 - x0)
				return x_pos[i] + t * (x_pos[i + 1] - x_pos[i])
		return x_pos[-1]

	x_min = int(x_vals[0])
	x_max = int(x_vals[-1])
	step = int(base_k)

	left = x_min
	right = step
	while left <= x_max:
		r = min(right, x_max)
		y = (math.ceil(r / base_k) * base_val) / 1000.0
		p0 = value_to_pos(left)
		p1 = value_to_pos(r)
		if p1 > p0:
			axis.plot([p0, p1], [y, y], color=color, linewidth=linewidth, solid_capstyle="round")
		left = r + 1
		right += step

	ys = [(math.ceil(x / base_k) * base_val) / 1000.0 for x in x_vals]
	axis.plot(x_pos, ys, color=color, marker=marker, markersize=markersize, linestyle="None", label=label)


def main():
	base = Path(__file__).resolve().parent

	series = [
		("01_S&T.lg", "S&T", "#1f77b4", "^", 3.0, 3.0),
		("02_Single.lg", "Cuble-Single", "#ff7f0e", "v", 3.0, 3.0),
		("03_PSQF.lg", "PSQF", "#2ca02c", "s", 3.0, 3.0),
		("04_Batch.lg", "Cuble-Batch", "#9467bd", "D", 3.0, 3.0),
		("05_OptBatch.lg", "Cuble-OptBatch", "#d62728", "*", 5.0, 5.0),
	]

	x_ref = [4, 16, 64, 256, 1024]
	x_positions = list(range(len(x_ref)))

	PANEL_W = 2.40
	PANEL_H = 1.80
	fig, ax = plt.subplots(figsize=(PANEL_W, PANEL_H), dpi=300)

	all_series_y = []
	psqf_base_val = None
	psqf_base_k = None

	for file_name, label, color, marker, marker_size, inset_marker_size in series:
		rows = read_rows(base / file_name)

		if file_name in ("01_S&T.lg", "02_Single.lg"):
			# single-row file: y = (third-from-last * x) / 1000
			base_val = float(rows[0][-3])
			ys = [(base_val * x) / 1000.0 for x in x_ref]
		elif file_name == "03_PSQF.lg":
			# single-row file: y = (ceil(x / col4) * third-from-last) / 1000
			base_val = float(rows[0][-3])
			base_k = float(rows[0][3])
			psqf_base_val = base_val
			psqf_base_k = base_k
			ys = [(math.ceil(x / base_k) * base_val) / 1000.0 for x in x_ref]
		else:
			# multi-row files: y = (third-from-last) / 1000, ordered by col4
			pairs = []
			for row in rows:
				x = int(float(row[3]))
				y = float(row[-3]) / 1000.0
				pairs.append((x, y))
			pairs.sort(key=lambda p: p[0])
			y_map = {x: y for x, y in pairs}
			ys = [y_map[x] for x in x_ref]

		all_series_y.append(ys)
		if file_name == "03_PSQF.lg":
			draw_psqf_segmented(
				ax,
				x_positions,
				x_ref,
				psqf_base_val,
				psqf_base_k,
				color,
				marker,
				1.0,
				marker_size,
				label,
			)
		else:
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

	# ax.set_title("Total subsampling time for k samples")
	ax.set_xlabel("Number of samples",labelpad=1.0)
	ax.set_ylabel("Time (s)", labelpad=1.0)
	ax.set_ylim(top=520)
	ax.grid(True, linestyle="--", alpha=0.6)
	ax.tick_params(axis="both", direction="in")

	zoom_idx = [0, 1, 2]
	zoom_labels = [to_pow2_label(x_ref[i]) for i in zoom_idx]

	inset = ax.inset_axes([0.15, 0.35, 0.52, 0.48])
	for (file_name, _, color, marker, _, inset_marker_size), ys in zip(series, all_series_y):
		zoom_y = [ys[i] for i in zoom_idx]
		if file_name == "03_PSQF.lg":
			draw_psqf_segmented(
				inset,
				zoom_idx,
				[x_ref[i] for i in zoom_idx],
				psqf_base_val,
				psqf_base_k,
				color,
				marker,
				1.0,
				inset_marker_size,
			)
		else:
			inset.plot(
				zoom_idx,
				zoom_y,
				color=color,
				marker=marker,
				linewidth=1.0,
				markersize=inset_marker_size,
			)

	inset.set_xlim(-0.2, 2.2)
	inset.set_xticks(zoom_idx)
	inset.set_xticklabels(zoom_labels)
	inset.set_ylim(-2, 25)
	inset.set_yticks([0, 10, 20])
	inset.tick_params(axis="both", pad=1, length=2)
	inset.grid(True, linestyle="--", alpha=0.4)

	# mark_inset(ax, inset, loc1=3, loc2=4, fc="none", ec="0.55")
	zoom_box, conn1, conn2 = mark_inset(ax, inset, loc1=3, loc2=4, fc="none", ec="0.55")
	zoom_box.set_visible(False)
	conn1.set_visible(False)
	conn2.set_visible(False)
	fig.tight_layout(pad=0.4)

	fig.savefig(base / "plot_k.pdf", bbox_inches="tight", pad_inches=0.01)
	fig.savefig(base / "plot_k.png", dpi=600, bbox_inches="tight", pad_inches=0.01)


if __name__ == "__main__":
	main()
