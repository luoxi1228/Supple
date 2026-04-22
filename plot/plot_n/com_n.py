#!/usr/bin/env python3

import csv
from pathlib import Path

import matplotlib as mpl
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter
from mpl_toolkits.axes_grid1.inset_locator import mark_inset

mpl.rcParams.update({
	"font.size": 7,
	"axes.titlesize": 8,
	"axes.labelsize": 8,
	"xtick.labelsize": 7,
	"ytick.labelsize": 7,
	"legend.fontsize": 8,
})


def read_data(file_path: Path):
	"""Read x from column 3, online from -3rd, offline from -4th."""
	data = {}

	with file_path.open("r", encoding="utf-8") as f:
		reader = csv.reader(f)
		for row in reader:
			if not row:
				continue
			x = int(float(row[2]))
			offline = float(row[-4]) / 1000.0   # ms -> s
			online = float(row[-3]) / 1000.0    # ms -> s
			data[x] = (online, offline)

	return data


def to_pow2_label(x: int) -> str:
	exp = int(x).bit_length() - 1
	return rf"$2^{{{exp}}}$"


def main():
	base = Path(__file__).resolve().parent
	file_batch = base / "04_Batch.lg"
	file_opt = base / "05_OptBatch.lg"

	data_batch = read_data(file_batch)
	data_opt = read_data(file_opt)

	xs = sorted(set(data_batch.keys()) | set(data_opt.keys()))
	x_pos = list(range(len(xs)))

	batch_online = [data_batch[x][0] for x in xs]
	batch_offline = [data_batch[x][1] for x in xs]
	opt_online = [data_opt[x][0] for x in xs]
	opt_offline = [data_opt[x][1] for x in xs]

	PANEL_W = 2.40
	PANEL_H = 1.80
	fig, ax = plt.subplots(figsize=(PANEL_W, PANEL_H), dpi=300)

	bar_width = 0.28
	batch_pos = [p - bar_width / 2 for p in x_pos]
	opt_pos = [p + bar_width / 2 for p in x_pos]

	ax.bar(batch_pos, batch_online, width=bar_width, color="#1f77b4", label="Cuple-Batch Online", zorder=3)
	ax.bar(batch_pos, [-v for v in batch_offline], width=bar_width, color="#aec7e8", label="Cuple-Batch Offline", zorder=3)
	ax.bar(opt_pos, opt_online, width=bar_width, color="#d62728", label="Cuple-OptBatch Online", zorder=3)
	ax.bar(opt_pos, [-v for v in opt_offline], width=bar_width, color="#ff9896", label="Cuple-OptBatch Offline", zorder=3)

	ax.axhline(0, color="black", linewidth=1.0)

	y_max = max(
		max(batch_online), max(batch_offline),
		max(opt_online), max(opt_offline)
	) * 1.10
	ax.set_ylim(-250, y_max)

	ax.set_xticks(x_pos)
	ax.set_xticklabels([to_pow2_label(v) for v in xs])
	ax.set_xlabel("Number of records",labelpad=1.0)
	ax.set_ylabel("Time (s)", labelpad=1.0)
	ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{int(round(abs(v)))}"))
	ax.tick_params(axis="both", direction="in")
	ax.set_axisbelow(True)
	ax.grid(axis="y", linestyle="--", alpha=0.6, zorder=0)

	zoom_idx = [0, 1, 2]
	zoom_x = [x_pos[i] for i in zoom_idx]
	axins = ax.inset_axes([0.15, 0.42, 0.50, 0.50])

	axins.bar(batch_pos, batch_online, width=bar_width, color="#1f77b4", zorder=3)
	axins.bar(batch_pos, [-v for v in batch_offline], width=bar_width, color="#aec7e8", zorder=3)
	axins.bar(opt_pos, opt_online, width=bar_width, color="#d62728", zorder=3)
	axins.bar(opt_pos, [-v for v in opt_offline], width=bar_width, color="#ff9896", zorder=3)

	axins.axhline(0, color="black", linewidth=0.8)
	inset_margin = bar_width * 0.65
	axins.set_xlim(min(zoom_x) - 2 * inset_margin, max(zoom_x) + 2 * inset_margin)
	axins.set_ylim(-15, 50)
	axins.set_yticks([-10, 0, 20, 40])
	axins.set_xticks(zoom_x)
	axins.set_xticklabels([to_pow2_label(xs[i]) for i in zoom_idx])
	axins.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{int(round(abs(v)))}"))
	axins.tick_params(axis="both", pad=1.0, length=2)
	axins.set_axisbelow(True)
	axins.grid(axis="y", linestyle="--", alpha=0.4, zorder=0)

	zoom_box, conn1, conn2 = mark_inset(ax, axins, loc1=3, loc2=4, fc="none", ec="0.55")
	zoom_box.set_visible(False)
	conn1.set_visible(False)
	conn2.set_visible(False)

	fig.tight_layout(pad=0.4)
	fig.savefig(base / "com_n.pdf", bbox_inches="tight", pad_inches=0.01)
	fig.savefig(base / "com_n.png", dpi=600, bbox_inches="tight", pad_inches=0.01)


if __name__ == "__main__":
	main()
