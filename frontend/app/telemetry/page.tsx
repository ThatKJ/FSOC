"use client";

import { useMemo } from "react";

import { Screen } from "@/components/shell/AppShell";
import { MetricStat } from "@/components/telemetry/MetricStat";
import { ChartCell } from "@/components/telemetry/ChartCell";
import { TimeSeriesChart } from "@/components/charts/TimeSeriesChart";
import { PlaybackControls } from "@/components/simulation/PlaybackControls";
import { useSimulation } from "@/lib/simulation/SimulationProvider";
import { toFullChartData, n } from "@/lib/telemetry/series";
import { SIM_DT_S } from "@/lib/baseline/constants";
import { deg, fixed, pct } from "@/lib/format";

/**
 * /telemetry — Stitch "Telemetry".
 * Metrics bar + 2x3 synchronized chart grid + transport. Every series is real
 * C++ telemetry; the playhead is the shared simulation time.
 * Substitutions (no such signal in the frozen telemetry contract, documented in
 * STITCH_IMPLEMENTATION_MAP.md): "Motor Current Draw" -> Detection Centroid Offset,
 * "Sensor Fusion Confidence" -> Detection Error vs Truth.
 */
export default function TelemetryPage() {
  const { frames, frameIndex, simTime, meta, current } = useSimulation();

  const upTo = frames.slice(0, frameIndex + 1);
  const lostSoFar = upTo.filter((f) => f.trackingState === "TARGET_LOST").length;
  const detectedSoFar = upTo.filter((f) => f.detection.detected).length;
  const runningDetectionPct = upTo.length ? (100 * detectedSoFar) / upTo.length : 0;
  const panLimited = current.control.panSaturated;

  const angErr = useMemo(
    () => toFullChartData(frames, (f) => ({ err: n(f.tracking.totalErrorDeg) }), 400),
    [frames],
  );
  const panTilt = useMemo(
    () =>
      toFullChartData(
        frames,
        (f) => ({ pan: n(f.tracking.panErrorDeg), tilt: n(f.tracking.tiltErrorDeg) }),
        400,
      ),
    [frames],
  );
  const panRate = useMemo(
    () =>
      toFullChartData(
        frames,
        (f) => ({ cmd: n(f.control.panCommandDegS), applied: n(f.camera.panRateDegS) }),
        400,
      ),
    [frames],
  );
  const tiltRate = useMemo(
    () =>
      toFullChartData(
        frames,
        (f) => ({ cmd: n(f.control.tiltCommandDegS), applied: n(f.camera.tiltRateDegS) }),
        400,
      ),
    [frames],
  );
  const centroidOffset = useMemo(
    () =>
      toFullChartData(
        frames,
        (f) => ({
          off:
            f.tracking.errorXPx != null && f.tracking.errorYPx != null
              ? Math.hypot(f.tracking.errorXPx, f.tracking.errorYPx)
              : 0,
        }),
        400,
      ),
    [frames],
  );
  const detErr = useMemo(
    () => toFullChartData(frames, (f) => ({ d: n(f.detectionErrorPx) }), 400),
    [frames],
  );

  const firstLoss = frames.find((f) => f.trackingState === "TARGET_LOST");
  const reacq = firstLoss
    ? frames.slice(firstLoss.frame).find((f) => f.detection.detected)
    : undefined;
  const lossMarkers = firstLoss
    ? [
        { x: firstLoss.simulationTime, color: "var(--lost)", label: "LOST" },
        ...(reacq ? [{ x: reacq.simulationTime, color: "var(--tracking)", label: "REACQ" }] : []),
      ]
    : [];

  return (
    <Screen>
      {/* metrics bar */}
      {/* contained horizontal scroll at narrow widths -- never full-page scroll */}
      <section className="flex h-[64px] shrink-0 items-stretch overflow-x-auto border-b border-outline-variant bg-surface-container-low">
        <MetricStat
          className="w-[170px] shrink-0 flex-1 border-r border-outline-variant"
          label="Detection Rate"
          value={pct(meta?.expected.detectionPct ?? runningDetectionPct, 2)}
          tone="primary"
          pulse
        />
        <MetricStat
          className="w-[170px] shrink-0 flex-1 border-r border-outline-variant"
          label="RMS Pointing Error"
          value={deg(meta?.expected.rmsDeg, 3)}
          status={(meta?.expected.rmsDeg ?? 0) < 1 ? "NOMINAL" : "ELEVATED"}
          tone={(meta?.expected.rmsDeg ?? 0) < 1 ? "default" : "warning"}
        />
        <MetricStat
          className="w-[170px] shrink-0 flex-1 border-r border-outline-variant"
          label="Lost Frames"
          value={lostSoFar}
          tone={lostSoFar > 0 ? "warning" : "default"}
        />
        <MetricStat
          className="w-[170px] shrink-0 flex-1"
          label="Frame Interval"
          value={fixed(SIM_DT_S * 1000, 1)}
          unit="ms"
          status="FIXED 50 HZ"
        />
      </section>

      {/* chart grid */}
      <section className="grid min-h-0 flex-1 grid-cols-2 grid-rows-3 gap-gutter overflow-hidden bg-outline-variant">
        <ChartCell
          title="Total Angular Error (deg)"
          legend={
            <span className="text-primary">
              CUR: {deg(current.tracking.totalErrorDeg, 2)} | MAX: {deg(meta?.expected.maxDeg, 2)}
            </span>
          }
        >
          <TimeSeriesChart
            data={angErr}
            compact
            playhead={simTime}
            markers={lossMarkers}
            threshold={{ value: 10, label: "PRD 40px" }}
            series={[{ key: "err", label: "Total", color: "var(--tracking)", width: 1.5 }]}
          />
        </ChartCell>

        <ChartCell
          title="Pan / Tilt Error Separation (deg)"
          legend={
            <>
              <span className="text-tertiary">PAN: {deg(current.tracking.panErrorDeg, 2)}</span>
              <span className="text-primary-fixed-dim">TILT: {deg(current.tracking.tiltErrorDeg, 2)}</span>
            </>
          }
        >
          <TimeSeriesChart
            data={panTilt}
            compact
            playhead={simTime}
            series={[
              { key: "pan", label: "Pan err", color: "var(--warning)" },
              { key: "tilt", label: "Tilt err", color: "var(--tracking-strong)", dashed: true },
            ]}
          />
        </ChartCell>

        <ChartCell
          title="Command vs Applied Rate (PAN) [deg/s]"
          marker={panLimited ? { label: "PAN LIMIT REACHED", tone: "warning" } : undefined}
        >
          <TimeSeriesChart
            data={panRate}
            compact
            playhead={simTime}
            series={[
              { key: "cmd", label: "Command", color: "var(--foreground)", width: 1.5 },
              { key: "applied", label: "Applied", color: "var(--tracking)" },
            ]}
          />
        </ChartCell>

        <ChartCell title="Command vs Applied Rate (TILT) [deg/s]">
          <TimeSeriesChart
            data={tiltRate}
            compact
            playhead={simTime}
            series={[
              { key: "cmd", label: "Command", color: "var(--foreground)", width: 1.5 },
              { key: "applied", label: "Applied", color: "var(--tracking)" },
            ]}
          />
        </ChartCell>

        <ChartCell
          title="Detection Centroid Offset (px)"
          legend={
            <span className="text-secondary-fixed">
              CUR:{" "}
              {current.tracking.errorXPx != null && current.tracking.errorYPx != null
                ? fixed(Math.hypot(current.tracking.errorXPx, current.tracking.errorYPx), 1)
                : "—"}
              px
            </span>
          }
        >
          <TimeSeriesChart
            data={centroidOffset}
            compact
            playhead={simTime}
            series={[{ key: "off", label: "Centroid offset", color: "var(--detected)" }]}
          />
        </ChartCell>

        <ChartCell
          title="Detection Error vs Truth (px)"
          marker={
            current.trackingState === "TARGET_LOST" ? { label: "OPTICAL DROPOUT", tone: "lost" } : undefined
          }
        >
          <TimeSeriesChart
            data={detErr}
            compact
            playhead={simTime}
            markers={lossMarkers}
            series={[{ key: "d", label: "Detect err vs truth", color: "var(--foreground)" }]}
          />
        </ChartCell>
      </section>

      <PlaybackControls />
    </Screen>
  );
}
