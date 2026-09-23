const pptxgen = require("pptxgenjs");

// ---------- palette: "Silicon & Copper" — dark graphite + warm copper accent ----------
const C = {
  graphite: "1B1F27",      // primary dark (title/conclusion bg, headers)
  graphiteSoft: "2A303B",  // secondary dark surface
  steel: "5B6472",         // muted supporting tone
  steelLight: "AEB6C2",    // light supporting tone (on dark bg)
  copper: "E08E45",        // sharp accent
  copperDeep: "B8672A",    // darker accent for text-on-light
  paper: "FFFFFF",         // content bg
  paperSoft: "F3F4F6",     // card bg on light slides
  ink: "1B1F27",           // body text on light
  inkSoft: "5B6472",       // muted body text
  good: "3E7C59",          // for positive stat (bugs/CVE green-ish, muted)
  line: "E1E3E7",
};

const FONT_HEAD = "Cambria";
const FONT_BODY = "Calibri";

const pres = new pptxgen();
pres.layout = "LAYOUT_WIDE"; // 13.3 x 7.5
const PGW = 13.33, PGH = 7.5;

pres.defineSlideMaster({
  title: "BLANK",
  background: { color: C.paper },
});

function eyebrow(slide, text, opts = {}) {
  slide.addText(text.toUpperCase(), {
    x: opts.x ?? 0.6, y: opts.y ?? 0.45, w: opts.w ?? 8, h: 0.35,
    fontFace: FONT_BODY, fontSize: 12, bold: true, color: opts.color ?? C.copperDeep,
    charSpacing: 2, align: "left",
  });
}

function slideNum(slide, n) {
  slide.addText(String(n).padStart(2, "0"), {
    x: PGW - 0.9, y: PGH - 0.5, w: 0.6, h: 0.3,
    fontFace: FONT_BODY, fontSize: 10, color: C.steel, align: "right",
  });
}

function pageTitle(slide, text, opts = {}) {
  slide.addText(text, {
    x: opts.x ?? 0.6, y: opts.y ?? 0.78, w: opts.w ?? 11.8, h: opts.h ?? 0.9,
    fontFace: FONT_HEAD, fontSize: opts.size ?? 30, bold: true, color: opts.color ?? C.ink,
    align: "left",
  });
}

function bulletBlock(slide, items, opts) {
  const baseColor = opts.color ?? C.ink;
  const paras = items.map((it, i) => {
    const obj = { text: it.text, options: { bullet: { code: "25AA" }, color: it.color ?? baseColor, fontSize: it.size ?? 14, fontFace: FONT_BODY, breakLine: i !== items.length - 1, paraSpaceAfter: 10, bold: !!it.bold } };
    return obj;
  });
  slide.addText(paras, { x: opts.x, y: opts.y, w: opts.w, h: opts.h, valign: "top" });
}

function imageCard(slide, path, x, y, w, h, opts = {}) {
  // subtle card behind the figure (no edge stripes — full soft-tint background + shadow)
  slide.addShape("roundRect", {
    x: x - 0.18, y: y - 0.18, w: w + 0.36, h: h + 0.36, rectRadius: 0.08,
    fill: { color: opts.cardColor ?? C.paperSoft }, line: { color: C.line, width: 1 },
    shadow: { type: "outer", color: "000000", opacity: 0.12, blur: 8, offset: 3, angle: 90 },
  });
  slide.addImage({ path, x, y, w, h });
}

function statCallout(slide, x, y, w, num, label, opts = {}) {
  slide.addText(num, { x, y, w, h: 0.85, fontFace: FONT_HEAD, fontSize: opts.numSize ?? 40, bold: true, color: opts.numColor ?? C.copperDeep, align: "left" });
  slide.addText(label, { x, y: y + 0.78, w, h: 0.6, fontFace: FONT_BODY, fontSize: 12, color: C.inkSoft, align: "left" });
}

