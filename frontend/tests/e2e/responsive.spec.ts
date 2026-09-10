import { test, expect } from "@playwright/test";
import { mkdirSync } from "node:fs";
import { join } from "node:path";

/**
 * Deterministic responsive QA -- explicit Playwright viewport contexts, not
 * browser-extension window resize (unreliable in this environment: it
 * reports success without changing the rendered viewport). Screenshots are
 * QA artifacts, not evidence: saved under generated/qa/mobile/ (gitignored
 * via the root "generated/" rule), never committed.
 */

const VIEWPORTS = [
  { name: "375x812", width: 375, height: 812 },
  { name: "390x844", width: 390, height: 844 },
  { name: "430x932", width: 430, height: 932 },
  { name: "768x1024", width: 768, height: 1024 },
  { name: "1024x768", width: 1024, height: 768 },
  { name: "1440x900", width: 1440, height: 900 },
  { name: "1920x1080", width: 1920, height: 1080 },
];

const ALL_ROUTES = [
  "/",
  "/mission",
  "/mission/live",
  "/tracking",
  "/world",
  "/telemetry",
  "/scenarios",
  "/architecture",
  "/validation",
  "/benchmarks",
];

// The 3 pages the brief calls out as needing a full breakpoint sweep;
// every route still gets checked at the primary mobile target (390px) below.
const SWEEP_ROUTES = ["/", "/mission", "/mission/live"];

const SHOT_DIR = join(process.cwd(), "generated", "qa", "mobile");
mkdirSync(SHOT_DIR, { recursive: true });

async function assertNoUnintendedHorizontalOverflow(page: import("@playwright/test").Page) {
  const { scrollWidth, innerWidth } = await page.evaluate(() => ({
    scrollWidth: document.documentElement.scrollWidth,
    innerWidth: window.innerWidth,
  }));
  // 1px tolerance for sub-pixel rounding.
  expect(scrollWidth, "document.documentElement.scrollWidth should not exceed window.innerWidth").toBeLessThanOrEqual(
    innerWidth + 1,
  );
}

test.describe("responsive: breakpoint sweep (/ , /mission, /mission/live)", () => {
  for (const vp of VIEWPORTS) {
    for (const route of SWEEP_ROUTES) {
      test(`${route} @ ${vp.name}`, async ({ page }) => {
        await page.setViewportSize({ width: vp.width, height: vp.height });
        await page.goto(route, { waitUntil: "load" });
        await page.waitForTimeout(500); // let client-side telemetry hydrate
        await assertNoUnintendedHorizontalOverflow(page);
        const file = `${route === "/" ? "home" : route.replace(/\//g, "_").slice(1)}__${vp.name}.png`;
        await page.screenshot({ path: join(SHOT_DIR, file), fullPage: false });
      });
    }
  }
});

test.describe("responsive: all routes @ 390x844 (primary mobile target)", () => {
  for (const route of ALL_ROUTES) {
    test(`${route} @ 390x844`, async ({ page }) => {
      await page.setViewportSize({ width: 390, height: 844 });
      await page.goto(route, { waitUntil: "load" });
      await page.waitForTimeout(500);
      await assertNoUnintendedHorizontalOverflow(page);
      const file = `${route === "/" ? "home" : route.replace(/\//g, "_").slice(1)}__390x844.png`;
      await page.screenshot({ path: join(SHOT_DIR, file), fullPage: false });
    });
  }
});
