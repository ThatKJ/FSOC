"use client";

/**
 * The single simulation clock for the whole application.
 *
 *   TelemetryProvider (fetch)  ──►  playback clock  ──►  useSimulation()
 *         │                                                    │
 *   /api/simulation/:scenario                            every screen
 *   (engine or replay fixture)                           reads ONE current frame
 *
 * Components consume `current` (a DemoSnapshot from the C++ engine) plus playback
 * controls. They never generate system state. No Math.random anywhere.
 */

import React, {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useReducer,
  useRef,
  useState,
} from "react";

import { SIM_DT_S, SCENARIOS, type ScenarioId } from "@/lib/baseline/constants";
import type {
  DemoSnapshot,
  RunState,
  SimEvent,
  SimulationMeta,
  SimulationPayload,
} from "@/lib/telemetry/types";
import { deriveEvents } from "./events";
import { EMPTY_SNAPSHOT } from "./emptySnapshot";

export type PlaybackSpeed = 0.5 | 1 | 2;
export type TelemetrySource = "auto" | "engine" | "replay";
export type LoadStatus = "idle" | "loading" | "ready" | "error";
/** Stage-3 perception seam (ADR-018), engine-mode only — replay fixtures are always CLASSICAL. */
export type PerceptionModeSelection = "classical" | "ai" | "hybrid";

interface SimState {
  scenario: ScenarioId;
  source: TelemetrySource;
  speed: PlaybackSpeed;
  playing: boolean;
  frameIndex: number;
  runState: RunState;
  perceptionMode: PerceptionModeSelection;
  trackerEnabled: boolean;
}

type Action =
  | { type: "SET_SCENARIO"; scenario: ScenarioId }
  | { type: "SET_SOURCE"; source: TelemetrySource }
  | { type: "SET_PERCEPTION_MODE"; mode: PerceptionModeSelection }
  | { type: "SET_TRACKER_ENABLED"; enabled: boolean }
  | { type: "SET_SPEED"; speed: PlaybackSpeed }
  | { type: "PLAY" }
  | { type: "PAUSE" }
  | { type: "TOGGLE" }
  | { type: "RESET" }
  | { type: "SEEK"; index: number; total: number }
  | { type: "ADVANCE"; index: number; total: number }
  | { type: "FINISH" }
  | { type: "TELEMETRY_LOADED" };

function reducer(s: SimState, a: Action): SimState {
  switch (a.type) {
    case "SET_SCENARIO":
      if (a.scenario === s.scenario) return s;
      return { ...s, scenario: a.scenario, frameIndex: 0, playing: false, runState: "READY" };
    case "SET_SOURCE":
      if (a.source === s.source) return s;
      return { ...s, source: a.source, frameIndex: 0, playing: false, runState: "READY" };
    case "SET_PERCEPTION_MODE":
      if (a.mode === s.perceptionMode) return s;
      return { ...s, perceptionMode: a.mode, frameIndex: 0, playing: false, runState: "READY" };
    case "SET_TRACKER_ENABLED":
      if (a.enabled === s.trackerEnabled) return s;
      return { ...s, trackerEnabled: a.enabled, frameIndex: 0, playing: false, runState: "READY" };
    case "SET_SPEED":
      return { ...s, speed: a.speed };
    case "PLAY":
      return { ...s, playing: true, runState: "RUNNING" };
    case "PAUSE":
      return { ...s, playing: false, runState: s.runState === "FINISHED" ? "FINISHED" : "PAUSED" };
    case "TOGGLE":
      return s.playing
        ? { ...s, playing: false, runState: s.runState === "FINISHED" ? "FINISHED" : "PAUSED" }
        : { ...s, playing: true, runState: "RUNNING" };
    case "RESET":
      return { ...s, frameIndex: 0, playing: false, runState: "READY" };
    case "SEEK": {
      const idx = Math.max(0, Math.min(a.index, Math.max(0, a.total - 1)));
      const finished = idx >= a.total - 1;
      return {
        ...s,
        frameIndex: idx,
        playing: false,
        runState: finished ? "FINISHED" : idx === 0 ? "READY" : "PAUSED",
      };
    }
    case "ADVANCE": {
      const idx = Math.max(0, Math.min(a.index, Math.max(0, a.total - 1)));
      if (idx >= a.total - 1) {
        return { ...s, frameIndex: a.total - 1, playing: false, runState: "FINISHED" };
      }
      return { ...s, frameIndex: idx, runState: "RUNNING" };
    }
    case "FINISH":
      return { ...s, playing: false, runState: "FINISHED" };
    case "TELEMETRY_LOADED":
      return s;
  }
}

