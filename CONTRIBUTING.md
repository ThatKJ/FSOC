# Contributing

FSOC is a research/hackathon engineering project (SIH26169). Contributions are
welcome, but the architecture boundaries below are load-bearing — read
`CLAUDE.md` before opening a PR that touches `src/`, `include/`, or `apps/`.

## Before you start

- **C++ changes**: know which module you're touching
  (`Environment` / `Trajectory` / `PanTiltCamera` / `Detector` / `Estimator` /
  `Controller` / `SimulationRunner` / `Telemetry`). Don't let physics, detection,
  control, and logging bleed into one function — see `docs/15_INTERFACE_CONTRACTS.md`.
- **Coordinate/unit conventions are frozen** (`docs/04_COORDINATES_AND_MATH.md`).
  If a change requires touching a sign or unit, update the math doc and tests in
  the same PR.
- **The `v1_baseline` tag is frozen.** Don't modify validated tracking math,
  detector behavior, controller tuning, or `presentation_assets/` evidence
  unless an actual bug forces it.
- **Frontend changes**: `frontend/DESIGN_SYSTEM.md` documents the token system
  (`Orbital Precision`) — reuse existing `components/ui` primitives rather than
  introducing new visual patterns.

## Workflow

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
cd frontend && npm run typecheck && npm run lint && npm run build && npx playwright test
```

All four must pass before opening a PR. The PR template
(`.github/pull_request_template.md`) asks which module you touched and which
architecture checks apply — fill it in honestly, it's there to catch scope
creep, not to be busywork.

## Reporting bugs / proposing features

Open a GitHub issue. For anything touching the frozen baseline or measured
claims in `README.md`, include the exact command/output that shows the
current (and, if applicable, proposed) behavior — this project treats
measured numbers as load-bearing, not decorative.
