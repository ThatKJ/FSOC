#!/usr/bin/env bash
#
# FSOC — ONE-COMMAND PROJECT LAUNCHER
#
# Pure orchestration. This script builds, launches, and shuts down the
# EXISTING FSOC binaries and the EXISTING Mission Control frontend -- it
# contains zero tracking/perception/controller logic of its own. Every
# command it runs is one already documented in README.md,
# docs/PHONE_CAMERA_GOLDEN_DEMO.md, docs/PHONE_CAMERA_TEST_PLAN.md, or
# .github/workflows/ci.yml.
#
# Usage:
#   ./run_fsoc.sh                 interactive menu
#   ./run_fsoc.sh simulation      Simulation Demo
#   ./run_fsoc.sh phone           Phone Camera Demo
#   ./run_fsoc.sh probe           Camera Probe
#   ./run_fsoc.sh ui              Mission Control Only
#   ./run_fsoc.sh test            Run Full Validation
#   ./run_fsoc.sh golden          Golden Phone Demo
#   ./run_fsoc.sh build           Build Everything
#   ./run_fsoc.sh --rebuild [subcommand]   force a clean C++ reconfigure+rebuild first
#   ./run_fsoc.sh --no-browser [subcommand]  never auto-open a browser tab
#
# Primary environment: macOS / Apple Silicon. Kept reasonably POSIX-ish
# where practical, but this is a bash script (uses arrays, [[ ]], local).

set -Eeuo pipefail

# Job control (`set -m`): without it, a background `( ... ) &` job shares
# THIS script's own process group, so a scoped "kill the group" would kill
# the launcher itself. With it, each backgrounded job (frontend, fsoc_live)
# becomes its own process-group leader, so `kill -- -PID` on cleanup takes
# down that job's entire descendant tree (npm -> next dev -> next-server)
# without ever touching an unrelated process -- verified empirically, since
# killing only the immediate child PID left next-server running and the
# port still occupied.
set -m

# ---------------------------------------------------------------------------
# Paths, logging, small helpers
# ---------------------------------------------------------------------------

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

BUILD_DIR="build/debug"
LOG_DIR="generated/logs"
LIVE_OUT_DIR="generated/live"
CALIBRATION_DEFAULT="configs/phone_camera.cfg"
mkdir -p "${LOG_DIR}"

# Non-secret local overrides (gitignored). See fsoc.env.example.
if [[ -f "fsoc.env" ]]; then
  # shellcheck disable=SC1091
  source "fsoc.env"
fi

REBUILD=0
NO_BROWSER=0
SUBCOMMAND=""

for arg in "$@"; do
  case "${arg}" in
    --rebuild) REBUILD=1 ;;
    --no-browser) NO_BROWSER=1 ;;
    -h|--help)
      # Only the leading header comment block (stops at the first non-comment
      # line, i.e. `set -Eeuo pipefail`) -- not every "# ---" section divider
      # in the rest of the file.
      awk 'NR==1{next} /^set -/{exit} /^#/{sub(/^# ?/,""); print; next} {exit}' "${BASH_SOURCE[0]}"
      exit 0
      ;;
    *)
      if [[ -z "${SUBCOMMAND}" ]]; then SUBCOMMAND="${arg}"; fi
      ;;
  esac
done

log()        { printf '[FSOC] %s\n' "$*"; }
log_build()  { printf '[BUILD] %s\n' "$*"; }
log_camera() { printf '[CAMERA] %s\n' "$*"; }
log_engine() { printf '[ENGINE] %s\n' "$*"; }
log_ui()     { printf '[UI] %s\n' "$*"; }
log_test()   { printf '[TEST] %s\n' "$*"; }
log_err()    { printf '[ERROR] %s\n' "$*" >&2; }

have() { command -v "$1" >/dev/null 2>&1; }

# ---------------------------------------------------------------------------
# Clean shutdown -- only ever touches PIDs THIS script itself started.
# ---------------------------------------------------------------------------

FRONTEND_PID=""
FRONTEND_STARTED_BY_US=0
ENGINE_PID=""
ENGINE_STARTED_BY_US=0

