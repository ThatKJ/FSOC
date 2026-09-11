import { test, expect, type Page } from "@playwright/test";
import { readdirSync, readFileSync, statSync } from "node:fs";
import { join } from "node:path";

/** known-noisy console messages that are not app bugs */
const IGNORED_CONSOLE =
  /favicon|Failed to load resource|Support for defaultProps|defaultProps will be removed|Download the React DevTools|WebGL|SwiftShader|GroupMarkerNotSet|Automatic fallback to software WebGL/i;

const ROUTES = [
  { path: "/", label: "Overview" },
  { path: "/mission", label: "Mission Control" },
  { path: "/tracking", label: "Optical Tracking" },
  { path: "/world", label: "Spatial View" },
  { path: "/telemetry", label: "Telemetry" },
  { path: "/scenarios", label: "Scenarios" },
  { path: "/benchmarks", label: "Benchmarks" },
  { path: "/validation", label: "Validation" },
  { path: "/architecture", label: "Architecture" },
];

/**
 * Wait until the shared clock holds telemetry with EXACTLY the given frame count
 * (frames - 1 = the scrubber's aria-valuemax). Guards against acting on the
 * previous scenario's still-loaded frames.
 */
async function waitForFrames(page: Page, frames: number) {
  await expect
    .poll(
      async () => Number((await page.locator('[role="slider"]').first().getAttribute("aria-valuemax")) ?? 0),
      { timeout: 15_000, intervals: [100, 150, 200] },
    )
    .toBe(frames - 1);
  await page.waitForTimeout(150); // let the post-load RESET commit
}

async function noConsoleErrors(page: Page) {
  const errors: string[] = [];
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  page.on("pageerror", (e) => errors.push(String(e)));
  return () => errors.filter((e) => !IGNORED_CONSOLE.test(e));
}

test.describe("routes load", () => {
  for (const r of ROUTES) {
    test(`${r.label} (${r.path}) renders without crashing`, async ({ page }) => {
      const getErrors = await noConsoleErrors(page);
      await page.goto(r.path, { waitUntil: "networkidle" });
      await expect(page.locator("header")).toContainText("Alignment System");
      await expect(page.locator("nav a")).toHaveCount(9);
      // the shell must not be blank
      await expect(page.locator("main")).toBeVisible();
      expect(getErrors(), `console errors on ${r.path}`).toHaveLength(0);
    });
  }
});

test("navigation + active route state + back/forward", async ({ page }) => {
  await page.goto("/");
  await page.locator('nav a[aria-label="Telemetry"]').click();
  await expect(page).toHaveURL(/\/telemetry$/);
  await expect(page.locator('nav a[aria-label="Telemetry"]')).toHaveAttribute("aria-current", "page");
  await page.locator('nav a[aria-label="Validation"]').click();
  await expect(page).toHaveURL(/\/validation$/);
  await page.goBack();
  await expect(page).toHaveURL(/\/telemetry$/);
  await page.goForward();
  await expect(page).toHaveURL(/\/validation$/);
});

test("scenario selection switches telemetry", async ({ page }) => {
  await page.goto("/mission");
  await page.getByRole("button", { name: /Active scenario/i }).click();
  await page.getByRole("option", { name: /Target Loss/i }).click();
  await expect(page.getByRole("button", { name: /Active scenario/i })).toContainText(/Target Loss/i);
  // wait for the loss telemetry (400 frames) to load, then play — TARGET LOST must appear
  await waitForFrames(page, 400);
  await page.getByTestId("mini-transport").getByRole("button", { name: "Play" }).first().click();
  await expect(page.getByTestId("target-lost")).toBeVisible({ timeout: 20_000 });
});

test("playback: play / pause / reset + timeline advances", async ({ page }) => {
  await page.goto("/telemetry");
  await waitForFrames(page, 1000);
  const controls = page.getByTestId("playback-controls");
  const slider = controls.getByRole("slider");
  await expect(slider).toHaveAttribute("aria-valuenow", "0");
  await controls.getByRole("button", { name: "Play" }).click();
  await page.waitForTimeout(1200);
  const now = Number(await slider.getAttribute("aria-valuenow"));
  expect(now).toBeGreaterThan(0);
  await controls.getByRole("button", { name: "Pause" }).click();
  await page.waitForTimeout(400);
  const paused = Number(await slider.getAttribute("aria-valuenow"));
  await page.waitForTimeout(500);
  expect(Number(await slider.getAttribute("aria-valuenow"))).toBe(paused); // paused = no advance
  await controls.getByRole("button", { name: "Restart" }).click();
  await expect(slider).toHaveAttribute("aria-valuenow", "0");
});