interface SimContextValue {
  // selection
  scenario: ScenarioId;
  setScenario: (s: ScenarioId) => void;
  runScenario: (s: ScenarioId) => void;
  source: TelemetrySource;
  setSource: (s: TelemetrySource) => void;
  perceptionMode: PerceptionModeSelection;
  setPerceptionMode: (m: PerceptionModeSelection) => void;
  trackerEnabled: boolean;
  setTrackerEnabled: (e: boolean) => void;
  // telemetry
  frames: DemoSnapshot[];
  meta: SimulationMeta | null;
  events: SimEvent[];
  status: LoadStatus;
  error: string | null;
  reload: () => void;
  // playback
  frameIndex: number;
  current: DemoSnapshot;
  simTime: number;
  progress: number; // 0..1
  totalFrames: number;
  playing: boolean;
  runState: RunState;
  speed: PlaybackSpeed;
  setSpeed: (s: PlaybackSpeed) => void;
  play: () => void;
  pause: () => void;
  toggle: () => void;
  reset: () => void;
  seek: (index: number) => void;
  seekTime: (seconds: number) => void;
  stepFrame: (delta: number) => void;
}

const SimContext = createContext<SimContextValue | null>(null);

const DEFAULT_SCENARIO: ScenarioId = "sinusoidal";