function flowNode(slide, x, y, w, h, text, opts = {}) {
  slide.addShape("roundRect", { x, y, w, h, rectRadius: 0.07, fill: { color: opts.fill ?? C.paperSoft }, line: { color: opts.border ?? C.line, width: 1 } });
  slide.addText(text, { x: x + 0.05, y, w: w - 0.1, h, fontFace: FONT_BODY, fontSize: opts.size ?? 11.5, bold: opts.bold ?? true, color: opts.color ?? C.ink, align: "center", valign: "middle", lineSpacingMultiple: 1.05 });
}
function flowArrowH(slide, x1, y, x2, opts = {}) {
  slide.addShape("line", { x: x1, y, w: x2 - x1, h: 0, line: { color: opts.color ?? C.steel, width: opts.width ?? 1.75, endArrowType: "triangle" } });
}

const ASSETS = __dirname;
const img = (f) => `${ASSETS}/${f}`;

// =====================================================================
// SLIDE 1 — Title
// =====================================================================
{
  const s = pres.addSlide();
  s.background = { color: C.graphite };
  // faint circuit-like corner motif via thin rounded rects (kept subtle, not a stripe: a contained decorative block, upper-right)
  s.addShape("roundRect", { x: 10.6, y: -1.2, w: 4.5, h: 4.5, rectRadius: 0.9, fill: { color: C.graphiteSoft }, line: { type: "none" }, rotate: 20 });
  s.addShape("roundRect", { x: 11.6, y: -0.4, w: 2.6, h: 2.6, rectRadius: 0.5, fill: { color: C.copper }, line: { type: "none" }, rotate: 20 , transparency: 78});

  s.addText("USENIX SECURITY 2026  ·  35TH USENIX SECURITY SYMPOSIUM", {
    x: 0.8, y: 1.5, w: 10, h: 0.4, fontFace: FONT_BODY, fontSize: 13, bold: true, color: C.copper, charSpacing: 2,
  });
  s.addText("Fuzzing Open-Source GPU Hardware\nwith SIMT Program Generation", {
    x: 0.8, y: 2.1, w: 11, h: 2.1, fontFace: FONT_HEAD, fontSize: 40, bold: true, color: "FFFFFF", lineSpacingMultiple: 1.08,
  });
  s.addText("FuzzGPU — the first RTL fuzzing framework built around the GPU's own SIMT execution model", {
    x: 0.8, y: 4.15, w: 10.5, h: 0.6, fontFace: FONT_BODY, fontSize: 16, italic: true, color: C.steelLight,
  });
  s.addText("Zibo Gao, Jie Wang, Qihang Zhou, Lixiao Shan, Junjie Hu, Xiaoqi Jia, Zhiqiang Lv", {
    x: 0.8, y: 6.3, w: 11, h: 0.4, fontFace: FONT_BODY, fontSize: 13, color: "FFFFFF",
  });
  s.addText("Institute of Information Engineering, Chinese Academy of Sciences  ·  School of Cyber Security, UCAS", {
    x: 0.8, y: 6.68, w: 11, h: 0.4, fontFace: FONT_BODY, fontSize: 11.5, color: C.steelLight,
  });
}

// =====================================================================
// SLIDE 2 — Flow / agenda
// =====================================================================
{
  const s = pres.addSlide();
  eyebrow(s, "Roadmap");
  pageTitle(s, "How this talk is organized");

  const steps = [
    ["01", "Where hardware fuzzing stands today", "CPU-style instruction-sequence fuzzers, and what they assume"],
    ["02", "Why that breaks on GPUs", "SIMT concurrency, divergence, barriers, memory races"],
    ["03", "FuzzGPU's two-phase design", "SIMT program generator + differential testing harness"],
    ["04", "How programs get generated", "Generation stack, barrier planner, interleaved warps"],
    ["05", "How bugs get caught", "RTL instrumentation, taint, memory-consistency checking"],
    ["06", "Results & takeaways", "20 new bugs, 10 CVEs, 2x coverage, 28x throughput"],
  ];
  const cols = 3, rows = 2, gx = 0.4, gy = 0.35, cw = (11.8 - gx * (cols - 1)) / cols, ch = 2.15;
  steps.forEach((st, i) => {
    const col = i % cols, row = Math.floor(i / cols);
    const x = 0.6 + col * (cw + gx), y = 2.05 + row * (ch + gy);
    s.addShape("roundRect", { x, y, w: cw, h: ch, rectRadius: 0.09, fill: { color: C.paperSoft }, line: { type: "none" },
      shadow: { type: "outer", color: "000000", opacity: 0.08, blur: 6, offset: 2, angle: 90 } });
    s.addText(st[0], { x: x + 0.25, y: y + 0.18, w: 1.2, h: 0.6, fontFace: FONT_HEAD, fontSize: 26, bold: true, color: C.copper });
    s.addText(st[1], { x: x + 0.25, y: y + 0.85, w: cw - 0.5, h: 0.6, fontFace: FONT_BODY, fontSize: 14, bold: true, color: C.ink });
    s.addText(st[2], { x: x + 0.25, y: y + 1.4, w: cw - 0.5, h: 0.65, fontFace: FONT_BODY, fontSize: 11, color: C.inkSoft });
  });
  slideNum(s, 2);
}