# Kills exactly one job THIS script started, by process GROUP (set -m above
# makes each backgrounded job its own group leader) so the whole descendant
# tree (e.g. npm -> next dev -> next-server) goes with it -- never a broad
# `pkill -f`/`killall`, always this one scoped PID/PGID. SIGTERM first, a
# bounded wait, then SIGKILL only if it's still alive.
kill_job_group() {
  local pid="$1"
  kill -TERM -- "-${pid}" 2>/dev/null || true
  local waited=0
  while (( waited < 5 )) && kill -0 "${pid}" 2>/dev/null; do
    sleep 1
    waited=$((waited + 1))
  done
  if kill -0 "${pid}" 2>/dev/null; then
    kill -KILL -- "-${pid}" 2>/dev/null || true
  fi
  wait "${pid}" 2>/dev/null || true
}

cleanup() {
  local had_something=0
  echo
  log "Stopping FSOC..."
  if [[ "${ENGINE_STARTED_BY_US}" == "1" && -n "${ENGINE_PID}" ]] && kill -0 "${ENGINE_PID}" 2>/dev/null; then
    kill_job_group "${ENGINE_PID}"
    echo "  [ok] C++ process stopped"
    had_something=1
  fi
  if [[ "${FRONTEND_STARTED_BY_US}" == "1" && -n "${FRONTEND_PID}" ]] && kill -0 "${FRONTEND_PID}" 2>/dev/null; then
    kill_job_group "${FRONTEND_PID}"
    echo "  [ok] Mission Control stopped"
    had_something=1
  fi
  rm -f "${LOG_DIR}/frontend.pid" "${LOG_DIR}/fsoc_live.pid" "${LOG_DIR}/fsoc_demo.pid" 2>/dev/null || true
  if [[ "${had_something}" == "0" ]]; then
    echo "  (nothing this launcher started was still running)"
  fi
  echo "  [ok] Cleanup complete"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# ---------------------------------------------------------------------------
# Prerequisite checks -- informational; specific actions gate on what THEY need.
# ---------------------------------------------------------------------------

print_prereqs() {
  for tool in cmake ninja git node npm curl; do
    if have "${tool}"; then
      echo "[OK] ${tool} ($(${tool} --version 2>&1 | head -1))"
    else
      echo "[MISSING] ${tool}"
    fi
  done
  if have c++; then
    echo "[OK] c++ ($(c++ --version 2>&1 | head -1))"
  elif have clang++; then
    echo "[OK] clang++ ($(clang++ --version 2>&1 | head -1))"
  else
    echo "[MISSING] a C++ compiler (c++/clang++)"
  fi
}

require_cpp_toolchain() {
  local missing=0
  for tool in cmake ninja git; do
    have "${tool}" || { log_err "missing required tool: ${tool}"; missing=1; }
  done
  have c++ || have clang++ || { log_err "missing a C++ compiler"; missing=1; }
  if [[ "${missing}" == "1" ]]; then
    log_err "Install the missing tool(s) yourself, e.g.: brew install cmake ninja"
    log_err "(this launcher never auto-installs packages)"
    exit 1
  fi
}

require_node_toolchain() {
  local missing=0
  have node || { log_err "missing required tool: node"; missing=1; }
  have npm || { log_err "missing required tool: npm"; missing=1; }
  if [[ "${missing}" == "1" ]]; then
    log_err "Install Node.js yourself (e.g. https://nodejs.org, or: brew install node)"
    exit 1
  fi
}

# ---------------------------------------------------------------------------
# C++ build -- delegates entirely to the project's own CMake presets. Ninja's
# own incremental dependency tracking already skips unchanged targets; this
# never reimplements "is source stale" itself.
# ---------------------------------------------------------------------------

cpp_build() {
  require_cpp_toolchain
  if [[ "${REBUILD}" == "1" && -d "${BUILD_DIR}" ]]; then
    log_build "forcing a clean reconfigure (--rebuild) -- removing ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
  fi
  log_build "configuring (cmake --preset debug)"
  cmake --preset debug > "${LOG_DIR}/cmake_configure.log" 2>&1 \
    || { log_err "cmake configure failed -- see ${LOG_DIR}/cmake_configure.log"; tail -n 30 "${LOG_DIR}/cmake_configure.log" >&2; exit 1; }
  if grep -q "FSOC: OpenCV videoio present" "${LOG_DIR}/cmake_configure.log"; then
    log_build "OpenCV videoio detected -- phone-camera targets will be built"
  elif grep -q "FSOC: OpenCV .* found" "${LOG_DIR}/cmake_configure.log"; then
    log_build "OpenCV found, but videoio absent -- phone-camera targets will be SKIPPED"
  else
    log_build "OpenCV not found -- Step 4+ / phone-camera targets will be SKIPPED (brew install opencv)"
  fi
  log_build "building (cmake --build --preset debug)"
  cmake --build --preset debug 2>&1 | tee "${LOG_DIR}/cmake_build.log" \
    || { log_err "build failed -- see ${LOG_DIR}/cmake_build.log"; exit 1; }
  log_build "done"
}

require_binary() {
  local bin="$1"
  if [[ ! -x "${BUILD_DIR}/${bin}" ]]; then
    log_err "${BUILD_DIR}/${bin} does not exist."
    log_err "Either OpenCV/videoio was not found at configure time, or the build hasn't run yet."
    log_err "Try: ./run_fsoc.sh --rebuild build"
    exit 1
  fi
}

# ---------------------------------------------------------------------------
# Frontend
# ---------------------------------------------------------------------------

# The launcher never hardcodes the port -- it reads it from the same place
# next actually gets it (frontend/package.json's own -p flag), so it can
# never drift from reality.
detect_frontend_port() {
  local port
  port="$(grep -oE '\-p [0-9]+' frontend/package.json 2>/dev/null | head -1 | awk '{print $2}')"
  echo "${port:-4317}"
}
FRONTEND_PORT="$(detect_frontend_port)"
MISSION_CONTROL_URL="http://localhost:${FRONTEND_PORT}"

frontend_ensure_deps() {
  require_node_toolchain
  if [[ ! -d "frontend/node_modules" ]]; then
    log_ui "node_modules missing -- installing dependencies"
    (
      cd frontend
      if [[ -f "package-lock.json" ]]; then npm ci; else npm install; fi
    )
  else
    log_ui "node_modules already present -- skipping install"
  fi
}

# "ours" if something on the port answers AND looks like this app; "occupied"
# if something answers but doesn't; "free" otherwise. Never guesses.
port_status() {
  if ! curl -fsS --max-time 1 "${MISSION_CONTROL_URL}/" -o /tmp/fsoc_port_probe.$$ 2>/dev/null; then
    rm -f /tmp/fsoc_port_probe.$$
    echo "free"
    return
  fi
  if grep -qi "FSOC ALIGNMENT\|fsoc-mission-control\|SIH26169" /tmp/fsoc_port_probe.$$ 2>/dev/null; then
    rm -f /tmp/fsoc_port_probe.$$
    echo "ours"
  else
    rm -f /tmp/fsoc_port_probe.$$
    echo "occupied"
  fi
}

wait_for_http() {
  local url="$1" timeout_s="${2:-30}" waited=0
  while (( waited < timeout_s )); do
    if curl -fsS --max-time 1 "${url}" -o /dev/null 2>/dev/null; then
      return 0
    fi
    sleep 1
    waited=$((waited + 1))
  done
  return 1
}

# start_frontend <dev|prod> -- reuses an already-running instance if it's ours.
start_frontend() {
  local mode="$1"
  local status
  status="$(port_status)"
  if [[ "${status}" == "ours" ]]; then
    log_ui "Mission Control is already running on ${MISSION_CONTROL_URL} -- reusing it"
    FRONTEND_STARTED_BY_US=0
    return 0
  fi
  if [[ "${status}" == "occupied" ]]; then
    log_err "port ${FRONTEND_PORT} is already in use by something that is NOT FSOC Mission Control."
    log_err "Stop that process yourself, or set a different port in frontend/package.json, and retry."
    exit 1
  fi

  frontend_ensure_deps
  if [[ "${mode}" == "prod" ]]; then
    log_ui "building production frontend (npm run build)"
    (cd frontend && npm run build) 2>&1 | tee "${LOG_DIR}/frontend_build.log"
    log_ui "starting production server (npm run start)"
    (cd frontend && npm run start) > "${LOG_DIR}/frontend.log" 2>&1 &
  else
    log_ui "starting dev server (npm run dev)"
    (cd frontend && npm run dev) > "${LOG_DIR}/frontend.log" 2>&1 &
  fi
  FRONTEND_PID=$!
  FRONTEND_STARTED_BY_US=1
  echo "${FRONTEND_PID}" > "${LOG_DIR}/frontend.pid"

  if ! kill -0 "${FRONTEND_PID}" 2>/dev/null; then
    log_err "frontend process exited immediately -- last log lines:"
    tail -n 30 "${LOG_DIR}/frontend.log" >&2 || true
    exit 1
  fi
  log_ui "waiting for ${MISSION_CONTROL_URL} to respond..."
  if ! wait_for_http "${MISSION_CONTROL_URL}/" 60; then
    log_err "frontend did not become ready within 60s -- last log lines:"
    tail -n 30 "${LOG_DIR}/frontend.log" >&2 || true
    exit 1
  fi
  log_ui "Mission Control ready (PID ${FRONTEND_PID})"
}

# Only actually blocks if THIS instance started the frontend itself. If it
# was reused (already running, not ours) or never started (e.g. the phone-
# camera connection menu's "Back"), `wait ""` would otherwise silently no-op
# instead of blocking -- print the truth instead of a misleading "Press
# Ctrl+C to stop" immediately followed by the script exiting anyway.
block_until_stopped() {
  if [[ "${FRONTEND_STARTED_BY_US}" == "1" && -n "${FRONTEND_PID}" ]]; then
    echo "Press Ctrl+C to stop FSOC."
    wait "${FRONTEND_PID}" 2>/dev/null || true
  else
    echo "(Mission Control is not owned by this launcher instance -- nothing to wait on; exiting.)"
  fi
}

open_browser() {
  local url="$1"
  [[ "${NO_BROWSER}" == "1" ]] && return 0
  if [[ "$(uname)" == "Darwin" ]] && have open; then
    open "${url}" >/dev/null 2>&1 || true
  fi
}

print_routes() {
  echo
  echo "Available Mission Control routes:"
  [[ -d "frontend/app/mission" ]]      && echo "  ${MISSION_CONTROL_URL}/mission"
  [[ -d "frontend/app/mission/live" ]] && echo "  ${MISSION_CONTROL_URL}/mission/live"
  [[ -d "frontend/app/telemetry" ]]    && echo "  ${MISSION_CONTROL_URL}/telemetry"
}

# ---------------------------------------------------------------------------
# Mode 1 -- Simulation Demo
# ---------------------------------------------------------------------------

mode_simulation() {
  cpp_build
  require_binary fsoc_demo

  log_engine "running fsoc_demo static --mode hybrid --tracker (real engine, terminal proof)"
  mkdir -p generated/demo
  if ! "${BUILD_DIR}/fsoc_demo" static --mode hybrid --tracker --csv generated/demo/launcher_static.csv \
      2>&1 | tee "${LOG_DIR}/fsoc_demo.log"; then
    log_err "fsoc_demo failed -- see ${LOG_DIR}/fsoc_demo.log"
    exit 1
  fi

  start_frontend dev

  cat <<EOF

==================================================
FSOC SIMULATION MODE

Perception   HYBRID
Tracker      ENABLED
Actuator     SIMULATED
Camera       SYNTHETIC

Mission Control:
${MISSION_CONTROL_URL}/mission

Logs:
${LOG_DIR}/fsoc_demo.log
${LOG_DIR}/frontend.log
==================================================
EOF
  open_browser "${MISSION_CONTROL_URL}/mission"
  block_until_stopped
}

# ---------------------------------------------------------------------------
# Mode 2 / 6 -- Phone Camera Demo / Golden Phone Demo
# ---------------------------------------------------------------------------

ensure_calibration() {
  local calib="${FSOC_CAMERA_CALIBRATION:-${CALIBRATION_DEFAULT}}"
  if [[ -f "${calib}" ]]; then
    echo "${calib}"
    return 0
  fi
  log_camera "no calibration file at '${calib}' yet." >&2
  echo "FSOC needs a declared/measured camera field of view before it will run --" >&2
  echo "see docs/PHONE_CAMERA_METRICS.md \"Camera calibration\". It is never assumed" >&2
  echo "to equal the simulation's synthetic FOV." >&2
  echo >&2
  read -rp "Enter it now interactively? [y/N] " reply
  if [[ ! "${reply}" =~ ^[Yy]$ ]]; then
    log_err "no calibration file -- run ./build/debug/fsoc_camera_calibrate yourself, then retry."
    exit 1
  fi
  read -rp "  Raw capture width_px [1920]: " w; w="${w:-1920}"
  read -rp "  Raw capture height_px [1080]: " h; h="${h:-1080}"
  read -rp "  Horizontal FOV in degrees (from your phone's real spec sheet): " hfov
  read -rp "  Vertical FOV in degrees (from your phone's real spec sheet): " vfov
  if [[ -z "${hfov}" || -z "${vfov}" ]]; then
    log_err "FOV values are required -- see docs/PHONE_CAMERA_METRICS.md for the known-object estimation method if you don't have a spec sheet."
    exit 1
  fi
  mkdir -p "$(dirname "${calib}")"
  require_binary fsoc_camera_calibrate
  "${BUILD_DIR}/fsoc_camera_calibrate" --manual --width "${w}" --height "${h}" \
    --hfov-deg "${hfov}" --vfov-deg "${vfov}" --out "${calib}" >&2
  echo "${calib}"
}

# Does everything through "Mission Control ready" and the status banner, but
# does NOT block on `wait`. Split out so mode_golden can print its extra
# manual checklist BEFORE the blocking wait -- Ctrl+C during that wait exits
# the whole script immediately via the INT trap, so anything meant to be
# seen by the user must be printed before it, never "after mode_phone returns"
# (it structurally never returns on the Ctrl+C path).
mode_phone_start() {
  cpp_build
  require_binary fsoc_live
  require_binary fsoc_camera_probe

  echo
  echo "Choose phone camera connection:"
  echo "  1. Native camera index"
  echo "  2. Network camera URL"
  echo "  3. Back"
  read -rp "Select an option [1-3]: " conn_choice

  local -a live_args=()
  case "${conn_choice}" in
    1)
      read -rp "Run camera probe first? [Y/n] " run_probe
      if [[ ! "${run_probe}" =~ ^[Nn]$ ]]; then
        log_camera "running fsoc_camera_probe"
        "${BUILD_DIR}/fsoc_camera_probe" || true
      fi
      read -rp "Camera index [0]: " cam_index
      cam_index="${cam_index:-${FSOC_CAMERA_INDEX:-0}}"
      live_args+=(--source camera --camera-index "${cam_index}")
      ;;
    2)
      read -rp "Camera URL (MJPEG/RTSP/HTTP): " cam_url
      if [[ -z "${cam_url}" ]]; then log_err "a URL is required"; return 1; fi
      live_args+=(--source camera-url --camera-url "${cam_url}")
      ;;
    3|*)
      return 0
      ;;
  esac

  local calib
  calib="$(ensure_calibration)"
  live_args+=(--calibration "${calib}" --live-out "${LIVE_OUT_DIR}")

  local ai_model="${FSOC_AI_MODEL:-models/tiny_beacon_net.onnx}"
  if [[ -f "${ai_model}" ]]; then
    live_args+=(--mode hybrid --tracker --ai-model "${ai_model}")
  else
    log_camera "no AI model at ${ai_model} -- falling back to --mode classical (no Hybrid/AI)"
    live_args+=(--mode classical --tracker)
  fi

  mkdir -p "${LIVE_OUT_DIR}"
  rm -f "${LIVE_OUT_DIR}/telemetry.json"
  log_camera "starting fsoc_live ${live_args[*]}"
  log_camera "opening the camera may trigger a macOS permission prompt -- approve it if asked."
  "${BUILD_DIR}/fsoc_live" "${live_args[@]}" > "${LOG_DIR}/fsoc_live.log" 2>&1 &
  ENGINE_PID=$!
  ENGINE_STARTED_BY_US=1
  echo "${ENGINE_PID}" > "${LOG_DIR}/fsoc_live.pid"

  log_engine "waiting for the first real frame (up to 20s)..."
  local waited=0
  while (( waited < 20 )); do
    if ! kill -0 "${ENGINE_PID}" 2>/dev/null; then
      log_err "fsoc_live exited immediately -- last log lines:"
      tail -n 40 "${LOG_DIR}/fsoc_live.log" >&2 || true
      log_err "Camera access may require:"
      log_err "  System Settings -> Privacy & Security -> Camera -> enable access for your terminal application"
      return 1
    fi
    [[ -f "${LIVE_OUT_DIR}/telemetry.json" ]] && break
    sleep 1
    waited=$((waited + 1))
  done
  if [[ ! -f "${LIVE_OUT_DIR}/telemetry.json" ]]; then
    log_err "no frame arrived within 20s -- fsoc_live is still running (PID ${ENGINE_PID}); check ${LOG_DIR}/fsoc_live.log"
  else
    log_engine "fsoc_live is producing real frames (PID ${ENGINE_PID})"
  fi

  start_frontend dev

  cat <<EOF

