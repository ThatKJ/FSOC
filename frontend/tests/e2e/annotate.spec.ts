import { test, expect, type Page } from "@playwright/test";
import { readFileSync } from "node:fs";
import path from "node:path";

/**
 * /mission/annotate G2 review: covers concrete risks in the save-validation and
 * per-frame draft-reset logic (route-mocked -- no real recording needed in CI).
 * Not a re-check of the already-verified live-camera manifest reader.
 */

const RECORDING_ID = "1234567890000";

const MANIFEST = {
  recordingId: RECORDING_ID,
  sessionId: "session-xyz",
  startedAtEpochMs: 1_700_000_000_000,
  endedAtEpochMs: 1_700_000_060_000,
  recordedFrameCount: 3,
  errorCount: 0,
  eventCount: 0,
  calibrationStatus: "UNCALIBRATED",
  perceptionMode: "CLASSICAL",
  rawWidthPx: 640,
  rawHeightPx: 480,
  cliArgs: "--source camera --camera-index 0 --uncalibrated",
  softwareCommit: "deadbeef",
};

const TELEMETRY = [
  { frameIndex: 0, timestampS: 0.0, targetDetected: true, detectedXPx: 100, detectedYPx: 100 },
  { frameIndex: 1, timestampS: 0.04, targetDetected: false, detectedXPx: null, detectedYPx: null },
  { frameIndex: 2, timestampS: 0.08, targetDetected: true, detectedXPx: 200, detectedYPx: 150 },
];

// A real decodable image is required here (unlike live-camera.spec.ts, which only
// checks blob-URL identity): this page reads the clicked image's rendered bounding
// box to convert a click into raw pixel coordinates, which needs the <img> to
// actually decode and lay out at a real size. Content-type is still served as
// image/jpeg to match the real frame route -- browsers sniff actual image bytes
// for <img> decoding regardless of the declared MIME type.
const FAKE_JPEG_BYTES = readFileSync(path.join(__dirname, "..", "..", "public", "demo", "frame-static.png"));

async function mockEmptyRecordingList(page: Page) {
  await page.route("**/api/real-sessions", (route) =>
    route.fulfill({ status: 200, contentType: "application/json", body: "[]" }),
  );
}

async function mockOneRecording(page: Page) {
  await page.route("**/api/real-sessions", (route) => {
    if (route.request().method() !== "GET" || !route.request().url().endsWith("/api/real-sessions")) {
      return route.fallback();
    }
    return route.fulfill({ status: 200, contentType: "application/json", body: JSON.stringify([MANIFEST]) });
  });
  await page.route(`**/api/real-sessions/${RECORDING_ID}`, (route) =>
    route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({ manifest: MANIFEST, telemetry: TELEMETRY }),
    }),
  );
  await page.route(`**/api/real-sessions/${RECORDING_ID}/frame/**`, (route) =>
    route.fulfill({ status: 200, contentType: "image/jpeg", body: FAKE_JPEG_BYTES }),
  );

  let savedLabels: Record<string, unknown> = {};
  await page.route(`**/api/real-sessions/${RECORDING_ID}/labels`, async (route) => {
    if (route.request().method() === "GET") {
      return route.fulfill({
        status: 200,
        contentType: "application/json",
        body: JSON.stringify({
          schemaVersion: 1,
          recordingId: RECORDING_ID,
          sessionId: MANIFEST.sessionId,
          labelCoordinateSpace: "raw",
          labels: savedLabels,
        }),
      });
    }
    const body = route.request().postDataJSON() as {
      frameIndex: number;
      presence: string;
      centerXPx: number | null;
      centerYPx: number | null;
    };
    savedLabels = {
      ...savedLabels,
      [String(body.frameIndex)]: { ...body, reviewed: true, labeledAtEpochMs: Date.now() },
    };
    return route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({
        schemaVersion: 1,
        recordingId: RECORDING_ID,
        sessionId: MANIFEST.sessionId,
        labelCoordinateSpace: "raw",
        labels: savedLabels,
      }),
    });
  });
}

test("no recordings shows the honest empty state, with a pointer to /mission/live", async ({ page }) => {
  await mockEmptyRecordingList(page);
  await page.goto("/mission/annotate");
  await expect(page.getByText(/No recordings found/)).toBeVisible();
});

test("saving Present without clicking the image is rejected, not silently accepted", async ({ page }) => {
  await mockOneRecording(page);
  await page.goto("/mission/annotate");
  await page.getByText(RECORDING_ID).click();
  await expect(page.getByText("Frame 0 / 2")).toBeVisible();

  await page.getByRole("radio").first().check(); // "Present" is the first option
  await page.getByRole("button", { name: "Save Label" }).click();
  await expect(page.getByText(/click the image to set the beacon center/)).toBeVisible();
});

test("clicking the image then saving Present persists a reviewed label", async ({ page }) => {
  await mockOneRecording(page);
  await page.goto("/mission/annotate");
  await page.getByText(RECORDING_ID).click();
  await expect(page.getByText("Frame 0 / 2")).toBeVisible();

  const img = page.locator('img[alt="Recorded frame 0"]');
  await img.click({ position: { x: 50, y: 50 } });
  await page.getByRole("radio").first().check();
  await page.getByRole("button", { name: "Save Label" }).click();

  await expect(page.getByText("saved")).toBeVisible();
  await expect(page.getByText("already reviewed: present")).toBeVisible();
});

test("navigating to a new frame does not carry over the previous frame's unsaved draft", async ({ page }) => {
  await mockOneRecording(page);
  await page.goto("/mission/annotate");
  await page.getByText(RECORDING_ID).click();
  await expect(page.getByText("Frame 0 / 2")).toBeVisible();

  const img = page.locator('img[alt="Recorded frame 0"]');
  await img.click({ position: { x: 50, y: 50 } });
  await page.getByRole("radio").first().check();
  await page.getByRole("button", { name: "Save Label" }).click();
  await expect(page.getByText("already reviewed: present")).toBeVisible();

  await page.getByRole("button", { name: "Next →" }).click();
  await expect(page.getByText("Frame 1 / 2")).toBeVisible();
  // Frame 1 was never labeled -- must not inherit frame 0's "already reviewed" text
  // or its checked radio button.
  await expect(page.getByText(/already reviewed/)).not.toBeVisible();
  for (const radio of await page.getByRole("radio").all()) {
    await expect(radio).not.toBeChecked();
  }
});
