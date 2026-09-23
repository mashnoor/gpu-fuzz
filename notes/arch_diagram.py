#!/usr/bin/env python3
"""Render the ESP/Vortex full-stack architecture as a slide-ready PNG.

Emits a light and a dark variant at 16:9, 200 dpi (3200x1800).
"""

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Rectangle

SANS = "DejaVu Sans"
MONO = "DejaVu Sans Mono"

THEMES = {
    "light": dict(
        bg="#FFFFFF", ink="#15252A", ink2="#5A6E74", ink3="#8A9CA1",
        box="#EDF2F2", box_edge="#A8BABD",
        hw="#D8EDEF", hw_edge="#0B6E77", hw_text="#06555C",
        new_edge="#A34A18", new="#FAEADF", new_text="#8C3E13",
        zone="#F5F8F8", zone_edge="#C5D3D5",
        arrow="#54686E", irq="#0B6E77",
    ),
    "dark": dict(
        bg="#0E1618", ink="#E6EFF0", ink2="#9DB1B6", ink3="#76898E",
        box="#1C2A2E", box_edge="#3A4E53",
        hw="#12363A", hw_edge="#52C9D1", hw_text="#7FDCE2",
        new_edge="#E39264", new="#33200F", new_text="#F0AE83",
        zone="#141F22", zone_edge="#2A393D",
        arrow="#8FA4A9", irq="#52C9D1",
    ),
}


def box(ax, x, y, w, h, title, sub=None, kind="plain", t=None, title_size=13):
    """Rounded box with a bold title and an optional mono sub-label."""
    fill, edge, tcol = t["box"], t["box_edge"], t["ink"]
    lw = 1.4
    if kind == "hw":
        fill, edge, tcol, lw = t["hw"], t["hw_edge"], t["hw_text"], 1.8
    elif kind == "new":
        fill, edge, tcol, lw = t["new"], t["new_edge"], t["new_text"], 1.8

    ax.add_patch(FancyBboxPatch(
        (x, y), w, h, boxstyle="round,pad=0,rounding_size=0.7",
        facecolor=fill, edgecolor=edge, linewidth=lw, zorder=3))

    if sub:
        ax.text(x + w / 2, y + h * 0.62, title, ha="center", va="center",
                fontsize=title_size, fontweight="bold", family=SANS,
                color=tcol, zorder=4)
        ax.text(x + w / 2, y + h * 0.27, sub, ha="center", va="center",
                fontsize=9, family=MONO, color=t["ink2"], zorder=4)
    else:
        ax.text(x + w / 2, y + h / 2, title, ha="center", va="center",
                fontsize=title_size, fontweight="bold", family=SANS,
                color=tcol, zorder=4)


def arrow(ax, x1, y1, x2, y2, t, color=None, lw=2.0, ls="-"):
    ax.add_patch(FancyArrowPatch(
        (x1, y1), (x2, y2), arrowstyle="-|>", mutation_scale=22,
        linewidth=lw, linestyle=ls,
        color=color or t["arrow"], zorder=5,
        shrinkA=0, shrinkB=0))


def label(ax, x, y, s, t, size=10, col=None, ha="left", mono=False, weight="normal"):
    ax.text(x, y, s, ha=ha, va="center", fontsize=size,
            family=MONO if mono else SANS, color=col or t["ink2"],
            fontweight=weight, zorder=6)


