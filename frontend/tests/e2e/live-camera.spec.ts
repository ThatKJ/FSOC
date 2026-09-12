import { test, expect, type Page } from "@playwright/test";

/**
 * /mission/live G1 review: the frontend's manifest-reader/session/staleness logic,
 * exercised via route interception (there is no real camera in CI -- see
 * docs/LIVE_DATA_AUDIT.md and PHONE_CAMERA_TEST_PLAN.md for the physical checklist
 * this complements, not replaces).
 *
 * Covers the concrete risks flagged for review, not a re-check of already-verified
 * work: reconnect (sessionId change) must drop the previous session's displayed
 * frame rather than silently keep it under a "LIVE" label; a missing/expired frame
 * (404) must not crash the page or show a mismatched image; the honest
 * no-session/stale states must render distinctly.
 */

const BASE_TELEMETRY = {
  schemaVersion: 1,
  timestampS: 1.0,
  dtS: 0.04, // -> PROCESSED FPS = 25.0
  cameraSource: "REAL_PHONE_CAMERA",
  actuatorType: "VIRTUAL",
  sourceKind: "OpenCVCamera",
  sourceBackend: "AVFoundation",
  sourceDescription: "index 0",
  rawWidthPx: 1280,
  rawHeightPx: 720,
  preprocessedWidthPx: 640,
  preprocessedHeightPx: 480,
  perceptionMode: "CLASSICAL",
  perceptionSource: "CLASSICAL",
  classicalDetected: true,
  aiCandidateDetected: false,
  aiPresenceProbability: null,
  targetDetected: true,
  detectedXPx: 320,
  detectedYPx: 240,
  pixelErrorXPx: 10,
  pixelErrorYPx: -5,
  panErrorDeg: null,
  tiltErrorDeg: null,
  totalErrorDeg: null,
  calibrationStatus: "UNCALIBRATED",
  lockState: "TRACKING",
  trackerConfidence: 0.9,
  isPrediction: false,
  controlEnabled: false,
  commandPanRateDegS: 0,
  commandTiltRateDegS: 0,
  virtualPanDeg: 0,
  virtualTiltDeg: 0,
  virtualPanSaturated: false,
  virtualTiltSaturated: false,
  recordingActive: false,
  recordingId: null as string | null,
  recordedFrameCount: 0,
  recordingErrorCount: 0,
};

/** A tiny non-empty byte buffer served as image/jpeg -- content need not decode
 *  cleanly (these tests assert on identity/reset behavior, not rendered pixels). */
const FAKE_JPEG_BYTES = Buffer.from([0xff, 0xd8, 0xff, 0xd9]);

/** Mocks both live-camera endpoints for one (sessionId, frameIndex) pair. Each
 *  call installs a fresh route (Playwright keeps the most recent handler). */
async function mockLiveSession(
  page: Page,
  opts: { sessionId: string; frameIndex: number; ageS?: number; extra?: Partial<typeof BASE_TELEMETRY> },
) {
  const frame = { ...BASE_TELEMETRY, ...opts.extra, sessionId: opts.sessionId, frameIndex: opts.frameIndex };
  const ageS = opts.ageS ?? 0.1;
  // Exact-match "**/api/live-camera" (no trailing "**") so this never also matches
  // "/api/live-camera/frame..." or "/api/live-camera/record" -- Playwright glob
  // patterns match the WHOLE url, so a bare suffix like this is already precise.
  await page.route("**/api/live-camera", (route) =>
    route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify({ frame, frameIndex: opts.frameIndex, ageS, stale: ageS > 3 }) }),
  );
  // "**" (not a literal "?frame=N") because Playwright glob syntax treats "?" as
  // "match any single character", not a literal question mark -- checking the real
  // query param inside the handler is the robust way to assert on it.
  await page.route("**/api/live-camera/frame**", (route) => {
    const url = new URL(route.request().url());
    if (Number(url.searchParams.get("frame")) !== opts.frameIndex) {
      return route.fulfill({ status: 404, contentType: "application/json", body: JSON.stringify({ error: "frame not found" }) });
    }
    return route.fulfill({ status: 200, contentType: "image/jpeg", body: FAKE_JPEG_BYTES });
  });
}

async function mockNoLiveSession(page: Page) {
  await page.route("**/api/live-camera", (route) =>
    route.fulfill({
      status: 503,
      contentType: "application/json",
      body: JSON.stringify({ error: "no live session", detail: "generated/live/manifest.json does not exist." }),
    }),
  );
}