==================================================
FSOC PHONE CAMERA MODE

Camera       REAL_PHONE_CAMERA
Perception   $([[ " ${live_args[*]} " == *" hybrid "* ]] && echo HYBRID || echo CLASSICAL)
Estimator    ENABLED
Controller   ENABLED
Actuator     VIRTUAL

Mission Control:
${MISSION_CONTROL_URL}/mission/live

Logs:
${LOG_DIR}/fsoc_live.log
${LOG_DIR}/frontend.log
==================================================
EOF
  open_browser "${MISSION_CONTROL_URL}/mission/live"
}

mode_phone() {
  mode_phone_start
  block_until_stopped
}

# ---------------------------------------------------------------------------
# Mode 3 -- Camera Probe
# ---------------------------------------------------------------------------

mode_probe() {
  cpp_build
  require_binary fsoc_camera_probe
  log_camera "running fsoc_camera_probe"
  "${BUILD_DIR}/fsoc_camera_probe"
}

# ---------------------------------------------------------------------------
# Mode 4 -- Mission Control Only
# ---------------------------------------------------------------------------

mode_mission_control_only() {
  start_frontend dev
  print_routes
  echo
  open_browser "${MISSION_CONTROL_URL}/"
  block_until_stopped
}

# ---------------------------------------------------------------------------
# Mode 5 -- Run Full Validation
# ---------------------------------------------------------------------------

