# ESP/Vortex Linux preflight and Verilator check

Updated: 2026-09-10. Context: [GPU fuzzing handoff](GPU_FUZZING_HANDOFF.md).

These commands check the Linux environment and test whether the pinned ESP wrapper plus Vortex can be parsed/elaborated by Verilator. They do NOT run the Linux-driver-to-GPU acceptance test or compile an executable simulator.

Status: SSH and environment inspection were completed. The Verilator lint command below has NOT yet been executed or verified to pass. Treat it as the next experiment, not a known-working recipe.

## 1. Connect and inspect

Run from your local terminal:

```sh
ssh nowfel@lab-computer
```

Then run on the Linux machine. Use Bash for the arrays and pipeline-status handling below; keep all sections in the same session.

```bash
bash
cd /home/nowfel/lab-works/fuzzing/gpu_fuzz

export PATH="/home/nowfel/tools/verilator/bin:/home/nowfel/tools/riscv64-gnu-toolchain/bin:$PATH"

verilator --version
riscv64-unknown-elf-gcc --version
g++ --version

for tool in qemu-system-riscv64 riscv64-unknown-linux-gnu-gcc riscv64-linux-gnu-gcc; do
    command -v "$tool" || echo "NOT ON PATH: $tool"
done

git -C esp rev-parse HEAD
git -C esp status --short
git -C esp submodule status
```

Expected ESP revision:

```text
ed7ed2e3daba852c2bc89c9b74c87053c53181ec
```

Previously observed: Linux x86_64, Verilator 5.034 at `/home/nowfel/tools/verilator/bin/verilator`, and a bare-metal RV64 GCC toolchain under `/home/nowfel/tools/riscv64-gnu-toolchain`. The SSH PATH did not include these directories. A bare-metal `unknown-elf` compiler is not a Linux userspace cross-compiler. Missing commands on PATH do not prove they are absent elsewhere.

The synced Linux submodule still reports modifications inherited from the original macOS case-colliding checkout. Preserve them until examined; use a fresh, Linux-local checkout for kernel builds. This RTL lint check does not consume the Linux kernel sources.

## 2. Test wrapper/GPU elaboration

Run in the same Bash session, from the project directory. Output is placed under Linux `/tmp`, outside the synced source tree. The temporary directory will not necessarily survive reboot; preserve useful logs before then.

```bash
set -o pipefail

ESP_ROOT="$PWD/esp"
VORTEX_ROOT="$ESP_ROOT/accelerators/third-party/GT_VORTEX/vortex"
RTL_ROOT="$VORTEX_ROOT/hw/rtl"
CHECK_DIR=$(mktemp -d /tmp/esp-vortex-check.XXXXXX)

includes=()
for dir in \
    "$RTL_ROOT" \
    "$RTL_ROOT/libs" \
    "$RTL_ROOT/interfaces" \
    "$RTL_ROOT/core" \
    "$RTL_ROOT/mem" \
    "$RTL_ROOT/cache" \
    "$RTL_ROOT/fpu" \
    "$VORTEX_ROOT/hw/dpi"
do
    includes+=("-I$dir")
done

verilator --lint-only --timing \
    --top-module GT_VORTEX_wrapper \
    --Mdir "$CHECK_DIR/obj_dir" \
    -Wall -Wno-fatal \
    -DSIMULATION -DSV_DPI -DXLEN_64 \
    -DESP_GT_VORTEX_NUM_CORES=1 \
    -DESP_GT_VORTEX_NUM_WARPS=4 \
    -DESP_GT_VORTEX_NUM_THREADS=4 \
    "${includes[@]}" \
    "$ESP_ROOT/accelerators/third-party/GT_VORTEX/GT_VORTEX_wrapper.v" \
    2>&1 | tee "$CHECK_DIR/lint.log"

lint_rc=${PIPESTATUS[0]}
echo "Verilator exit status: $lint_rc"
echo "Log directory: $CHECK_DIR"
```

Capture `PIPESTATUS` immediately after the pipeline, as shown. This reports Verilator's exit status instead of `tee`'s.

This experiment uses the simulation/DPI configuration. A successful result would not establish compatibility with ESP's FPGA floating-point implementation or eliminate the need to link DPI implementations in an executable harness.

## 3. Inspect and report

```bash
grep -nE '%Error|%Warning' "$CHECK_DIR/lint.log" | head -60
```

- Exit status 0: initial parsing/elaboration passed. Review warnings; `-Wno-fatal` deliberately permits them.
- Nonzero status: inspect the first errors and resolve include paths, dependencies, configuration, or RTL compatibility as indicated. Do not automatically suppress errors or change hardware semantics to force a pass.
- Neither outcome proves GPU execution, working memory transactions, interrupts, or a Linux KMD connection.

Record the exit status and printed log-directory path in the handoff. Codex can inspect that directory over SSH. If using a new shell, `CHECK_DIR` must be set to the actual path printed by the earlier run.

## 4. What follows a successful check

1. Compile and run an executable simulation harness with the required DPI support, reset/clock handling, APB control, AXI memory service, and observable completion.
2. Execute a known GPU kernel and verify its output.
3. Connect a Linux guest and the real ESP/Vortex KMD to that RTL execution path, including common ESP register/allocation behavior and interrupt delivery.
4. Run `vortex-regression basic -t 1 -n 1` inside that guest and collect software plus RTL evidence, as specified in the handoff.

The QEMU–Verilator bridge is still proposed work. These commands neither implement it nor run the full acceptance test.