def render(theme_name):
    t = THEMES[theme_name]
    fig, ax = plt.subplots(figsize=(16, 9), dpi=200)
    fig.patch.set_facecolor(t["bg"])
    ax.set_facecolor(t["bg"])
    ax.set_xlim(0, 160)
    ax.set_ylim(0, 90)
    ax.set_aspect("equal")
    ax.axis("off")

    # ---------------- title ----------------
    ax.text(4, 86.4, "Full-stack GPU testing: Linux driver to Vortex RTL",
            fontsize=17, fontweight="bold", family=SANS, color=t["ink"])
    ax.text(4, 83.0,
            "Every layer is real — the application, both drivers and the GPU "
            "are unmodified; only the two orange boxes were written for this work.",
            fontsize=10.5, family=SANS, color=t["ink2"])

    # ---------------- HOST ----------------
    ax.add_patch(Rectangle((3, 3), 154, 76, facecolor="none",
                           edgecolor=t["ink3"], linewidth=1.6,
                           linestyle=(0, (6, 4)), zorder=1))
    label(ax, 5, 76.6, "HOST   x86-64 Linux workstation", t,
          size=11, col=t["ink3"], weight="bold")

    # ---------------- GUEST ----------------
    ax.add_patch(Rectangle((6, 36), 88, 38, facecolor=t["zone"],
                           edgecolor=t["zone_edge"], linewidth=1.6, zorder=2))
    label(ax, 8, 71.8, "GUEST   QEMU RISC-V virtual machine · Linux 6.6",
          t, size=11, col=t["ink2"], weight="bold")

    # user space
    label(ax, 8, 68.0, "USER SPACE", t, size=8.5, col=t["ink3"], weight="bold")
    box(ax, 10, 61.0, 80, 6.0, "APPLICATION",
        "basic.exe   —  the program that wants GPU work done", t=t)
    arrow(ax, 50, 61.0, 50, 57.6, t)
    label(ax, 51.5, 59.3, "vx_start()   library call", t, size=9.5, mono=True)

    box(ax, 10, 51.4, 80, 6.2, "UMD   user-mode driver",
        "libvortex.so  +  libvortex-esp.so", t=t)

    # syscall boundary
    ax.plot([8, 88], [48.6, 48.6], color=t["ink3"], lw=1.4, ls=(0, (5, 3)), zorder=4)
    label(ax, 88.6, 48.6, " the wall", t, size=8.5, col=t["ink3"])
    arrow(ax, 50, 51.4, 50, 46.0, t)
    label(ax, 51.5, 50.2, "ioctl()  on  /dev/gt_vortex_rtl.0", t, size=9.5, mono=True)

    # kernel space
    label(ax, 8, 44.6, "KERNEL SPACE", t, size=8.5, col=t["ink3"], weight="bold")
    box(ax, 10, 37.6, 80, 8.0, "KMD   kernel-mode driver",
        "gt_vortex_rtl.ko  +  esp.ko  +  contig_alloc.ko  +  esp_cache.ko", t=t)

    # ---------------- out of the guest ----------------
    arrow(ax, 50, 37.6, 50, 32.6, t)
    label(ax, 51.5, 35.6, "MMIO register writes", t, size=9.5, weight="bold")
    label(ax, 51.5, 33.5, "0x10010050 .. 0x70   (8 writes, last = GO)", t,
          size=9, mono=True)

    box(ax, 10, 26.4, 80, 6.2, "QEMU DEVICE   pretends to be the GPU",
        "qemu-v8.2.2/hw/misc/esp_vortex.c", kind="new", t=t)
    arrow(ax, 50, 26.4, 50, 21.6, t)
    label(ax, 51.5, 24.0, "hands the values to the bridge", t, size=9.5)

    box(ax, 10, 15.4, 80, 6.2, "BRIDGE   drives the chip's pins",
        "qemu-vortex-bridge/libesp_vortex_bridge.so", kind="new", t=t)
    arrow(ax, 50, 15.4, 50, 12.6, t)
    label(ax, 51.5, 14.0, "APB bus cycles  —  clk, psel, paddr, pwdata", t,
          size=9, mono=True)

    # ---------------- the chip ----------------
    box(ax, 10, 5.0, 80, 7.6, "GPU   real RTL, simulated by Verilator",
        "GT_VORTEX_wrapper.v   →   Vortex  (1 core · 4 warps · 4 threads)",
        kind="hw", t=t, title_size=13.5)

    # ---------------- guest RAM ----------------
    box(ax, 104, 12.0, 50, 55.6, "", None, t=t)
    ax.text(129, 64.4, "GUEST RAM", ha="center", va="center", fontsize=13,
            fontweight="bold", family=SANS, color=t["ink"], zorder=4)
    ax.text(129, 61.4, "physical memory of the virtual machine",
            ha="center", va="center", fontsize=9, family=SANS,
            color=t["ink2"], zorder=4)

    seg = [
        (46.0, 12.0, "0x80000000", "Linux’s own RAM", "plain",
         "boot with mem=512M so Linux\nnever touches what is above"),
        (36.0, 8.0, "0xA0000000", "contig_alloc pool", "plain", None),
        (14.0, 20.0, "0xA5000000", "the GPU’s memory window", "hw",
         "kernel program · inputs · results\nthe RTL reads and writes here"),
    ]
    for y, h, addr, name, kind, note in seg:
        fill = t["hw"] if kind == "hw" else t["box"]
        edge = t["hw_edge"] if kind == "hw" else t["box_edge"]
        tcol = t["hw_text"] if kind == "hw" else t["ink"]
        ax.add_patch(Rectangle((108, y), 42, h, facecolor=fill,
                               edgecolor=edge, linewidth=1.5, zorder=4))
        ax.text(110, y + h - 1.9, addr, ha="left", va="center", fontsize=9,
                family=MONO, color=t["ink2"], zorder=5)
        name_y = y + h / 2 + (0.8 if note else -1.2)
        ax.text(129, name_y, name, ha="center", va="center", fontsize=11,
                fontweight="bold", family=SANS, color=tcol, zorder=5)
        if note:
            ax.text(129, y + h / 2 - 3.4, note, ha="center", va="center",
                    fontsize=8.5, family=SANS, color=t["ink2"], zorder=5)

    # DMA arrow: the GPU itself reaches into guest RAM
    arrow(ax, 90, 9.5, 107, 17.5, t, color=t["irq"], lw=2.6)
    label(ax, 92.0, 6.4, "AXI DMA", t, size=10.5, col=t["irq"], weight="bold")
    label(ax, 92.0, 4.3, "the GPU reads and writes memory by itself", t,
          size=8.5, col=t["ink2"])

    # ---------------- the data path: UMD writes memory directly ----------------
    # The launch *command* goes down through the KMD, but the data does not:
    # the UMD mmap()s /dev/mem and memcpy()s straight into the GPU's window.
    dash = (0, (5, 3))
    ax.plot([90, 99], [54.5, 54.5], color=t["arrow"], lw=2.0,
            ls=dash, zorder=5)
    ax.plot([99, 99], [54.5, 24.0], color=t["arrow"], lw=2.0,
            ls=dash, zorder=5)
    ax.add_patch(FancyArrowPatch((99, 24.0), (107.5, 24.0), arrowstyle="-|>",
                                 mutation_scale=22, linewidth=2.0,
                                 linestyle=dash, color=t["arrow"], zorder=5,
                                 shrinkA=0, shrinkB=0))
    ax.text(97.0, 39.0, "/dev/mem  —  data skips the KMD",
            rotation=90, ha="center", va="center", fontsize=8.5,
            family=SANS, color=t["ink2"], zorder=6)

    # ---------------- the return path ----------------
    rx = 7.0
    ax.plot([rx, rx], [8.8, 41.6], color=t["irq"], lw=2.4, zorder=5)
    ax.add_patch(FancyArrowPatch((rx, 38.0), (rx, 42.4), arrowstyle="-|>",
                                 mutation_scale=22, linewidth=2.4,
                                 color=t["irq"], zorder=6,
                                 shrinkA=0, shrinkB=0))
    ax.plot([rx, 9.6], [8.8, 8.8], color=t["irq"], lw=2.4, zorder=5)
    ax.plot([rx, 9.6], [41.6, 41.6], color=t["irq"], lw=2.4, zorder=5)
    ax.text(4.4, 26.5, "busy_interrupt  →  PLIC IRQ 12  →  wakes the application",
            rotation=90, ha="center", va="center", fontsize=9,
            family=SANS, color=t["irq"], fontweight="bold", zorder=6)

    # ---------------- legend ----------------
    lx, ly = 12, 0.2
    keys = [
        ("plain", "unmodified ESP / Vortex code"),
        ("new", "written for this project"),
        ("hw", "simulated logic gates"),
    ]
    for i, (kind, text) in enumerate(keys):
        fill = {"plain": t["box"], "new": t["new"], "hw": t["hw"]}[kind]
        edge = {"plain": t["box_edge"], "new": t["new_edge"], "hw": t["hw_edge"]}[kind]
        x = lx + i * 40.0
        ax.add_patch(Rectangle((x, ly + 0.5), 2.4, 1.8, facecolor=fill,
                               edgecolor=edge, linewidth=1.5, zorder=4))
        ax.text(x + 3.2, ly + 1.4, text, ha="left", va="center", fontsize=8.5,
                family=SANS, color=t["ink2"], zorder=5)

    out = ("/home/nowfel/lab-works/fuzzing/gpu_fuzz/notes/"
           f"vortex-architecture-{theme_name}.png")
    fig.savefig(out, facecolor=t["bg"], bbox_inches="tight", pad_inches=0.25)
    plt.close(fig)
    return out


if __name__ == "__main__":
    for name in ("light", "dark"):
        print("wrote", render(name))