// =====================================================================
// SLIDE 3 — Current fuzzing approaches (motivation, CPU-style)
// =====================================================================
{
  const s = pres.addSlide();
  eyebrow(s, "Background");
  pageTitle(s, "Hardware fuzzing today: built for CPUs");

  bulletBlock(s, [
    { text: "Hardware bugs are permanent — once fabricated, silicon can't be patched like software.", bold: false },
    { text: "State-of-the-art RTL fuzzers (e.g., Cascade, DiveFuzz) generate & mutate random instruction sequences, then check them against a Golden Reference Model (GRM)." },
    { text: "This “differential testing” recipe works well for CPUs: one thread, one program counter, in-order retirement." },
    { text: "No prior work systematically studies how to generate valid test programs for a GPU's execution model — this paper is the first.", bold: true },
  ], { x: 0.6, y: 2.0, w: 5.9, h: 4.6 });

  imageCard(s, img("fig2_histogram.png"), 7.0, 2.5, 5.5, 5.5 / 3.01, {});
  s.addText("Figure 2 — Cascade & DiveFuzz manually ported to a real GPU (Vortex64)", {
    x: 7.0, y: 2.5 + 5.5 / 3.01 + 0.25, w: 5.5, h: 0.4, fontFace: FONT_BODY, fontSize: 11, italic: true, color: C.inkSoft,
  });
  s.addText("Most ported test cases execute almost nothing before aborting.", {
    x: 7.0, y: 2.5 + 5.5 / 3.01 + 0.62, w: 5.5, h: 0.5, fontFace: FONT_BODY, fontSize: 12.5, bold: true, color: C.copperDeep,
  });

  s.addText("THE CLASSIC RECIPE", { x: 7.0, y: 5.75, w: 5.5, h: 0.3, fontFace: FONT_BODY, fontSize: 10.5, bold: true, color: C.steel, charSpacing: 1.5 });
  const fy = 6.15, fh = 0.62;
  flowNode(s, 7.0, fy, 1.5, fh, "Generate\ninstructions", { fill: C.paperSoft, size: 10.5 });
  flowArrowH(s, 8.5, fy + fh / 2, 9.0);
  flowNode(s, 9.0, fy, 1.5, fh, "Run on\nDUT & GRM", { fill: C.paperSoft, size: 10.5 });
  flowArrowH(s, 10.5, fy + fh / 2, 11.0);
  flowNode(s, 11.0, fy, 1.5, fh, "Compare\nfinal state", { fill: C.paperSoft, size: 10.5 });
  slideNum(s, 3);
}

// =====================================================================
// SLIDE 4 — GPU background (Figure 1)
// =====================================================================
{
  const s = pres.addSlide();
  eyebrow(s, "Background");
  pageTitle(s, "The execution model these fuzzers never see");

  imageCard(s, img("fig1_gpu_arch.png"), 0.7, 2.0, 6.1, 6.1 / 1.3242, {});
  s.addText("Figure 1 — GPU microarchitecture overview (paper, §2.1)", {
    x: 0.7, y: 2.0 + 6.1/1.3242 + 0.1, w: 6.1, h: 0.35, fontFace: FONT_BODY, fontSize: 10.5, italic: true, color: C.inkSoft,
  });

  bulletBlock(s, [
    { text: "SIMT: many threads share one instruction stream, grouped into warps (32 threads).", size: 14 },
    { text: "Threads keep a private register file + predicate mask — they can individually go inactive." },
    { text: "A SIMT Stack in hardware tracks active-thread masks (TMASK) and reconvergence PCs (RPC) for divergent branches." },
    { text: "Warps run concurrently across Processing Blocks, sharing L1/L2 caches — with out-of-order writeback and no CPU-style reorder buffer.", bold: true },
  ], { x: 7.1, y: 2.05, w: 5.6, h: 4.8 });
  slideNum(s, 4);
}

