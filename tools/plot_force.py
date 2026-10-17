"""
Vẽ đồ thị Mục 5.1 (giống Hình 8 của [2]) từ file log của phím T.

Cách dùng:
    python plot_force.py force_log_CSEM.csv            -> lưu force_log_CSEM.png
    python plot_force.py force_log_CSEM.csv force_log_CSEMimproved.csv

Trục trái: lực (N) và độ sâu xuyên trung bình (mm).
Trục phải: số phần tử đang va chạm.
"""
import csv
import sys

import matplotlib.pyplot as plt


def read_log(path):
    cols = {"t": [], "n": [], "depth": [], "raw": [], "filt": []}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            cols["t"].append(float(row["time_ms"]))
            cols["n"].append(int(row["num_colliding"]))
            cols["depth"].append(float(row["avg_depth_mm"]))
            cols["raw"].append(float(row["force_raw_N"]))
            cols["filt"].append(float(row["force_filtered_N"]))
    return cols


def plot(path):
    d = read_log(path)

    fig, ax_left = plt.subplots(figsize=(7, 4))
    ax_left.plot(d["t"], d["raw"], color="0.6", linewidth=1, label="total feedback force (raw)")
    ax_left.plot(d["t"], d["filt"], color="black", linewidth=2, label="filtered total feedback force")
    ax_left.plot(d["t"], d["depth"], color="tab:blue", linestyle=":", label="average penetrating depth (mm)")
    ax_left.set_xlabel("Time (ms)")
    ax_left.set_ylabel("Force (N), Depth (mm)")

    ax_right = ax_left.twinx()
    ax_right.plot(d["t"], d["n"], color="tab:orange", linestyle="--", label="number of colliding elements")
    ax_right.set_ylabel("Number of colliding elements")

    lines = ax_left.get_lines() + ax_right.get_lines()
    ax_left.legend(lines, [l.get_label() for l in lines], loc="upper left", fontsize=8)
    ax_left.set_title(path)
    fig.tight_layout()

    out = path.rsplit(".", 1)[0] + ".png"
    fig.savefig(out, dpi=150)
    print("saved", out)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    for p in sys.argv[1:]:
        plot(p)