export function SimulationProvider({ children }: { children: React.ReactNode }) {
  const [state, dispatch] = useReducer(reducer, {
    scenario: DEFAULT_SCENARIO,
    source: "auto",
    speed: 1,
    playing: false,
    frameIndex: 0,
    runState: "READY",
    perceptionMode: "classical",
    trackerEnabled: false,
  });

  const [frames, setFrames] = useState<DemoSnapshot[]>([]);
  const [meta, setMeta] = useState<SimulationMeta | null>(null);
  const [status, setStatus] = useState<LoadStatus>("idle");
  const [error, setError] = useState<string | null>(null);
  const [reloadKey, setReloadKey] = useState(0);
  const autoplayRef = useRef(false);
  const loadedKeyRef = useRef<string>("");

  // fetch telemetry when scenario / source changes
  useEffect(() => {
    const ctrl = new AbortController();
    let alive = true;
    // only rewind the playhead when the SELECTION changes — a plain refetch
    // (reloadKey bump) of the same scenario keeps the current position.
    const selectionKey = `${state.scenario}:${state.source}:${state.perceptionMode}:${state.trackerEnabled}`;
    const isNewSelection = loadedKeyRef.current !== selectionKey;
    setStatus("loading");
    setError(null);
    const params = new URLSearchParams({ source: state.source, mode: state.perceptionMode });
    if (state.trackerEnabled) params.set("tracker", "1");
    fetch(`/api/simulation/${state.scenario}?${params.toString()}`, { signal: ctrl.signal })
      .then(async (r) => {
        const body = (await r.json()) as SimulationPayload | { error: string; detail?: string };
        if (!r.ok || !("frames" in body)) {
          const msg = "error" in body ? `${body.error}${body.detail ? ` — ${body.detail}` : ""}` : `HTTP ${r.status}`;
          throw new Error(msg);
        }
        if (!alive) return;
        setFrames(body.frames);
        setMeta(body.meta);
        setStatus("ready");
        loadedKeyRef.current = selectionKey;
        if (isNewSelection && !autoplayRef.current) dispatch({ type: "RESET" });
        if (autoplayRef.current) {
          autoplayRef.current = false;
          dispatch({ type: "RESET" });
          setTimeout(() => alive && dispatch({ type: "PLAY" }), 0);
        }
      })
      .catch((e: unknown) => {
        if (!alive || (e instanceof DOMException && e.name === "AbortError")) return;
        setFrames([]);
        setMeta(null);
        setStatus("error");
        setError(e instanceof Error ? e.message : String(e));
      });
    return () => {
      alive = false;
      ctrl.abort();
    };
  }, [state.scenario, state.source, state.perceptionMode, state.trackerEnabled, reloadKey]);

  const events = useMemo(() => deriveEvents(frames), [frames]);
  const totalFrames = frames.length;

  // playback loop — wall time * speed -> 50 Hz sim frames.
  // interval-based (not rAF) so it keeps advancing in headless / background renders.
  const accRef = useRef(0);
  const lastTsRef = useRef<number | null>(null);
  const frameIndexRef = useRef(state.frameIndex);
  frameIndexRef.current = state.frameIndex;

  useEffect(() => {
    if (!state.playing || totalFrames === 0) {
      lastTsRef.current = null;
      return;
    }
    const step = () => {
      const now = typeof performance !== "undefined" ? performance.now() : Date.now();
      if (lastTsRef.current == null) lastTsRef.current = now;
      const dtRealS = Math.min(0.25, (now - lastTsRef.current) / 1000);
      lastTsRef.current = now;
      accRef.current += dtRealS * state.speed;

      let advanced = frameIndexRef.current;
      while (accRef.current >= SIM_DT_S && advanced < totalFrames - 1) {
        accRef.current -= SIM_DT_S;
        advanced += 1;
      }
      if (advanced !== frameIndexRef.current) {
        frameIndexRef.current = advanced;
        dispatch({ type: "ADVANCE", index: advanced, total: totalFrames });
      }
      if (advanced >= totalFrames - 1) {
        accRef.current = 0;
        dispatch({ type: "FINISH" });
      }
    };
    const id = setInterval(step, 16);
    return () => clearInterval(id);
  }, [state.playing, state.speed, totalFrames]);

  // reset the accumulator whenever we jump
  useEffect(() => {
    accRef.current = 0;
    lastTsRef.current = null;
  }, [state.frameIndex, state.scenario, state.source, state.perceptionMode, state.trackerEnabled]);

  const clampedIndex = totalFrames > 0 ? Math.min(state.frameIndex, totalFrames - 1) : 0;
  const current = totalFrames > 0 ? frames[clampedIndex] : EMPTY_SNAPSHOT;
  const simTime = current.simulationTime ?? clampedIndex * SIM_DT_S;
  const progress = totalFrames > 1 ? clampedIndex / (totalFrames - 1) : 0;

  const setScenario = useCallback((s: ScenarioId) => dispatch({ type: "SET_SCENARIO", scenario: s }), []);
  const runScenario = useCallback((s: ScenarioId) => {
    autoplayRef.current = true;
    dispatch({ type: "SET_SCENARIO", scenario: s });
    // if the scenario is unchanged, SET_SCENARIO is a no-op — reload + autoplay
    setReloadKey((k) => k + 1);
  }, []);
  const setSource = useCallback((s: TelemetrySource) => dispatch({ type: "SET_SOURCE", source: s }), []);
  const setPerceptionMode = useCallback(
    (m: PerceptionModeSelection) => dispatch({ type: "SET_PERCEPTION_MODE", mode: m }),
    [],
  );
  const setTrackerEnabled = useCallback(
    (e: boolean) => dispatch({ type: "SET_TRACKER_ENABLED", enabled: e }),
    [],
  );
  const setSpeed = useCallback((s: PlaybackSpeed) => dispatch({ type: "SET_SPEED", speed: s }), []);
  const play = useCallback(() => dispatch({ type: "PLAY" }), []);
  const pause = useCallback(() => dispatch({ type: "PAUSE" }), []);
  const toggle = useCallback(() => dispatch({ type: "TOGGLE" }), []);
  const reset = useCallback(() => dispatch({ type: "RESET" }), []);
  const seek = useCallback(
    (index: number) => dispatch({ type: "SEEK", index, total: Math.max(1, totalFrames) }),
    [totalFrames],
  );
  const seekTime = useCallback(
    (seconds: number) => dispatch({ type: "SEEK", index: Math.round(seconds / SIM_DT_S), total: Math.max(1, totalFrames) }),
    [totalFrames],
  );
  const stepFrame = useCallback(
    (delta: number) => dispatch({ type: "SEEK", index: frameIndexRef.current + delta, total: Math.max(1, totalFrames) }),
    [totalFrames],
  );
  const reload = useCallback(() => setReloadKey((k) => k + 1), []);

  const value = useMemo<SimContextValue>(
    () => ({
      scenario: state.scenario,
      setScenario,
      runScenario,
      source: state.source,
      setSource,
      perceptionMode: state.perceptionMode,
      setPerceptionMode,
      trackerEnabled: state.trackerEnabled,
      setTrackerEnabled,
      frames,
      meta,
      events,
      status,
      error,
      reload,
      frameIndex: clampedIndex,
      current,
      simTime,
      progress,
      totalFrames,
      playing: state.playing,
      runState: state.runState,
      speed: state.speed,
      setSpeed,
      play,
      pause,
      toggle,
      reset,
      seek,
      seekTime,
      stepFrame,
    }),
    [
      state.scenario, state.source, state.playing, state.runState, state.speed,
      state.perceptionMode, state.trackerEnabled,
      frames, meta, events, status, error, reload,
      clampedIndex, current, simTime, progress, totalFrames,
      setScenario, runScenario, setSource, setPerceptionMode, setTrackerEnabled,
      setSpeed, play, pause, toggle, reset, seek, seekTime, stepFrame,
    ],
  );

  return <SimContext.Provider value={value}>{children}</SimContext.Provider>;
}

export function useSimulation(): SimContextValue {
  const ctx = useContext(SimContext);
  if (!ctx) throw new Error("useSimulation must be used within <SimulationProvider>");
  return ctx;
}

/** convenience: current scenario metadata from the frozen constants table */
export function useScenarioMeta() {
  const { scenario } = useSimulation();
  return SCENARIOS[scenario];
}