mode_validate() {
  # Plain variables, not an associative array: macOS's default
  # /usr/bin/env bash is 3.2 (no `local -A` support) -- verified on this
  # machine. Every stage's own binary/tool already reports failures with a
  # real diagnostic in its own output/log; these are just SKIPPED/PASS/FAIL
  # labels for the final summary.
  local r_ctest="SKIPPED" r_acceptance="SKIPPED" r_typecheck="SKIPPED"
  local r_lint="SKIPPED" r_build="SKIPPED" r_playwright="SKIPPED"
  local overall="PASS"

  cpp_build  # exits the whole script on failure -- a build failure is a hard stop

  log_test "ctest --preset debug"
  if ctest --preset debug --output-on-failure 2>&1 | tee "${LOG_DIR}/ctest.log"; then
    r_ctest="PASS"
  else
    r_ctest="FAIL"; overall="FAIL"
  fi

  if [[ "${overall}" == "PASS" ]]; then
    log_test "step10_validation_smoke (baseline acceptance)"
    require_binary step10_validation_smoke
    if "${BUILD_DIR}/step10_validation_smoke" 2>&1 | tee "${LOG_DIR}/step10.log"; then
      r_acceptance="PASS"
    else
      r_acceptance="FAIL"; overall="FAIL"
    fi
  fi

  if [[ "${overall}" == "PASS" ]]; then
    frontend_ensure_deps
    log_test "npm run typecheck"
    if (cd frontend && npm run typecheck) 2>&1 | tee "${LOG_DIR}/frontend_typecheck.log"; then
      r_typecheck="PASS"
    else
      r_typecheck="FAIL"; overall="FAIL"
    fi
  fi

  if [[ "${overall}" == "PASS" ]]; then
    log_test "npm run lint"
    if (cd frontend && npm run lint) 2>&1 | tee "${LOG_DIR}/frontend_lint.log"; then
      r_lint="PASS"
    else
      r_lint="FAIL"; overall="FAIL"
    fi
  fi

  if [[ "${overall}" == "PASS" ]]; then
    log_test "npm run build"
    if (cd frontend && npm run build) 2>&1 | tee "${LOG_DIR}/frontend_build.log"; then
      r_build="PASS"
    else
      r_build="FAIL"; overall="FAIL"
    fi
  fi

  if [[ "${overall}" == "PASS" ]]; then
    log_test "npm run test:e2e (Playwright manages its own webServer)"
    if (cd frontend && npm run test:e2e) 2>&1 | tee "${LOG_DIR}/playwright.log"; then
      r_playwright="PASS"
    else
      r_playwright="FAIL"; overall="FAIL"
    fi
  fi

  echo
  echo "================================"
  echo "FSOC VALIDATION RESULT"
  echo "C++ tests: ${r_ctest}"
  echo "Acceptance: ${r_acceptance}"
  echo "Frontend typecheck: ${r_typecheck}"
  echo "Frontend lint: ${r_lint}"
  echo "Frontend build: ${r_build}"
  echo "Playwright: ${r_playwright}"
  echo "OVERALL: ${overall}"
  echo "================================"

  [[ "${overall}" == "PASS" ]]
}

