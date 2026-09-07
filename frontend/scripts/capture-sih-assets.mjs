// FSOC — SIH judge-facing evidence pack capture script.
//
// Drives the REAL Mission Control frontend (production build, ENGINE mode —
// the real fsoc_demo C++ binary, streaming real telemetry) with Playwright,
// and captures deterministic PNG evidence into presentation_assets/.
//
// The only liberty taken beyond plain UI interaction: for scenarios that need
// Hybrid perception / the state estimator, this script intercepts the
// browser's own request to the app's real /api/simulation/:scenario route and
// appends &mode=hybrid&tracker=1 to the query string. The app's server-side
// route (frontend/app/api/simulation/[scenario]/route.ts) already accepts and
// forwards these exact params to the real C++ engine — the frontend simply
// has no UI control wired to them yet (SIH MVP V2 shipped `--mode`/`--tracker`
// at the CLI/API level; a UI toggle was deliberately deferred to avoid an
// "engineering debug wall" — see the P0-v2 session notes). This script does
// not fabricate, replace, or post-process any telemetry value; it only
// chooses which REAL engine invocation the page asks for, exactly the way a
// future UI control would.
//
// Usage:
//   cd frontend && npm run build && npm run start &   # server on :4317
//   node scripts/capture-sih-assets.mjs
//
// Requires the C++ engine to be built (build/debug/fsoc_demo) for ENGINE mode.

import { chromium } from "@playwright/test";
import { mkdirSync, writeFileSync, copyFileSync, existsSync, readFileSync } from "node:fs";
import path from "node:path";
import os from "node:os";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(__dirname, "../..");
const OUT = path.resolve(repoRoot, "presentation_assets");
const SHOTS = path.join(OUT, "screenshots");
const METRICS = path.join(OUT, "metrics");
const DIAGRAMS = path.join(OUT, "diagrams");
const TERMINAL = path.join(OUT, "terminal");
const BASE_URL = process.env.FSOC_BASE_URL ?? "http://localhost:4317";
const VIEWPORT = { width: 1920, height: 1080 };

for (const d of [SHOTS, METRICS, DIAGRAMS, TERMINAL]) mkdirSync(d, { recursive: true });

