import { NextRequest, NextResponse } from "next/server";

import { SCENARIOS, SCENARIO_IDS, SIM_RATE_HZ, type ScenarioId } from "@/lib/baseline/constants";
import { engineAvailable, engineBinaryPath, runEngineScenario } from "@/lib/simulation/engine";
import { fixtureAvailable, loadFixture } from "@/lib/simulation/fixtures";
import type { SimulationMeta, SimulationPayload } from "@/lib/telemetry/types";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";

function isScenario(v: string): v is ScenarioId {
  return (SCENARIO_IDS as string[]).includes(v);
}

/**
 * GET /api/simulation/:scenario?source=engine|replay|auto[&duration=<s>]
 *
 * Returns real C++-generated telemetry (LOCAL ENGINE MODE) or the checked-in
 * deterministic replay fixture (REPLAY MODE). `auto` prefers the engine and
 * falls back to the fixture. No simulation math is computed here.
 */
export async function GET(
  req: NextRequest,
  { params }: { params: { scenario: string } },
) {
  const scenario = params.scenario;
  if (!isScenario(scenario)) {
    return NextResponse.json(
      {
        error: "unknown scenario",
        detail: `'${scenario}' is not one of ${SCENARIO_IDS.join(", ")}`,
      },
      { status: 400 },
    );
  }

  const url = new URL(req.url);
  const requested = (url.searchParams.get("source") ?? "auto").toLowerCase();
  const durationParam = url.searchParams.get("duration");
  const durationS = durationParam ? Number(durationParam) : undefined;
  if (durationParam && (!Number.isFinite(durationS) || (durationS as number) <= 0)) {
    return NextResponse.json({ error: "duration must be a positive number of seconds" }, { status: 400 });
  }

  // Stage-3 perception mode + P0-v2 tracker (ADR-018/ADR-019) — LOCAL ENGINE
  // MODE only; replay fixtures are always CLASSICAL, tracker off (documented,
  // not a bug: the checked-in fixtures were generated with neither flag).
  const modeParam = (url.searchParams.get("mode") ?? "classical").toLowerCase();
  if (modeParam !== "classical" && modeParam !== "ai" && modeParam !== "hybrid") {
    return NextResponse.json({ error: "mode must be one of: classical, ai, hybrid" }, { status: 400 });
  }
  const mode = modeParam as "classical" | "ai" | "hybrid";
  const trackerEnabled = url.searchParams.get("tracker") === "1";

  const cfg = SCENARIOS[scenario];
  const baseMeta = {
    scenario,
    demoScenario: cfg.demoScenario,
    label: cfg.label,
    controlEnabled: cfg.controlEnabled,
    durationS: durationS ?? cfg.durationS,
    frames: cfg.frames,
    simRateHz: SIM_RATE_HZ,
    expected: cfg.expected,
  };

  const wantEngine = requested === "engine" || requested === "auto";
  const wantReplay = requested === "replay" || requested === "auto";

  // ---- LOCAL ENGINE MODE ----
  if (wantEngine && engineAvailable()) {
    try {
      const run = await runEngineScenario(scenario, { durationS, mode, trackerEnabled });
      const meta: SimulationMeta = {
        ...baseMeta,
        frames: run.frames.length,
        source: "engine",
        enginePath: run.enginePath,
        generatedAt: new Date().toISOString(),
      };
      const payload: SimulationPayload = { meta, frames: run.frames };
      return NextResponse.json(payload, {
        headers: { "cache-control": "no-store", "x-fsoc-source": "engine" },
      });
    } catch (err) {
      if (requested === "engine") {
        return NextResponse.json(
          { error: "engine run failed", detail: String(err), enginePath: engineBinaryPath() },
          { status: 502 },
        );
      }
      // auto -> fall through to replay
      console.warn(`[api/simulation] engine failed for ${scenario}, using replay:`, String(err));
    }
  } else if (requested === "engine") {
    return NextResponse.json(
      { error: "engine not available", detail: `not found at ${engineBinaryPath()}` },
      { status: 503 },
    );
  }

  // ---- REPLAY MODE ----
  if (wantReplay && fixtureAvailable(scenario)) {
    try {
      const { frames, generatedAt } = await loadFixture(scenario);
      const meta: SimulationMeta = {
        ...baseMeta,
        frames: frames.length,
        source: "replay",
        generatedAt,
      };
      const payload: SimulationPayload = { meta, frames };
      return NextResponse.json(payload, {
        headers: { "cache-control": "public, max-age=60", "x-fsoc-source": "replay" },
      });
    } catch (err) {
      return NextResponse.json({ error: "replay fixture failed", detail: String(err) }, { status: 500 });
    }
  }

  return NextResponse.json(
    {
      error: "no telemetry source available",
      detail:
        "the C++ engine is not built and no replay fixture is checked in. run `npm run gen:fixtures` (needs the C++ build) or `cmake --build --preset debug`.",
    },
    { status: 503 },
  );
}