// =====================================================================
// SLIDE 5 — Problem: why CPU fuzzers fail (3 challenges)
// =====================================================================
{
  const s = pres.addSlide();
  eyebrow(s, "The Problem");
  pageTitle(s, "Three challenges a single-threaded fuzzer can't see");

  const chall = [
    ["Test Program Validity", "Random instruction sequences stall almost immediately once real thread/warp concurrency is turned on — median completion collapses versus single-thread runs."],
    ["Memory Non-determinism", "Relaxed cross-thread memory ordering means the “correct” value at a shared address is legitimately ambiguous — an ISA-level GRM can't model it."],
    ["Differential-Testing Feasibility", "Inter-warp barriers + out-of-order writeback break classic lockstep DUT-vs-GRM comparison outright."],
  ];
  const cw = 3.85, gx = 0.25;
  chall.forEach((c, i) => {
    const x = 0.6 + i * (cw + gx);
    s.addShape("roundRect", { x, y: 1.95, w: cw, h: 3.55, rectRadius: 0.09, fill: { color: C.graphite }, line: { type: "none" } });
    s.addShape("ellipse", { x: x + 0.28, y: 2.22, w: 0.55, h: 0.55, fill: { color: C.copper }, line: { type: "none" } });
    s.addText(String(i + 1), { x: x + 0.28, y: 2.22, w: 0.55, h: 0.55, fontFace: FONT_HEAD, fontSize: 20, bold: true, color: C.graphite, align: "center", valign: "middle" });
    s.addText("CHALLENGE " + (i + 1), { x: x + 0.98, y: 2.22, w: cw - 1.2, h: 0.55, fontFace: FONT_BODY, fontSize: 11, bold: true, color: C.copper, charSpacing: 1.2, valign: "middle" });
    s.addText(c[0], { x: x + 0.28, y: 2.9, w: cw - 0.55, h: 0.8, fontFace: FONT_HEAD, fontSize: 17, bold: true, color: "FFFFFF" });
    s.addText(c[1], { x: x + 0.28, y: 3.72, w: cw - 0.55, h: 1.7, fontFace: FONT_BODY, fontSize: 12, color: C.steelLight, lineSpacingMultiple: 1.15 });
  });

  s.addText("Root cause: prior fuzzers generate one thread's instructions at a time — nothing plans for divergence, barriers, or cross-warp races.", {
    x: 0.6, y: 5.7, w: 12.1, h: 0.6, fontFace: FONT_BODY, fontSize: 13.5, italic: true, bold: true, color: C.copperDeep,
  });
  slideNum(s, 5);
}

// =====================================================================
// SLIDE 6 — Concrete failure modes (Fig 3 + Fig 4)
// =====================================================================
{
  const s = pres.addSlide();
  eyebrow(s, "The Problem, Concretely");
  pageTitle(s, "What actually goes wrong");

  imageCard(s, img("fig3_rejections.png"), 0.7, 2.05, 5.8, 5.8/2.658, {});
  s.addText("Figure 3 — Why ported CPU-fuzzer programs get rejected", { x: 0.7, y: 2.05+5.8/2.658+0.12, w: 5.8, h: 0.35, fontFace: FONT_BODY, fontSize: 10.5, italic: true, color: C.inkSoft });
  s.addText("SIMT-stack errors and invalid, thread-divergence-induced control flow dominate — not real hardware bugs, just unsupported program shapes.", {
    x: 0.7, y: 2.05+5.8/2.658+0.5, w: 5.8, h: 0.9, fontFace: FONT_BODY, fontSize: 12, color: C.ink,
  });

  imageCard(s, img("fig4_lockstep.png"), 6.9, 2.05, 5.75, 5.75/2.327, {});
  s.addText("Figure 4 — Ordering ambiguity in lockstep testing", { x: 6.9, y: 2.05+5.75/2.327+0.12, w: 5.75, h: 0.35, fontFace: FONT_BODY, fontSize: 10.5, italic: true, color: C.inkSoft });
  s.addText("A barrier stalls W1 in real hardware while the GRM has no way to know W2 hasn't arrived yet — stepping the GRM naively produces a spurious mismatch.", {
    x: 6.9, y: 2.05+5.75/2.327+0.5, w: 5.75, h: 0.9, fontFace: FONT_BODY, fontSize: 12, color: C.ink,
  });
  slideNum(s, 6);
}