# ---------------------------------------------------------------------------
# Mode 6 -- Golden Phone Demo
# ---------------------------------------------------------------------------

mode_golden() {
  log "Golden Phone Demo -- see docs/PHONE_CAMERA_GOLDEN_DEMO.md for the full narrated walkthrough."

  if [[ "$(uname)" == "Darwin" ]] && have open; then
    read -rp "Open tools/beacon_display.html now? [Y/n] " open_beacon
    if [[ ! "${open_beacon}" =~ ^[Nn]$ ]]; then
      open "tools/beacon_display.html" || true
    fi
  else
    log "Open tools/beacon_display.html yourself on a second screen/window."
  fi

  # mode_phone_start, NOT mode_phone: the checklist below must print BEFORE
  # the blocking wait, since Ctrl+C during that wait exits the whole script
  # immediately via the INT trap -- anything after a blocking wait would
  # never actually be seen.
  mode_phone_start

  cat <<'EOF'

Manual checklist (fill in yourself -- this launcher does NOT fake PASS states):
  [ ] Beacon visible
  [ ] Real centroid detected
  [ ] Pointing error responds to phone motion
  [ ] TRACKING achieved
  [ ] Brief occlusion -> COASTING
  [ ] Re-entry -> TRACKING
  [ ] Clutter behavior observed

Full checklist with PASS/FAIL fields: docs/PHONE_CAMERA_TEST_PLAN.md
EOF
  block_until_stopped
}