test("tracking view: reticle present while TRACKING, hidden on TARGET_LOST", async ({ page }) => {
  await page.goto("/tracking");
  // default scenario (sinusoidal) is 100% detected -> reticle visible
  await expect(page.getByTestId("detection-reticle")).toBeVisible({ timeout: 10_000 });
  await expect(page.getByTestId("target-lost")).toHaveCount(0);

  // switch to loss and play -> a lost frame must hide the reticle
  await page.getByRole("button", { name: /Active scenario/i }).click();
  await page.getByRole("option", { name: /Target Loss/i }).click();
  await waitForFrames(page, 400);
  await page.getByTestId("playback-controls").getByRole("button", { name: "Play" }).click();
  await expect(page.getByTestId("target-lost")).toBeVisible({ timeout: 20_000 });
  await expect(page.getByTestId("detection-reticle")).toHaveCount(0);
  // natural reacquisition later in the run
  await expect(page.getByTestId("detection-reticle")).toBeVisible({ timeout: 25_000 });
});

test("open vs closed benchmark keeps the runs separate", async ({ page }) => {
  await page.goto("/benchmarks");
  await expect(page.getByText("57.4", { exact: true })).toBeVisible(); // open detection %
  await expect(page.getByText("6.45", { exact: true })).toBeVisible(); // open RMS deg
  await expect(page.getByText("0.55", { exact: true })).toBeVisible(); // closed RMS deg (0.5461)
  await expect(page.getByText(/11\.8x/)).toBeVisible();
  await expect(page.getByText("426 lost frames")).toBeVisible();
  await expect(page.getByTestId("timeseries-chart")).toBeVisible({ timeout: 15_000 });
});

test("validation shows the real Step-10 result (7/7, loss row is PASS)", async ({ page }) => {
  await page.goto("/validation");
  await expect(page.getByText("7 / 7 Scenarios Passed")).toBeVisible();
  const lossRow = page.locator("tr", { hasText: "Target Loss & Re-entry" });
  await expect(lossRow).toContainText("107");
  await expect(lossRow).toContainText("PASS");
});

test("world view renders (WebGL canvas or 2D fallback) and view switches", async ({ page }) => {
  await page.goto("/world");
  // either the R3F canvas or the 2D schematic fallback must be present — never a blank crash
  const canvasOrFallback = page.locator("canvas, svg");
  await expect(canvasOrFallback.first()).toBeVisible({ timeout: 15_000 });
  await expect(page.getByText("Target Telemetry")).toBeVisible();
  await page.getByRole("button", { name: "TOP ORTHO" }).click();
  await page.getByRole("button", { name: "CAMERA" }).click();
  await expect(page.getByRole("button", { name: "WORLD VIEW" })).toBeVisible();
});

test("invalid scenario API returns 400", async ({ request }) => {
  const r = await request.get("/api/simulation/not-a-scenario");
  expect(r.status()).toBe(400);
  const body = await r.json();
  expect(body.error).toMatch(/unknown scenario/i);
});

test("invalid perception mode API returns 400", async ({ request }) => {
  const r = await request.get("/api/simulation/static?mode=not-a-mode");
  expect(r.status()).toBe(400);
  const body = await r.json();
  expect(body.error).toMatch(/mode must be one of/i);
});

test("engine mode --tracker actually reaches the C++ engine (P0-v2)", async ({ request }) => {
  const r = await request.get("/api/simulation/static?source=engine&mode=hybrid&tracker=1");
  // The C++ binary may not be built in every environment this suite runs in;
  // skip rather than false-fail when the engine genuinely isn't available.
  if (r.status() !== 200) {
    test.skip(true, "local C++ engine not available in this environment");
    return;
  }
  const body = await r.json();
  expect(body.meta.source).toBe("engine");
  const last = body.frames.at(-1);
  // Regression guard: the engine subprocess's CWD must resolve the AI
  // model's relative path -- if it doesn't, fsoc_demo silently falls back to
  // CLASSICAL and this assertion catches it.
  expect(last.perception?.mode).toBe("HYBRID");
  expect(last.tracker).toBeTruthy();
  expect(["SEARCHING", "ACQUIRING", "TRACKING", "COASTING", "LOST"]).toContain(last.tracker.lockState);
});

test("no Math.random in application source (simulation state is never faked)", () => {
  // vendor libs (three.js) legitimately use Math.random for UUIDs; scope the
  // guard to OUR code — lib/, components/, app/.
  const roots = ["lib", "components", "app"].map((d) => join(process.cwd(), d));
  const offenders: string[] = [];
  const walk = (dir: string) => {
    for (const entry of readdirSync(dir)) {
      const p = join(dir, entry);
      if (statSync(p).isDirectory()) {
        walk(p);
      } else if (/\.(ts|tsx)$/.test(entry) && !/\.spec\./.test(entry)) {
        // match actual calls, not the words in a comment
        const src = readFileSync(p, "utf8").replace(/\/\/.*$/gm, "").replace(/\/\*[\s\S]*?\*\//g, "");
        if (/Math\s*\.\s*random\s*\(/.test(src)) offenders.push(p);
      }
    }
  };
  roots.forEach(walk);
  expect(offenders, `Math.random found in: ${offenders.join(", ")}`).toHaveLength(0);
});
