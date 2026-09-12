"use client";

import { useCallback, useEffect, useMemo, useRef, useState } from "react";

import { Screen } from "@/components/shell/AppShell";
import { Panel, PanelHeader, KeyValueRow } from "@/components/ui/Panel";
import { fixed } from "@/lib/format";

/**
 * /mission/annotate — G2 real-session annotation tool.
 *
 * NOT one of the 9 fixed Stitch nav screens (same convention as /mission/live --
 * see STITCH_IMPLEMENTATION_MAP.md); reachable from the /mission/live page and by
 * direct link. Reviews a LOCAL recording written by fsoc_live's
 * RealSessionRecorder (generated/real_sessions/<recordingId>/) -- there is
 * deliberately no upload step; this reads local files a person can inspect
 * themselves.
 *
 * A detector "suggestion" shown here (from the recording's own telemetry.jsonl)
 * is never independent ground truth -- it is only ever a starting point a human
 * can accept, adjust, or ignore before hitting Save, which is the only action
 * that writes a reviewed label.
 */

type Presence = "present" | "absent" | "partial_occlusion" | "full_occlusion" | "ambiguous";
const PRESENCE_OPTIONS: { value: Presence; label: string; needsCenter: boolean }[] = [
  { value: "present", label: "Present", needsCenter: true },
  { value: "partial_occlusion", label: "Partial Occlusion", needsCenter: true },
  { value: "full_occlusion", label: "Full Occlusion", needsCenter: false },
  { value: "absent", label: "Absent", needsCenter: false },
  { value: "ambiguous", label: "Ambiguous", needsCenter: false },
];

interface RecordingSummary {
  recordingId: string;
  sessionId: string;
  startedAtEpochMs: number;
  endedAtEpochMs: number | null;
  recordedFrameCount: number;
  errorCount: number;
  eventCount: number;
  calibrationStatus: string;
  perceptionMode: string;
}

interface Manifest extends RecordingSummary {
  rawWidthPx: number;
  rawHeightPx: number;
  cliArgs: string;
  softwareCommit: string;
}

interface TelemetryFrame {
  frameIndex: number;
  timestampS: number;
  targetDetected: boolean;
  detectedXPx: number | null;
  detectedYPx: number | null;
}

interface FrameLabel {
  presence: Presence;
  centerXPx: number | null;
  centerYPx: number | null;
  reviewed: true;
  labeledAtEpochMs: number;
}

interface LabelsDoc {
  schemaVersion: number;
  recordingId: string;
  sessionId: string;
  labelCoordinateSpace: "raw";
  labels: Record<string, FrameLabel>;
}