# ---------------------------------------------------------------------------
# Mode 7 -- Build Everything
# ---------------------------------------------------------------------------

mode_build() {
  cpp_build
  frontend_ensure_deps
  log_build "frontend production build (npm run build)"
  (cd frontend && npm run build)
  log_build "all builds complete"
}

# ---------------------------------------------------------------------------
# Menu
# ---------------------------------------------------------------------------

show_menu() {
  cat <<'EOF'

==================================================
FSOC — LAUNCHER
==================================================
1. Simulation Demo
2. Phone Camera Demo
3. Camera Probe
4. Mission Control Only
5. Run Full Validation
6. Golden Phone Demo
7. Build Everything
8. Exit
==================================================
EOF
  read -rp "Select an option [1-8]: " choice
  case "${choice}" in
    1) mode_simulation ;;
    2) mode_phone ;;
    3) mode_probe ;;
    4) mode_mission_control_only ;;
    5) mode_validate ;;
    6) mode_golden ;;
    7) mode_build ;;
    8) exit 0 ;;
    *) log_err "invalid choice" ;;
  esac
}

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

log "FSOC launcher -- repo root: ${REPO_ROOT}"
print_prereqs
echo

case "${SUBCOMMAND}" in
  simulation) mode_simulation ;;
  phone) mode_phone ;;
  probe) mode_probe ;;
  ui) mode_mission_control_only ;;
  test|validate) mode_validate ;;
  golden) mode_golden ;;
  build) mode_build ;;
  "")
    while true; do
      show_menu
    done
    ;;
  *)
    log_err "unknown subcommand '${SUBCOMMAND}'. One of: simulation phone probe ui test golden build"
    exit 2
    ;;
esac