// =====================================================================
// SLIDE 7 — FuzzGPU solution overview (hero slide, Figure 5)
// =====================================================================
{
  const s = pres.addSlide();
  s.background = { color: C.graphite };
  eyebrow(s, "The Solution", { color: C.copper });
  pageTitle(s, "FuzzGPU: two primitives, one fuzzing loop", { color: "FFFFFF" });

  const f5w = 7.6, f5h = f5w / 1.968, f5x = (PGW - f5w) / 2, f5y = 2.05;
  imageCard(s, img("fig5_fuzzing_loop.png"), f5x, f5y, f5w, f5h, { cardColor: "FFFFFF" });
  s.addText("Figure 5 — Fuzzing loop overview (paper, §4.1)", {
    x: f5x, y: f5y + f5h + 0.22, w: f5w, h: 0.3, fontFace: FONT_BODY, fontSize: 11, italic: true, color: C.steelLight, align: "center",
  });

  s.addText([
    { text: "(i) SIMT Program Generator", options: { bold: true, color: C.copper, breakLine: false } },
    { text: "  — an ISA-emulation-guided generator that builds valid, intricate, multi-warp test programs.        ", options: { color: "FFFFFF", breakLine: true } },
    { text: "(ii) Differential Testing Harness", options: { bold: true, color: C.copper, breakLine: false } },
    { text: "  — instruction-level DUT-vs-GRM comparison that tolerates GPU non-determinism.", options: { color: "FFFFFF" } },
  ], { x: 1.0, y: f5y + f5h + 0.58, w: 11.3, h: 0.7, fontFace: FONT_BODY, fontSize: 13, align: "center", valign: "top" });
  slideNum(s, 7);
}

// =====================================================================
// SLIDE 8 — SIMT Program Generator: structure + instruction selection
// =====================================================================
{
  const s = pres.addSlide();
  eyebrow(s, "Phase (i) — Generator");
  pageTitle(s, "Step 1: build the program's skeleton");

  bulletBlock(s, [
    { text: "Target structure follows the real GPU hierarchy: CTA → warps → basic blocks, each block a straight-line instruction run ending in a control-transfer instruction." },
    { text: "A top-down, two-level loop: a top-level loop plans overall structure; a bottom-level loop fills in each warp's own instruction stream." },
    { text: "Instructions are randomly sampled from the full, category-weighted opcode pool (integer, float, atomics, vector, GPU-custom ops)." },
    { text: "Constrained instructions (e.g., matrix-multiply thread counts, CSR encodings) use rejection sampling — re-roll operands until they're legal, never emit garbage.", bold: true },
  ], { x: 0.6, y: 2.0, w: 6.1, h: 4.7 });

  // simple structure diagram: CTA -> warps -> blocks, built from shapes (no external image needed)
  const bx = 7.1, by = 2.1, bw = 5.6;
  s.addShape("roundRect", { x: bx, y: by, w: bw, h: 4.5, rectRadius: 0.08, fill: { color: C.paperSoft }, line: { color: C.line, width: 1 } });
  s.addText("CTA", { x: bx + 0.3, y: by + 0.25, w: 2, h: 0.4, fontFace: FONT_BODY, fontSize: 12, bold: true, color: C.steel, charSpacing: 1 });
  ["Warp 0", "Warp 1", "Warp n"].forEach((w, i) => {
    const wy = by + 0.75 + i * 1.2;
    s.addShape("roundRect", { x: bx + 0.3, y: wy, w: bw - 0.6, h: 0.95, rectRadius: 0.06, fill: { color: C.paper }, line: { color: C.steelLight, width: 1 } });
    s.addText(w, { x: bx + 0.5, y: wy + 0.06, w: 1.4, h: 0.3, fontFace: FONT_BODY, fontSize: 11, bold: true, color: C.inkSoft });
    const blocks = i === 2 ? 2 : 3;
    for (let bIdx = 0; bIdx < blocks; bIdx++) {
      const bxi = bx + 0.55 + bIdx * 1.15;
      s.addShape("roundRect", { x: bxi, y: wy + 0.42, w: 0.95, h: 0.42, rectRadius: 0.05, fill: { color: C.copper }, line: { type: "none" } });
      s.addText("block", { x: bxi, y: wy + 0.42, w: 0.95, h: 0.42, fontFace: FONT_BODY, fontSize: 9.5, bold: true, color: "FFFFFF", align: "center", valign: "middle" });
      if (bIdx < blocks - 1) s.addText("→", { x: bxi + 0.95, y: wy + 0.4, w: 0.2, h: 0.45, fontFace: FONT_BODY, fontSize: 14, color: C.steel, align: "center", valign: "middle" });
    }
  });
  slideNum(s, 8);
}