const manifest = [];
function record(entry) {
  manifest.push(entry);
  console.log(`captured: ${entry.filename}`);
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

/** Intercept the app's OWN /api/simulation/:scenario requests and append real,
 * already-supported query params. Never touches the response. */
async function withEngineParams(page, extraParams) {
  await page.route("**/api/simulation/**", async (route) => {
    const url = new URL(route.request().url());
    for (const [k, v] of Object.entries(extraParams)) url.searchParams.set(k, v);
    await route.continue({ url: url.toString() });
  });
}

async function selectScenario(page, label) {
  await page.getByRole("button", { name: /Active scenario/i }).click();
  await page.getByRole("option", { name: new RegExp(label, "i") }).click();
}

async function selectEngineSource(page) {
  const btn = page.getByRole("button", { name: /^engine$/i });
  if (await btn.count()) await btn.first().click();
}

async function waitForRealTelemetry(page, timeout = 20000) {
  await page.waitForFunction(
    () => {
      const el = document.querySelector('[data-testid="tracking-feed"]');
      return el && el.getAttribute("data-tracking-state") != null;
    },
    { timeout },
  );
  // let the fetch settle and the first frame paint
  await page.waitForTimeout(600);
}

/** Pages without the TrackingFeed component (e.g. /telemetry) -- wait for the
 * transport slider (present on every telemetry-consuming screen) instead. */
async function waitForTelemetryPageReady(page, sliderLabel = "Simulation timeline", timeout = 20000) {
  await page.locator(`[role="slider"][aria-label="${sliderLabel}"]`).first().waitFor({ state: "visible", timeout });
  await page.waitForTimeout(600);
}

/** Seek the transport slider to a 0..1 ratio of the loaded run. */
async function seekToRatio(page, ratio, sliderLabel = "Timeline") {
  const slider = page.locator(`[role="slider"][aria-label="${sliderLabel}"]`).first();
  const box = await slider.boundingBox();
  if (!box) throw new Error(`slider "${sliderLabel}" not found/visible`);
  const x = box.x + Math.max(2, Math.min(box.width - 2, ratio * box.width));
  const y = box.y + box.height / 2;
  await page.mouse.click(x, y);
  await page.waitForTimeout(250);
}

async function readTrackingState(page) {
  return page.locator('[data-testid="tracking-feed"]').getAttribute("data-tracking-state");
}

async function readEstimatorPanelText(page) {
  const heading = page.getByText("STATE ESTIMATOR (P0-v2)", { exact: true });
  if ((await heading.count()) === 0) return null;
  return heading.locator("xpath=..").first().innerText();
}

/** Step through the loaded run frame-ratio-wise looking for a lock-state
 * substring in the estimator panel. Returns the ratio it found it at, or null. */
async function findRatioWithLockState(page, needle, { steps = 40, sliderLabel = "Timeline" } = {}) {
  for (let i = 0; i <= steps; i++) {
    const ratio = i / steps;
    await seekToRatio(page, ratio, sliderLabel);
    const text = await readEstimatorPanelText(page);
    if (text && text.includes(needle)) return ratio;
  }
  return null;
}

// page.setContent() leaves the page on an about:blank origin, which Chromium
// refuses to load file:// <img> sources from (cross-origin file access is
// blocked). The collage pages embed real screenshots via file:// paths, so
// they need an actual file:// document -- write to a temp .html and goto it.
let tmpHtmlCounter = 0;
async function gotoHtml(page, html) {
  const p = path.join(os.tmpdir(), `fsoc-capture-${process.pid}-${tmpHtmlCounter++}.html`);
  writeFileSync(p, html, "utf8");
  await page.goto("file://" + p);
}

async function shot(page, filename, opts = {}) {
  await page.screenshot({ path: path.join(SHOTS, filename), type: "png", ...opts });
}

// ---------------------------------------------------------------------------
// HTML-graphic pages (metrics / diagrams / terminal renders) — real data only
// ---------------------------------------------------------------------------

const PAGE_CSS = `
  * { box-sizing: border-box; }
  body { margin:0; width:1920px; height:1080px; background:#0b0f14; color:#e6edf3;
         font-family: 'SF Mono', 'JetBrains Mono', ui-monospace, monospace; }
  .wrap { padding:64px; height:100%; display:flex; flex-direction:column; }
  h1 { font-size:40px; margin:0 0 8px; letter-spacing:0.04em; text-transform:uppercase; color:#7ee787; }
  h2 { font-size:20px; margin:0 0 40px; color:#8b949e; font-weight:400; }
  table { border-collapse:collapse; width:100%; font-size:22px; }
  th, td { text-align:left; padding:14px 20px; border-bottom:1px solid #21262d; }
  th { color:#8b949e; text-transform:uppercase; font-size:15px; letter-spacing:0.05em; }
  td.num { text-align:right; font-variant-numeric:tabular-nums; color:#79c0ff; font-weight:600; }
  td.good { color:#7ee787; }
  .badge { display:inline-block; padding:4px 12px; border:1px solid #30363d; border-radius:4px;
           font-size:14px; color:#f0883e; margin-bottom:24px; letter-spacing:0.05em; }
  .foot { margin-top:auto; padding-top:24px; border-top:1px solid #21262d; font-size:15px; color:#6e7681; }
`;

function metricsSummaryHtml() {
  return `<!doctype html><html><head><meta charset="utf-8"><style>${PAGE_CSS}
    .grid { display:grid; grid-template-columns: repeat(3, 1fr); gap:24px; margin-top:16px; }
    .card { border:1px solid #21262d; border-radius:8px; padding:28px; background:#0d1117; }
    .card .v { font-size:44px; font-weight:700; color:#7ee787; margin:8px 0; }
    .card .l { font-size:15px; color:#8b949e; text-transform:uppercase; letter-spacing:0.04em; }
    .card .s { font-size:14px; color:#6e7681; }
  </style></head><body><div class="wrap">
    <span class="badge">SIMULATION / DEVELOPMENT-MACHINE RESULTS</span>
    <h1>FSOC — Measured Results</h1>
    <h2>Every number below is from a committed, deterministic tool in this repository. docs/MVP_METRICS.md · docs/MVP_ABLATION.md</h2>
    <div class="grid">
      <div class="card"><div class="l">Step-10 Baseline Acceptance</div><div class="v">7 / 7</div><div class="s">scenarios PASS</div></div>
      <div class="card"><div class="l">C++ Test Suites</div><div class="v">17 / 17</div><div class="s">ctest --preset debug</div></div>
      <div class="card"><div class="l">Frontend E2E Tests</div><div class="v">20 / 20</div><div class="s">Playwright</div></div>
      <div class="card"><div class="l">Severe Outliers — Classical</div><div class="v" style="color:#f85149">2,240</div><div class="s">&gt;50px, closed-loop, Stage-4</div></div>
      <div class="card"><div class="l">Severe Outliers — Classical+Tracker</div><div class="v">12</div><div class="s">↓ ~99.5% reduction</div></div>
      <div class="card"><div class="l">Severe Outliers — Hybrid</div><div class="v" style="color:#f85149">1,808</div><div class="s">&gt;50px, closed-loop, Stage-4</div></div>
      <div class="card"><div class="l">Severe Outliers — Hybrid+Tracker (V2)</div><div class="v">9</div><div class="s">↓ ~99.5% reduction</div></div>
      <div class="card"><div class="l">AI Inference Latency (P95)</div><div class="v">1.27<span style="font-size:22px">ms</span></div><div class="s">C++ ONNX, this machine</div></div>
      <div class="card"><div class="l">Full-Step Latency, Hybrid+Tracker (P95)</div><div class="v">~1.2<span style="font-size:22px">ms</span></div><div class="s">vs. 20ms / 50Hz budget</div></div>
    </div>
    <div class="foot">Commit ${process.env.FSOC_COMMIT_SHA ?? ""} · ${new Date().toISOString().slice(0,10)} · Apple M5, macOS · Simulation only, no physical hardware</div>
  </div></body></html>`;
}

function ablationHtml() {
  return `<!doctype html><html><head><meta charset="utf-8"><style>${PAGE_CSS}</style></head><body><div class="wrap">
    <span class="badge">SIMULATION / DEVELOPMENT-MACHINE RESULTS</span>
    <h1>FSOC — Ablation: Classical vs Hybrid vs Hybrid+Estimator</h1>
    <h2>Full frozen Stage-4 protocol, 11 scenarios × 5 seeds × 8s @ 50Hz = 22,000 frames/config. docs/MVP_ABLATION.md</h2>
    <table>
      <tr><th>Configuration</th><th>Coverage</th><th class="num">Severe (&gt;50px) Outliers</th><th class="num">Max Error (px)</th></tr>
      <tr><td>A. Classical</td><td>99.69%</td><td class="num" style="color:#f85149">2,240</td><td class="num">581.4</td></tr>
      <tr><td>B. Classical + Tracker</td><td>77.51%</td><td class="num good">12</td><td class="num">127.5</td></tr>
      <tr><td>C. Hybrid (V1)</td><td>97.35%</td><td class="num" style="color:#f85149">1,808</td><td class="num">581.4</td></tr>
      <tr><td>D. Hybrid + Tracker (V2)</td><td>77.05%</td><td class="num good">9</td><td class="num">106.6</td></tr>
    </table>
    <div class="foot">A → B and C → D: adding the state estimator cuts severe outliers by ~99.5% in both cases — the estimator, not the Classical/AI fusion policy, neutralizes this failure mode. Real, disclosed coverage cost: ~20 points. A temporally coherent moving distractor still defeats this mitigation completely (docs/MVP_ABLATION.md §6) — not claimed solved.</div>
  </div></body></html>`;
}

function architectureHtml() {
  return `<!doctype html><html><head><meta charset="utf-8"><style>${PAGE_CSS}
    .flow { display:flex; align-items:center; gap:0; margin-top:24px; flex-wrap:wrap; }
    .box { border:2px solid #30363d; border-radius:8px; padding:20px 22px; background:#0d1117; min-width:190px; text-align:center; }
    .box .t { font-size:18px; font-weight:700; color:#e6edf3; }
    .box .s { font-size:13px; color:#8b949e; margin-top:6px; }
    .arrow { font-size:28px; color:#6e7681; padding:0 14px; }
    .row { display:flex; align-items:center; }
    .fuse { border-color:#7ee787; }
    .hw { border:2px dashed #6e7681; opacity:0.55; }
    .sec-label { font-size:14px; text-transform:uppercase; letter-spacing:0.06em; color:#6e7681; margin:36px 0 4px; }
  </style></head><body><div class="wrap">
    <h1>FSOC — System Architecture</h1>
    <h2>Implemented modules only. Every box below is real, tested C++20 code.</h2>
    <div class="sec-label">Simulation environment — this repository, today</div>
    <div class="flow">
      <div class="row"><div class="box"><div class="t">Trajectory /<br/>Environment</div></div><div class="arrow">→</div></div>
      <div class="row"><div class="box"><div class="t">Synthetic<br/>Camera</div></div><div class="arrow">→</div></div>
      <div class="row"><div class="box fuse"><div class="t">Classical CV +<br/>TinyBeaconNet</div><div class="s">independent proposals</div></div><div class="arrow">→</div></div>
      <div class="row"><div class="box fuse"><div class="t">Safe Hybrid<br/>Fusion</div><div class="s">ADR-018</div></div><div class="arrow">→</div></div>
      <div class="row"><div class="box"><div class="t">TargetTracker</div><div class="s">alpha-beta + temporal gate, ADR-019</div></div><div class="arrow">→</div></div>
    </div>
    <div class="flow" style="margin-top:20px;">
      <div class="row"><div class="box"><div class="t">Control Safety</div><div class="s">is_safe_to_steer</div></div><div class="arrow">→</div></div>
      <div class="row"><div class="box"><div class="t">PID<br/>Controller</div></div><div class="arrow">→</div></div>
      <div class="row"><div class="box"><div class="t">Pan / Tilt<br/>Actuator</div></div><div class="arrow">↺</div></div>
    </div>
    <div class="sec-label">Future hardware interface — NOT built, not claimed</div>
    <div class="flow">
      <div class="row"><div class="box hw"><div class="t">Real camera /<br/>frame grabber</div></div></div>
      <div class="row" style="margin-left:32px;"><div class="box hw"><div class="t">Real servo /<br/>gimbal driver</div></div></div>
    </div>
    <div class="foot">docs/09_FUTURE_ARCHITECTURE.md · Simulation only — no physical camera, beacon, or pan/tilt hardware exists in this repository.</div>
  </div></body></html>`;
}

function systemStoryHtml() {
  const blocks = [
    ["SEE", "Camera + Classical/AI Hybrid Perception"],
    ["ESTIMATE", "Position + Velocity (alpha-beta filter)"],
    ["PREDICT", "Bridge Brief Detection Gaps"],
    ["CORRECT", "PID Pan/Tilt Command"],
  ];
  return `<!doctype html><html><head><meta charset="utf-8"><style>${PAGE_CSS}
    .story { display:flex; align-items:center; justify-content:center; gap:0; flex:1; }
    .sbox { border:2px solid #30363d; border-radius:12px; padding:48px 36px; background:#0d1117; width:340px; text-align:center; }
    .sbox .t { font-size:34px; font-weight:800; letter-spacing:0.06em; color:#7ee787; }
    .sbox .s { font-size:17px; color:#8b949e; margin-top:16px; line-height:1.5; }
    .sarrow { font-size:40px; color:#6e7681; padding:0 20px; }
  </style></head><body><div class="wrap">
    <h1 style="text-align:center;">FSOC — How It Works</h1>
    <h2 style="text-align:center;">A closed loop, running today in a deterministic C++ simulation</h2>
    <div class="story">
      ${blocks.map(([t, s], i) => `<div class="sbox"><div class="t">${t}</div><div class="s">${s}</div></div>${i < blocks.length-1 ? '<div class="sarrow">→</div>' : ''}`).join("")}
    </div>
    <div class="foot" style="text-align:center;">Every stage is real, tested C++20 code — not a scripted demo.</div>
  </div></body></html>`;
}

function terminalHtml(title, rawText) {
  const esc = rawText.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  return `<!doctype html><html><head><meta charset="utf-8"><style>
    * { box-sizing:border-box; }
    body { margin:0; width:1920px; min-height:1080px; background:#0b0f14; display:flex; align-items:flex-start; justify-content:center; padding:48px 0; font-family: ui-monospace, 'SF Mono', monospace; }
    .term { width:1720px; border-radius:10px; overflow:hidden; border:1px solid #30363d; box-shadow:0 20px 60px rgba(0,0,0,0.5); }
    .bar { background:#161b22; padding:14px 20px; display:flex; align-items:center; gap:8px; border-bottom:1px solid #21262d; }
    .dot { width:12px; height:12px; border-radius:50%; }
    .r{background:#ff5f56;} .y{background:#ffbd2e;} .g{background:#27c93f;}
    .title { margin-left:16px; color:#8b949e; font-size:15px; }
    pre { margin:0; padding:32px; background:#0d1117; color:#c9d1d9; font-size:17px; line-height:1.45; white-space:pre-wrap; }
    .ok { color:#7ee787; }
  </style></head><body>
    <div class="term">
      <div class="bar"><span class="dot r"></span><span class="dot y"></span><span class="dot g"></span><span class="title">${title}</span></div>
      <pre>${esc}</pre>
    </div>
  </body></html>`;
}

function collageHtml(title, items) {
  // items: [{src, label}]
  return `<!doctype html><html><head><meta charset="utf-8"><style>
    * { box-sizing:border-box; }
    body { margin:0; width:1920px; height:1080px; background:#0b0f14; font-family: ui-monospace, 'SF Mono', monospace; color:#e6edf3; }
    .wrap { padding:48px; height:100%; display:flex; flex-direction:column; }
    h1 { font-size:32px; margin:0 0 28px; text-transform:uppercase; letter-spacing:0.05em; color:#7ee787; }
    .row { display:flex; gap:28px; flex:1; }
    .cell { flex:1; display:flex; flex-direction:column; border:1px solid #21262d; border-radius:8px; overflow:hidden; background:#0d1117; }
    .cell img { width:100%; flex:1; object-fit:cover; display:block; }
    .cap { padding:14px 18px; font-size:18px; color:#8b949e; border-top:1px solid #21262d; text-transform:uppercase; letter-spacing:0.04em; }
  </style></head><body><div class="wrap">
    <h1>${title}</h1>
    <div class="row">
      ${items.map(it => `<div class="cell"><img src="${it.src}"><div class="cap">${it.label}</div></div>`).join("")}
    </div>
  </div></body></html>`;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

async function main() {
  const browser = await chromium.launch({ channel: "chrome", headless: true });
  const page = await browser.newPage({ viewport: VIEWPORT, deviceScaleFactor: 2 });

  // ---- 01 / 02 / 03: hero tracking + misalignment/converged pair ----
  await page.goto(`${BASE_URL}/mission`);
  await selectEngineSource(page);
  await selectScenario(page, "Static Acquisition");
  await waitForRealTelemetry(page);

  await seekToRatio(page, 0.02); // just past frame 0 — real, large initial error
  await shot(page, "02_initial_misalignment.png");
  record({ filename: "02_initial_misalignment.png", purpose: "Real initial off-axis error before PID convergence", scenario: "static", source: "ENGINE", environment: "Simulation" });

  await seekToRatio(page, 0.98); // converged
  await shot(page, "03_alignment_converged.png");
  record({ filename: "03_alignment_converged.png", purpose: "Same run, converged: pointing error near zero", scenario: "static", source: "ENGINE", environment: "Simulation" });

  await seekToRatio(page, 0.6);
  await shot(page, "01_mission_control_tracking.png");
  record({ filename: "01_mission_control_tracking.png", purpose: "Hero: Mission Control actively tracking, mid-run", scenario: "static", source: "ENGINE", environment: "Simulation" });

  // ---- 04: hybrid perception + state estimator, both real ----
  await withEngineParams(page, { mode: "hybrid", tracker: "1" });
  await page.goto(`${BASE_URL}/mission`);
  await selectEngineSource(page);
  await selectScenario(page, "Static Acquisition");
  await waitForRealTelemetry(page);
  const trackingRatio = await findRatioWithLockState(page, "TRACKING", { steps: 30 });
  await seekToRatio(page, trackingRatio ?? 0.5);
  await shot(page, "04_hybrid_perception.png");
  record({ filename: "04_hybrid_perception.png", purpose: "Real Hybrid fusion + alpha-beta state estimator panels, both populated by the live engine", scenario: "static", source: "ENGINE (mode=hybrid&tracker=1)", environment: "Simulation" });

  // ---- 05 / 06: occlusion/coasting + reacquisition (real target-loss scenario + tracker) ----
  await page.goto(`${BASE_URL}/mission`);
  await selectEngineSource(page);
  await selectScenario(page, "Target Loss");
  await waitForRealTelemetry(page);

  // A same-scenario "tracking" frame for the 3-panel failure-recovery
  // collage below (Phase 22 wants all three panels from one deterministic
  // run, not composited across different scenarios).
  const lossTrackingRatio = await findRatioWithLockState(page, "TRACKING", { steps: 20 });
  if (lossTrackingRatio != null) {
    await seekToRatio(page, lossTrackingRatio);
    await shot(page, "07_loss_scenario_tracking.png");
    record({ filename: "07_loss_scenario_tracking.png", purpose: "Same 'loss' run as 05/06, early TRACKING frame -- the 'before' panel for the failure-recovery collage", scenario: "loss", source: "ENGINE (mode=hybrid&tracker=1)", environment: "Simulation" });
  }

  const coastRatio = await findRatioWithLockState(page, "COASTING", { steps: 60 });
  if (coastRatio != null) {
    await seekToRatio(page, coastRatio);
    await shot(page, "05_occlusion_coasting.png");
    record({ filename: "05_occlusion_coasting.png", purpose: "Real brief detection gap bridged by the state estimator (COASTING, PREDICTED)", scenario: "loss", source: "ENGINE (mode=hybrid&tracker=1)", environment: "Simulation" });
  } else {
    record({ filename: null, purpose: "05_occlusion_coasting BLOCKED", caveat: "No COASTING frame found in this scenario/seed within the search budget — see report." });
  }
  // find TRACKING again *after* having been LOST at some point
  let reacqRatio = null;
  for (let i = 0; i <= 60; i++) {
    const ratio = i / 60;
    await seekToRatio(page, ratio);
    const ts = await readTrackingState(page);
    const est = await readEstimatorPanelText(page);
    if (ts === "TARGET_LOST") reacqRatio = "seen-lost";
    if (reacqRatio === "seen-lost" && ts === "TRACKING" && est && est.includes("TRACKING")) {
      reacqRatio = ratio;
      break;
    }
  }
  if (typeof reacqRatio === "number") {
    await seekToRatio(page, reacqRatio);
    await shot(page, "06_reacquisition.png");
    record({ filename: "06_reacquisition.png", purpose: "Real reacquisition after target loss: fresh TRACKING lock, valid perception source", scenario: "loss", source: "ENGINE (mode=hybrid&tracker=1)", environment: "Simulation" });
  } else {
    record({ filename: null, purpose: "06_reacquisition BLOCKED", caveat: "No post-loss reacquisition frame found within the search budget — see report." });
  }

  // ---- 08: error convergence chart (back to the plain classical baseline) ----
  await page.unroute("**/api/simulation/**");
  await page.goto(`${BASE_URL}/telemetry`);
  await selectEngineSource(page);
  await selectScenario(page, "Static Acquisition");
  await waitForTelemetryPageReady(page);
  await seekToRatio(page, 0.99, "Simulation timeline");
  await shot(page, "08_error_convergence.png");
  record({ filename: "08_error_convergence.png", purpose: "Full telemetry dashboard: real angular-error/pan-tilt/rate/detection charts, all high->low convergence", scenario: "static", source: "ENGINE", environment: "Simulation" });

  await browser.close();

  // ---- 09: real Step-9 visualizer frame (already exists, not a browser capture) ----
  const src9 = path.join(repoRoot, "frontend/public/demo/frame-static.png");
  if (existsSync(src9)) {
    copyFileSync(src9, path.join(SHOTS, "09_perception_frame.png"));
    record({ filename: "09_perception_frame.png", purpose: "Real rendered simulator frame (Step-9 visualizer output) — beacon + crosshair", scenario: "static", source: "Step-9 visualizer (deterministic replay of the C++ renderer)", environment: "Simulation" });
  }

  // ---- terminal captures (real, pre-captured program output) ----
  const browser2 = await chromium.launch({ channel: "chrome", headless: true });
  const tpage = await browser2.newPage({ viewport: VIEWPORT, deviceScaleFactor: 2 });

  const validationRaw = readTextIfExists("/tmp/terminal_validation_raw.txt");
  if (validationRaw) {
    await tpage.setContent(terminalHtml("ctest --preset debug  &&  step10_validation_smoke", validationRaw));
    await tpage.screenshot({ path: path.join(SHOTS, "12_validation_terminal.png"), fullPage: true });
    record({ filename: "12_validation_terminal.png", purpose: "Real captured terminal output: 17/17 CTest, 7/7 Step-10 PASS", scenario: "n/a", source: "real program stdout, styled (not retyped)", environment: "Simulation" });
  }
  const hybridRaw = readTextIfExists("/tmp/terminal_hybrid_raw.txt");
  if (hybridRaw) {
    await tpage.setContent(terminalHtml("fsoc_demo static --mode hybrid --tracker", hybridRaw));
    await tpage.screenshot({ path: path.join(SHOTS, "13_hybrid_cli_demo.png"), fullPage: true });
    record({ filename: "13_hybrid_cli_demo.png", purpose: "Real CLI run: Hybrid perception + tracker, live lock-state/source transitions", scenario: "static", source: "real program stdout, styled (not retyped)", environment: "Simulation" });
  }

  // ---- metrics / ablation / diagrams ----
  await tpage.setContent(metricsSummaryHtml());
  await tpage.screenshot({ path: path.join(METRICS, "10_metrics_summary.png") });
  record({ filename: "metrics/10_metrics_summary.png", purpose: "Consolidated real measured metrics", source: "docs/MVP_METRICS.md" });

  await tpage.setContent(ablationHtml());
  await tpage.screenshot({ path: path.join(METRICS, "11_ablation.png") });
  record({ filename: "metrics/11_ablation.png", purpose: "A/B/C ablation, real measured numbers", source: "docs/MVP_ABLATION.md" });

  await tpage.setContent(architectureHtml());
  await tpage.screenshot({ path: path.join(DIAGRAMS, "15_architecture.png") });
  record({ filename: "diagrams/15_architecture.png", purpose: "Implemented-only system architecture", source: "README.md architecture diagram" });

  await tpage.setContent(systemStoryHtml());
  await tpage.screenshot({ path: path.join(DIAGRAMS, "16_system_story.png") });
  record({ filename: "diagrams/16_system_story.png", purpose: "SEE/ESTIMATE/PREDICT/CORRECT judge-facing storytelling graphic", source: "current implementation" });

  // ---- collages (real screenshots only, composited via HTML <img>) ----
  const misPath = path.join(SHOTS, "02_initial_misalignment.png");
  const alPath = path.join(SHOTS, "03_alignment_converged.png");
  if (existsSync(misPath) && existsSync(alPath)) {
    await gotoHtml(tpage, collageHtml("Misaligned → Aligned", [
      { src: "file://" + misPath, label: "Initial misalignment" },
      { src: "file://" + alPath, label: "Converged" },
    ]));
    await tpage.screenshot({ path: path.join(DIAGRAMS, "18_before_after.png") });
    record({ filename: "diagrams/18_before_after.png", purpose: "Before/after collage from real captured screenshots", source: "02_initial_misalignment.png + 03_alignment_converged.png" });
  }
  const trackPath = path.join(SHOTS, "07_loss_scenario_tracking.png");
  const coastPath = path.join(SHOTS, "05_occlusion_coasting.png");
  const reacqPath = path.join(SHOTS, "06_reacquisition.png");
  if (existsSync(trackPath) && existsSync(coastPath) && existsSync(reacqPath)) {
    await gotoHtml(tpage, collageHtml("Tracking → Coasting → Reacquired", [
      { src: "file://" + trackPath, label: "Tracking" },
      { src: "file://" + coastPath, label: "Occluded / Coasting" },
      { src: "file://" + reacqPath, label: "Reacquired" },
    ]));
    await tpage.screenshot({ path: path.join(DIAGRAMS, "19_failure_recovery.png") });
    record({ filename: "diagrams/19_failure_recovery.png", purpose: "Failure-recovery collage from real captured screenshots, all three panels from the SAME loss-scenario run", source: "07_loss_scenario_tracking.png + 05_occlusion_coasting.png + 06_reacquisition.png" });
  } else {
    record({ filename: null, purpose: "19_failure_recovery BLOCKED", caveat: "One or more source frames (coasting/reacquisition) were not found — see report." });
  }

  await browser2.close();

  writeFileSync(path.join(OUT, "manifest.json"), JSON.stringify(manifest, null, 2));
  console.log(`\nWrote ${manifest.length} manifest entries to presentation_assets/manifest.json`);
}

function readTextIfExists(p) {
  try {
    return existsSync(p) ? readFileSync(p, "utf8") : null;
  } catch {
    return null;
  }
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
