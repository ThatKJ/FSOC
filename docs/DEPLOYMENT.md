# Deployment

How FSOC's public web presence is built, deployed, and — most importantly — where its
honest boundary sits relative to the local phone-camera prototype. See `README.md`
"Working modes" for the short version.

## Architecture

```
GitHub  ThatKJ/FSOC
   │  (push / PR)
   ▼
Vercel  builds  frontend/  only  (Root Directory: frontend)
   │
   ▼
Public site: project pages + Mission Control (REPLAY mode) + checked-in
             deterministic telemetry fixtures produced by the real C++ engine
```

The public deployment never builds or runs any C++ code. `frontend/` is a self-contained
Next.js app; its REPLAY mode and evidence pages read only fixtures already committed to
the repo (`frontend/lib/telemetry/fixtures/*.json`), generated ahead of time by the real
`fsoc_demo` binary — see `frontend/scripts/generate-fixtures.mjs`.

## Vercel project

| Setting | Value |
|---|---|
| Project | `fsoc` (Vercel team: Kirtan's projects) |
| GitHub repo | `ThatKJ/FSOC` |
| Root Directory | `frontend` |
| Framework | Next.js (auto-detected) |
| Production branch | `main` |
| Environment variables | none required — no secrets, no API keys |

Root Directory is a project-level setting in the Vercel dashboard
(**Project → Settings → General → Root Directory**) — it is not something a `vercel.json`
or the Vercel API-via-MCP can safely change on an already-linked project without risking a
misconfigured push, so it's a one-time manual step rather than something automated here.

### Preview vs. production

- Every push to a non-`main` branch (or a PR) gets its own **Preview Deployment** —
  a real, shareable URL built from that exact commit. Use this to validate a change
  before it ever reaches `main`.
- Pushing/merging to `main` builds **production** at the project's production domain.
- Nothing in this repository auto-merges a feature branch into `main`, and nothing here
  should trigger a production deploy without a human deciding `main` is ready.

## Why the phone-camera prototype cannot run on Vercel

`fsoc_live` is a long-running native C++ process that opens a real camera device and
writes `generated/live/telemetry.json` after every frame. Vercel's serverless functions
are short-lived, have no access to your Mac's camera hardware or local filesystem, and
cannot host a persistent native process. There is no configuration that changes this —
it's a hardware/process locality problem, not a missing feature.

`frontend/app/api/live-camera/route.ts` reflects this honestly: it reads
`generated/live/telemetry.json` if present and returns `503 {"error":"no live session"}`
if not — it **never fabricates a reading**, in any environment. On the public deployment
that file will never exist, so `/mission/live` always shows the "Real-Camera Mode runs
locally" state (with the exact command to run it yourself) rather than an error page or
fake data.

### Running the real-camera mode locally

```bash
./run_fsoc.sh phone     # or: ./run_fsoc.sh golden for the full narrated walkthrough
cd frontend && npm run dev
# open http://localhost:4317/mission/live
```

See `docs/PHONE_CAMERA_GOLDEN_DEMO.md` and `docs/PHONE_CAMERA_TEST_PLAN.md` for the full
walkthrough and camera setup.

## Deployment smoke

`frontend/tests/e2e/smoke.spec.ts` doubles as a deploy-safe smoke suite: point
it at any deployed URL and it exercises the public routes, navigation,
playback, and API error handling. The one test that needs the local C++
engine (`engine mode ... reaches the C++ engine`) skips itself automatically
when that engine isn't reachable, so the full suite is safe to run against a
deployment that has no C++ build at all.

```bash
cd frontend
FSOC_BASE_URL=https://<preview-or-prod-url> npx playwright test
```

Setting `FSOC_BASE_URL` also disables the config's local `webServer` — no
local build or dev server is started when testing a remote deployment.

## Troubleshooting

- **Deployed site shows `framework: null` / doesn't look like the app** — Root
  Directory isn't set to `frontend` in the Vercel project settings; fix it there and
  redeploy.
- **`/mission/live` shows "Real-Camera Mode" on your own machine** — that's correct
  when `fsoc_live` isn't currently running; start it with `./run_fsoc.sh phone`.
- **Build fails on `npm run build`** — reproduce locally first: `cd frontend && npm ci &&
  npm run typecheck && npm run lint && npm run build`. Vercel runs the same commands.

## Claude Code + Vercel MCP

This repository's Vercel project can be inspected and deployed from Claude Code via
Vercel's official MCP server.

```bash
claude mcp add --transport http vercel https://mcp.vercel.com
```

Then, inside Claude Code, run `/mcp` and complete the OAuth authorization in the browser.
Once connected, Claude Code can list projects/deployments and inspect build logs and
runtime errors for this project — never expose or paste OAuth tokens into a session.

Some project-level settings (notably **Root Directory** on an already-linked project) are
intentionally not exposed as a write operation over MCP and must be changed in the Vercel
dashboard directly.