// =====================================================================
// SLIDE 9 — Generation stack (Figure 6)
// =====================================================================
{
  const s = pres.addSlide();
  eyebrow(s, "Phase (i) — Generator");
  pageTitle(s, "Step 2: the generation stack resolves divergence");

  imageCard(s, img("fig6_genstack.png"), 0.6, 2.0, 8.5, 8.5/2.376, {});
  s.addText("Figure 6 — Generation stack example (paper, §4.2.2)", {
    x: 0.6, y: 2.0 + 8.5/2.376 + 0.14, w: 8.5, h: 0.35, fontFace: FONT_BODY, fontSize: 10.5, italic: true, color: C.inkSoft,
  });

  bulletBlock(s, [
    { text: "One Generation Program Counter (GPC) per warp — as long as threads agree, only one stream is generated.", size: 13 },
    { text: "A branch that splits the thread mask pushes a stack entry (PC, TMASK, not-taken target, R-flag) and generation follows the taken side depth-first." },
    { text: "At the reconvergence point with R=0, generation jumps to the pending not-taken path instead — both sides get real code, never dead code." },
    { text: "Guided live by an ISA Simulator, so branch outcomes are resolved with real register values, not guesses.", bold: true },
  ], { x: 9.35, y: 2.05, w: 3.4, h: 4.6 });
  slideNum(s, 9);
}