export default function AnnotatePage() {
  const [recordings, setRecordings] = useState<RecordingSummary[] | null>(null);
  const [recordingId, setRecordingId] = useState<string | null>(null);
  const [manifest, setManifest] = useState<Manifest | null>(null);
  const [telemetry, setTelemetry] = useState<TelemetryFrame[]>([]);
  const [labelsDoc, setLabelsDoc] = useState<LabelsDoc | null>(null);
  // Position WITHIN the telemetry array, not a raw frame number: a recording's
  // frameIndex values are the camera's own continuous counter across the whole
  // fsoc_live session, so a recording started partway through often begins at a
  // large, non-zero frameIndex (e.g. 1000, not 0) -- navigating by array position
  // is correct regardless of what the first/last real frameIndex happens to be.
  const [cursor, setCursor] = useState(0);
  const [pendingPresence, setPendingPresence] = useState<Presence | null>(null);
  const [pendingCenter, setPendingCenter] = useState<{ x: number; y: number } | null>(null);
  const [status, setStatus] = useState<string | null>(null);
  const [loadError, setLoadError] = useState<string | null>(null);
  const imgRef = useRef<HTMLImageElement>(null);

  useEffect(() => {
    fetch("/api/real-sessions", { cache: "no-store" })
      .then((r) => r.json())
      .then(setRecordings)
      .catch((err) => setLoadError(String(err)));
  }, []);

  const loadRecording = useCallback((id: string) => {
    setRecordingId(id);
    setCursor(0);
    setLoadError(null);
    Promise.all([
      fetch(`/api/real-sessions/${id}`, { cache: "no-store" }).then((r) => r.json()),
      fetch(`/api/real-sessions/${id}/labels`, { cache: "no-store" }).then((r) => r.json()),
    ])
      .then(([detail, labels]) => {
        if (detail.error) throw new Error(detail.error);
        setManifest(detail.manifest);
        setTelemetry(detail.telemetry);
        setLabelsDoc(labels);
      })
      .catch((err) => setLoadError(String(err)));
  }, []);

  const currentTelemetry = telemetry[cursor] ?? null;
  const frameIndex = currentTelemetry?.frameIndex ?? 0;
  const existingLabel = labelsDoc?.labels[String(frameIndex)] ?? null;

  // Reset the pending (unsaved) label draft whenever the frame changes -- reload
  // from whatever was already reviewed for this frame, if anything.
  useEffect(() => {
    setPendingPresence(existingLabel?.presence ?? null);
    setPendingCenter(
      existingLabel?.centerXPx != null && existingLabel?.centerYPx != null
        ? { x: existingLabel.centerXPx, y: existingLabel.centerYPx }
        : null,
    );
    setStatus(null);
    // existingLabel is derived from labelsDoc + cursor; depending on it directly
    // would re-run this after every save, clobbering the just-saved draft.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [cursor, recordingId]);

  function handleImageClick(e: React.MouseEvent<HTMLImageElement>) {
    const img = imgRef.current;
    if (!img || !manifest) return;
    const rect = img.getBoundingClientRect();
    const scaleX = manifest.rawWidthPx / rect.width;
    const scaleY = manifest.rawHeightPx / rect.height;
    const x = (e.clientX - rect.left) * scaleX;
    const y = (e.clientY - rect.top) * scaleY;
    setPendingCenter({ x, y });
  }

  function acceptDetectorSuggestion() {
    if (currentTelemetry?.detectedXPx != null && currentTelemetry?.detectedYPx != null) {
      setPendingCenter({ x: currentTelemetry.detectedXPx, y: currentTelemetry.detectedYPx });
    }
  }

  async function save() {
    if (!recordingId || !pendingPresence) {
      setStatus("choose a presence value first");
      return;
    }
    const needsCenter = PRESENCE_OPTIONS.find((o) => o.value === pendingPresence)?.needsCenter;
    if (needsCenter && !pendingCenter) {
      setStatus("click the image to set the beacon center first");
      return;
    }
    setStatus("saving...");
    try {
      const res = await fetch(`/api/real-sessions/${recordingId}/labels`, {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({
          frameIndex,
          presence: pendingPresence,
          centerXPx: pendingCenter?.x ?? null,
          centerYPx: pendingCenter?.y ?? null,
        }),
      });
      const body = await res.json();
      if (!res.ok) {
        setStatus(body.error ?? "save failed");
        return;
      }
      setLabelsDoc(body);
      setStatus("saved");
    } catch (err) {
      setStatus(String(err));
    }
  }

  const reviewedCount = labelsDoc ? Object.keys(labelsDoc.labels).length : 0;
  const totalFrames = telemetry.length;

  const markers = useMemo(() => {
    if (!imgRef.current || !manifest) return null;
    const el = imgRef.current;
    // offsetLeft/offsetTop (relative to the `relative` container, the img's
    // offsetParent) account for the flex-centering gap when the image doesn't
    // fill its container -- getBoundingClientRect()-based scaling alone drops
    // that offset and puts the marker near the container's corner instead of on
    // the actual point (same bug fixed in /mission/live).
    const w = el.offsetWidth;
    const h = el.offsetHeight;
    const toCss = (x: number, y: number) => ({
      left: `${el.offsetLeft + (x / manifest.rawWidthPx) * w}px`,
      top: `${el.offsetTop + (y / manifest.rawHeightPx) * h}px`,
    });
    return { toCss };
  }, [manifest, cursor]); // eslint-disable-line react-hooks/exhaustive-deps

  return (
    <Screen pad>
      <div className="flex items-center justify-between border border-outline-variant bg-surface-container px-margin-md py-margin-sm">
        <span className="font-headline-sm text-headline-sm uppercase tracking-wider text-primary">
          Real-Session Annotation (G2)
        </span>
        <span className="font-label-xs text-label-xs uppercase tracking-widest text-on-surface-variant">
          Local recordings only — reviewed labels are not independent ground truth
          until saved by a human
        </span>
      </div>

      {loadError && (
        <Panel className="mt-margin-md p-margin-md text-error">{loadError}</Panel>
      )}

      {!recordingId && (
        <Panel className="mt-margin-md flex flex-col gap-margin-sm p-margin-lg">
          <span className="font-headline-sm text-headline-sm uppercase text-on-surface">
            Select a recording
          </span>
          {recordings === null && <span className="text-on-surface-variant">Loading…</span>}
          {recordings != null && recordings.length === 0 && (
            <span className="font-data-mono text-data-mono text-on-surface-variant">
              No recordings found under generated/real_sessions/. Start one from{" "}
              <code>/mission/live</code> (Start Recording).
            </span>
          )}
          <div className="flex flex-col gap-margin-sm">
            {recordings?.map((r) => (
              <button
                key={r.recordingId}
                type="button"
                onClick={() => loadRecording(r.recordingId)}
                className="flex items-center justify-between border border-outline-variant bg-surface px-margin-md py-margin-sm text-left hover:border-primary/60"
              >
                <span className="font-data-mono text-data-mono text-primary">{r.recordingId}</span>
                <span className="font-data-mono text-[11px] text-on-surface-variant">
                  {r.recordedFrameCount} frames · {r.calibrationStatus} · {r.perceptionMode}
                  {r.endedAtEpochMs == null ? " · UNCLEAN STOP" : ""}
                </span>
              </button>
            ))}
          </div>
        </Panel>
      )}

      {recordingId && manifest && (
        <div className="mt-margin-md flex flex-1 gap-margin-md overflow-hidden">
          <Panel className="flex flex-1 flex-col overflow-hidden">
            <PanelHeader
              title={`Frame ${cursor + 1} / ${totalFrames} (frameIndex ${frameIndex})`}
              right={
                <span className="font-data-mono text-[11px] text-on-surface-variant">
                  reviewed {reviewedCount} / {totalFrames}
                </span>
              }
            />
            <div className="relative flex flex-1 items-center justify-center overflow-hidden bg-black">
              {/* eslint-disable-next-line @next/next/no-img-element -- local recorded frame, clicked for annotation, not an optimizable static asset */}
              <img
                ref={imgRef}
                src={`/api/real-sessions/${recordingId}/frame/${frameIndex}`}
                alt={`Recorded frame ${frameIndex}`}
                onClick={handleImageClick}
                className="max-h-full max-w-full cursor-crosshair object-contain"
              />
              {markers && currentTelemetry?.detectedXPx != null && currentTelemetry?.detectedYPx != null && (
                <span
                  className="pointer-events-none absolute h-3 w-3 -translate-x-1/2 -translate-y-1/2 rounded-full border-2 border-tertiary"
                  style={markers.toCss(currentTelemetry.detectedXPx, currentTelemetry.detectedYPx)}
                  title="detector suggestion (not ground truth)"
                />
              )}
              {markers && pendingCenter && (
                <span
                  className="pointer-events-none absolute h-4 w-4 -translate-x-1/2 -translate-y-1/2 rounded-full border-2 border-primary"
                  style={markers.toCss(pendingCenter.x, pendingCenter.y)}
                  title="your label (unsaved until you click Save)"
                />
              )}
            </div>
            <div className="flex items-center justify-between border-t border-outline-variant bg-surface-container-low px-margin-md py-margin-sm">
              <div className="flex gap-margin-sm">
                <button
                  type="button"
                  disabled={cursor === 0}
                  onClick={() => setCursor((c) => Math.max(0, c - 1))}
                  className="border border-outline-variant px-margin-md py-unit text-on-surface disabled:opacity-40"
                >
                  ← Prev
                </button>
                <input
                  type="number"
                  min={1}
                  max={Math.max(totalFrames, 1)}
                  value={cursor + 1}
                  onChange={(e) =>
                    setCursor(Math.max(0, Math.min(totalFrames - 1, (Number(e.target.value) || 1) - 1)))
                  }
                  className="w-20 border border-outline-variant bg-surface px-margin-sm text-center font-data-mono text-on-surface"
                />
                <button
                  type="button"
                  disabled={cursor >= totalFrames - 1}
                  onClick={() => setCursor((c) => Math.min(totalFrames - 1, c + 1))}
                  className="border border-outline-variant px-margin-md py-unit text-on-surface disabled:opacity-40"
                >
                  Next →
                </button>
              </div>
              <span className="font-data-mono text-[11px] text-on-surface-variant">
                t={currentTelemetry ? fixed(currentTelemetry.timestampS, 2) : "—"}s
              </span>
            </div>
          </Panel>

          <aside className="flex w-[320px] shrink-0 flex-col gap-gutter overflow-y-auto bg-outline-variant">
            <div className="flex flex-col gap-margin-sm bg-surface-container-low p-margin-md">
              <span className="mb-unit font-label-xs text-label-xs text-primary">RECORDING</span>
              <KeyValueRow k="ID" v={manifest.recordingId} />
              <KeyValueRow k="RAW SIZE" v={`${manifest.rawWidthPx}x${manifest.rawHeightPx}`} />
              <KeyValueRow k="CALIBRATION" v={manifest.calibrationStatus} />
              <KeyValueRow k="MODE" v={manifest.perceptionMode} border={false} />
            </div>

            <div className="flex flex-col gap-margin-sm bg-surface-container-low p-margin-md">
              <span className="mb-unit font-label-xs text-label-xs text-on-surface-variant">
                DETECTOR SUGGESTION (not ground truth)
              </span>
              <KeyValueRow
                k="DETECTED"
                v={currentTelemetry?.targetDetected ? "yes" : "no"}
                tone={currentTelemetry?.targetDetected ? "primary" : "lost"}
              />
              <button
                type="button"
                disabled={!currentTelemetry?.targetDetected}
                onClick={acceptDetectorSuggestion}
                className="mt-margin-sm w-full border border-outline-variant py-unit text-on-surface disabled:opacity-40"
              >
                Accept suggestion as label center
              </button>
            </div>

            <div className="flex flex-col gap-margin-sm bg-surface-container-low p-margin-md">
              <span className="mb-unit font-label-xs text-label-xs text-secondary">LABEL THIS FRAME</span>
              <div className="flex flex-col gap-unit">
                {PRESENCE_OPTIONS.map((opt) => (
                  <label key={opt.value} className="flex items-center gap-margin-sm text-on-surface">
                    <input
                      type="radio"
                      name="presence"
                      checked={pendingPresence === opt.value}
                      onChange={() => {
                        setPendingPresence(opt.value);
                        if (!opt.needsCenter) setPendingCenter(null);
                      }}
                    />
                    <span className="font-data-mono text-data-mono">{opt.label}</span>
                  </label>
                ))}
              </div>
              <span className="pt-unit font-data-mono text-[10px] text-on-surface-variant">
                Present/Partial Occlusion require clicking the image to set a center
                first. Full Occlusion/Absent/Ambiguous never store a center.
              </span>
              <button
                type="button"
                onClick={save}
                className="mt-margin-sm w-full border border-primary py-margin-sm text-primary hover:bg-primary/10"
              >
                Save Label
              </button>
              {status && (
                <span className="font-data-mono text-[11px] text-on-surface-variant">{status}</span>
              )}
              {existingLabel && (
                <span className="font-data-mono text-[11px] text-secondary">
                  already reviewed: {existingLabel.presence}
                </span>
              )}
            </div>

            <button
              type="button"
              onClick={() => {
                setRecordingId(null);
                setManifest(null);
                setTelemetry([]);
                setLabelsDoc(null);
              }}
              className="border border-outline-variant py-margin-sm text-on-surface-variant hover:text-on-surface"
            >
              ← Choose a different recording
            </button>
          </aside>
        </div>
      )}
    </Screen>
  );
}