test("no live session shows the honest empty state, never a fabricated reading", async ({ page }) => {
  await mockNoLiveSession(page);
  await page.goto("/mission/live");
  await expect(page.getByText("Real-Camera Mode")).toBeVisible();
  await expect(page.getByText(/manifest\.json does not exist/)).toBeVisible();
});

test("an active session displays telemetry, calibration status, and the session id", async ({ page }) => {
  await mockLiveSession(page, { sessionId: "session-aaa", frameIndex: 0 });
  await page.goto("/mission/live");
  await expect(page.getByText("session-aaa")).toBeVisible();
  await expect(page.getByText("UNCALIBRATED (pixel-only)")).toBeVisible();
  await expect(page.locator('img[alt="Live camera frame"]')).toBeVisible();
});

test("reconnect (new sessionId) drops the previous session's displayed frame", async ({ page }) => {
  await mockLiveSession(page, { sessionId: "session-aaa", frameIndex: 0 });
  await page.goto("/mission/live");
  await expect(page.getByText("session-aaa")).toBeVisible();

  const imgLocator = page.locator('img[alt="Live camera frame"]');
  await expect(imgLocator).toBeVisible();
  const firstSrc = await imgLocator.getAttribute("src");
  expect(firstSrc).toMatch(/^blob:/);

  // Simulate fsoc_live being stopped and restarted: a brand new sessionId, frame
  // counter starts over at 0 again (a real reconnect's frame_index also restarts).
  await mockLiveSession(page, { sessionId: "session-bbb", frameIndex: 0 });

  await expect(page.getByText("session-bbb")).toBeVisible({ timeout: 5_000 });
  await expect(page.getByText("session-aaa")).not.toBeVisible();

  // The image must be re-fetched under the new session, not silently kept from the
  // old one -- a fresh blob: URL is guaranteed distinct from the first even though
  // both requests returned the same bytes.
  await expect.poll(async () => imgLocator.getAttribute("src")).not.toBe(firstSrc);
  const secondSrc = await imgLocator.getAttribute("src");
  expect(secondSrc).toMatch(/^blob:/);
});

test("a pruned/missing frame (404) does not crash the page or show a mismatched image", async ({ page }) => {
  const frame = { ...BASE_TELEMETRY, sessionId: "session-ccc", frameIndex: 5 };
  await page.route("**/api/live-camera", (route) =>
    route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({ frame, frameIndex: 5, ageS: 0.1, stale: false }),
    }),
  );
  await page.route("**/api/live-camera/frame**", (route) =>
    route.fulfill({
      status: 404,
      contentType: "application/json",
      body: JSON.stringify({ error: "frame not found", detail: "already pruned" }),
    }),
  );

  const pageErrors: string[] = [];
  page.on("pageerror", (e) => pageErrors.push(String(e)));

  await page.goto("/mission/live");
  await expect(page.getByText("session-ccc")).toBeVisible();
  // Telemetry renders even though the image never arrived -- the two are fetched
  // independently once the frame index is known, and a missing image must not block
  // the (already-valid) telemetry display.
  await expect(page.locator('img[alt="Live camera frame"]')).toHaveCount(0);
  expect(pageErrors).toEqual([]);
});

test("stale telemetry (age > 3s) is labeled STALE, not shown as LIVE", async ({ page }) => {
  await mockLiveSession(page, { sessionId: "session-ddd", frameIndex: 0, ageS: 5.2 });
  await page.goto("/mission/live");
  await expect(page.getByText(/STALE \(5\.2s\)/)).toBeVisible();
  await expect(page.getByText("LIVE", { exact: true })).not.toBeVisible();
});

test("recording panel reflects an active recording from telemetry, not local optimistic state", async ({
  page,
}) => {
  await mockLiveSession(page, {
    sessionId: "session-eee",
    frameIndex: 0,
    extra: { recordingActive: true, recordingId: "1234567890", recordedFrameCount: 42, recordingErrorCount: 1 },
  });
  await page.goto("/mission/live");
  await expect(page.getByText("RECORDING (1234567890)")).toBeVisible();
  await expect(page.getByText("42 / 1")).toBeVisible();
  await expect(page.getByRole("button", { name: "Start Recording" })).toBeDisabled();
  await expect(page.getByRole("button", { name: "Stop Recording" })).toBeEnabled();
});