// =====================================================================
// SLIDE 10 — Top-level loop: barrier planner + interleaved warps
// =====================================================================
{
  const s = pres.addSlide();
  eyebrow(s, "Phase (i) — Generator");
  pageTitle(s, "Step 3: coordinate across warps, not just within one");

  const cw = 5.85, gx = 0.4;
  const cards = [
    ["Barrier Planner", "Every barrier ID and its participating warps are decided top-down, before a single instruction is emitted — guaranteeing every warp that shows up agrees on where, so the generated program can never deadlock on its own barriers."],
    ["Interleaved Warp Generation", "Warps are generated side-by-side rather than one at a time. Every memory access is checked against every other warp's in-flight accesses, so genuine cross-thread and cross-warp races get deliberately planted — and tainted addresses are flagged for the checker instead of guessed at."],
  ];
  cards.forEach((c, i) => {
    const x = 0.6 + i * (cw + gx);
    s.addShape("roundRect", { x, y: 2.0, w: cw, h: 3.0, rectRadius: 0.09, fill: { color: C.paperSoft }, line: { type: "none" }, shadow: { type: "outer", color: "000000", opacity: 0.08, blur: 6, offset: 2, angle: 90 } });
    s.addShape("ellipse", { x: x + 0.32, y: 2.32, w: 0.55, h: 0.55, fill: { color: C.copper }, line: { type: "none" } });
    s.addText(String(i + 1), { x: x + 0.32, y: 2.32, w: 0.55, h: 0.55, fontFace: FONT_HEAD, fontSize: 20, bold: true, color: "FFFFFF", align: "center", valign: "middle" });
    s.addText(c[0], { x: x + 1.05, y: 2.35, w: cw - 1.3, h: 0.55, fontFace: FONT_HEAD, fontSize: 18, bold: true, color: C.ink, valign: "middle" });
    s.addText(c[1], { x: x + 0.35, y: 3.05, w: cw - 0.7, h: 1.8, fontFace: FONT_BODY, fontSize: 13.5, color: C.inkSoft, lineSpacingMultiple: 1.25, valign: "top" });
  });

  s.addShape("roundRect", { x: 0.6, y: 5.35, w: 12.1, h: 1.35, rectRadius: 0.09, fill: { color: C.graphite }, line: { type: "none" } });
  s.addText("Both are decided before generation ever emits an instruction — the top-level loop plans structure, the bottom-level loop (previous step) fills it in.", {
    x: 0.95, y: 5.35, w: 11.4, h: 1.35, fontFace: FONT_BODY, fontSize: 14, italic: true, color: "FFFFFF", valign: "middle",
  });
  slideNum(s, 10);
}

// =====================================================================
// SLIDE 11 — Differential testing harness
// =====================================================================
{
  const s = pres.addSlide();
  eyebrow(s, "Phase (ii) — Differential Testing");
  pageTitle(s, "Catching bugs despite GPU non-determinism");

  // pipeline diagram: what actually happens to one generated test
  const dy = 1.95, dh = 0.85;
  flowNode(s, 0.6, dy, 2.3, dh, "Generated\nTest Program", { fill: C.paperSoft, size: 11.5 });
  flowArrowH(s, 2.9, dy + dh / 2, 3.2);
  flowNode(s, 3.2, dy, 4.6, dh, "Co-Simulation\nRTL (DUT)  +  Golden Model (ISS)", { fill: C.graphite, color: "FFFFFF", size: 11.5 });
  flowArrowH(s, 7.8, dy + dh / 2, 8.1);
  flowNode(s, 8.1, dy, 2.4, dh, "Instruction Compare\n+ Taint Check", { fill: C.paperSoft, size: 11 });
  flowArrowH(s, 10.5, dy + dh / 2, 10.8);
  flowNode(s, 10.8, dy, 1.9, dh, "Bug Report", { fill: C.copper, color: "FFFFFF", size: 12 });

  const rows = [
    ["Instruction-level tracing", "Every DUT writeback is tagged with an instruction ID and re-serialized in program order — not lockstep, immune to out-of-order retirement."],
    ["Synchronization-aware scheduling", "The GRM is advanced using a policy that understands barriers, avoiding the false mismatches classic lockstep testing produces (Figure 4)."],
    ["Taint-aware checking", "Memory operations that are genuinely non-deterministic (racy, unsynchronized) get deferred to a dedicated memory-consistency validator instead of being force-compared to one expected value."],
    ["Instruction-granular localization", "Because checking happens after every committed instruction, a mismatch points at the exact offending instruction — not just “somewhere in this test.”"],
  ];
  let y = 2.95;
  rows.forEach((r) => {
    s.addShape("roundRect", { x: 0.6, y, w: 12.1, h: 0.85, rectRadius: 0.06, fill: { color: C.paperSoft }, line: { type: "none" } });
    s.addText(r[0], { x: 0.85, y: y, w: 3.5, h: 0.85, fontFace: FONT_BODY, fontSize: 13, bold: true, color: C.copperDeep, valign: "middle" });
    s.addText(r[1], { x: 4.5, y: y, w: 8.0, h: 0.85, fontFace: FONT_BODY, fontSize: 12, color: C.ink, valign: "middle" });
    y += 1.0;
  });
  s.addText("Output: a structured bug report — offending instruction, warp/thread, and CVE/CWE classification when applicable.", {
    x: 0.6, y: y + 0.02, w: 12.1, h: 0.4, fontFace: FONT_BODY, fontSize: 12.5, italic: true, bold: true, color: C.copperDeep,
  });
  slideNum(s, 11);
}

// =====================================================================
// SLIDE 12 — Results: headline numbers
// =====================================================================
{
  const s = pres.addSlide();
  eyebrow(s, "Results");
  pageTitle(s, "20 new bugs, 10 with CVE IDs");

  const stats = [["20", "new bugs found"], ["17 / 3", "RTL bugs / ISS bugs"], ["10", "assigned CVE IDs"], ["2× / 28×", "coverage / effective throughput vs. baselines"]];
  const sw = 2.95, sgx = 0.15;
  stats.forEach((st, i) => {
    const x = 0.6 + i * (sw + sgx);
    s.addShape("roundRect", { x, y: 1.95, w: sw, h: 1.75, rectRadius: 0.08, fill: { color: C.paperSoft }, line: { type: "none" },
      shadow: { type: "outer", color: "000000", opacity: 0.08, blur: 6, offset: 2, angle: 90 } });
    statCallout(s, x + 0.25, 2.15, sw - 0.5, st[0], st[1], { numSize: 32 });
  });

  bulletBlock(s, [
    { text: "Found on two independent, unrelated open-source GPUs (Vortex and OpenGPGPU) plus their reference ISS/Spike models — not one codebase's quirks." },
    { text: "Ablation confirms each generator piece matters: removing the generation stack alone drops median instruction completion from 97.3% to 5.6%." },
    { text: "Disabling taint propagation during interleaved warp generation cuts expression coverage from 59% to 41% — cross-warp race modeling measurably finds more of the design.", bold: true },
  ], { x: 0.6, y: 4.1, w: 12.1, h: 2.9 });
  slideNum(s, 12);
}

// =====================================================================
// SLIDE 13 — Results: full bug table
// =====================================================================
{
  const s = pres.addSlide();
  eyebrow(s, "Results");
  pageTitle(s, "Every bug FuzzGPU found");

  const tw = 8.5, th = tw / 1.871, tx = (PGW - tw) / 2, ty = 1.95;
  imageCard(s, img("table3_bugs.png"), tx, ty, tw, th, {});
  s.addText("Table 3 — Summary of discovered bugs on Vortex, OpenGPGPU, and their reference ISS/Spike simulators", {
    x: tx, y: ty + th + 0.22, w: tw, h: 0.35, fontFace: FONT_BODY, fontSize: 10.5, italic: true, color: C.inkSoft, align: "center",
  });
  slideNum(s, 13);
}

// =====================================================================
// SLIDE 14 — Conclusion / takeaways
// =====================================================================
{
  const s = pres.addSlide();
  s.background = { color: C.graphite };
  eyebrow(s, "Conclusion", { color: C.copper });
  pageTitle(s, "Why this matters", { color: "FFFFFF" });

  bulletBlock(s, [
    { text: "GPUs are ubiquitous and their bugs are permanent — yet hardware fuzzing had never targeted the GPU execution model itself.", },
    { text: "FuzzGPU shows that valid, intricate SIMT test programs can be generated systematically — the generation stack + barrier planner make divergence and synchronization first-class citizens of the generator, not accidents to work around." },
    { text: "Taint-aware, instruction-level differential testing turns GPU non-determinism from a source of false positives into something explicitly modeled and safely deferred." },
    { text: "Result: 20 real, previously-unknown bugs — 10 with CVE IDs — across two independent open-source GPUs, with markedly higher coverage and throughput than adapted CPU fuzzers.", bold: true, color: C.copper },
  ], { x: 0.6, y: 2.1, w: 11.6, h: 4.2, color: "FFFFFF" });

  s.addText("Fuzzing Open-Source GPU Hardware with SIMT Program Generation  ·  USENIX Security 2026", {
    x: 0.6, y: 6.7, w: 11.6, h: 0.4, fontFace: FONT_BODY, fontSize: 12, italic: true, color: C.steelLight,
  });
}

pres.writeFile({ fileName: `${ASSETS}/fuzzgpu_presentation.pptx` }).then(() => {
  console.log("done");
});
