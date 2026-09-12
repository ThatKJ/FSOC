cmake_minimum_required(VERSION 3.24)
project(fsoc_mvp VERSION 0.1.0 LANGUAGES CXX)

option(FSOC_BUILD_TESTS "Build unit tests" ON)
option(FSOC_ENABLE_SANITIZERS "Enable Address/Undefined sanitizers in Debug" OFF)

# OpenCV powers only the image/perception boundary (Step 4+). Pure math/core stays
# OpenCV-free. AUTO builds those targets when OpenCV is discoverable and silently
# skips them otherwise; ON makes OpenCV mandatory; OFF never looks for it.
set(FSOC_ENABLE_OPENCV "AUTO" CACHE STRING "OpenCV renderer: AUTO | ON | OFF")
set_property(CACHE FSOC_ENABLE_OPENCV PROPERTY STRINGS AUTO ON OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)

add_library(fsoc_core
    src/geometry.cpp
    src/camera.cpp
    src/environment.cpp
    src/trajectory.cpp
    src/observation.cpp
    src/tracking_error.cpp
    src/target_tracker.cpp
)
add_library(fsoc::core ALIAS fsoc_core)

target_include_directories(fsoc_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(fsoc_core PUBLIC cxx_std_20)
fsoc_set_project_warnings(fsoc_core)
fsoc_enable_sanitizers(fsoc_core)

# ---------------------------------------------------------------------------
# fsoc_control — pan/tilt PID controller (Step 6). OpenCV-FREE.
# Depends on fsoc::core only (fsoc/tracking_error.hpp contract). No image,
# perception, or OpenCV dependency.
# ---------------------------------------------------------------------------
add_library(fsoc_control src/pid_controller.cpp)
add_library(fsoc::control ALIAS fsoc_control)
target_include_directories(fsoc_control
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(fsoc_control PUBLIC cxx_std_20)
target_link_libraries(fsoc_control PUBLIC fsoc::core)
fsoc_set_project_warnings(fsoc_control)
fsoc_enable_sanitizers(fsoc_control)

# ---------------------------------------------------------------------------
# fsoc_camera_calibration — Mobile Phone Camera-in-the-Loop milestone.
# The simplest defensible camera geometry (Phase 8): a declared/measured
# pinhole FOV, stored in a dependency-free key=value file. Deliberately
# OpenCV-FREE and buildable unconditionally (even with FSOC_ENABLE_OPENCV=OFF)
# — it is pure math + file I/O, no camera or image dependency at all.
# ---------------------------------------------------------------------------
add_library(fsoc_camera_calibration src/live_camera_calibration.cpp)
add_library(fsoc::camera_calibration ALIAS fsoc_camera_calibration)
target_include_directories(fsoc_camera_calibration
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(fsoc_camera_calibration PUBLIC cxx_std_20)
fsoc_set_project_warnings(fsoc_camera_calibration)
fsoc_enable_sanitizers(fsoc_camera_calibration)

# ---------------------------------------------------------------------------
# fsoc_virtual_actuator — Mobile Phone Camera-in-the-Loop milestone.
# Honest bookkeeping actuator (Phase 11). Also OpenCV-FREE and buildable
# unconditionally — pure rate-limit/integrate/clamp math, no camera/image
# dependency, no coupling to PanTiltCamera (see fsoc/virtual_actuator.hpp).
# ---------------------------------------------------------------------------
add_library(fsoc_virtual_actuator src/virtual_actuator.cpp)
add_library(fsoc::virtual_actuator ALIAS fsoc_virtual_actuator)
target_include_directories(fsoc_virtual_actuator
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(fsoc_virtual_actuator PUBLIC cxx_std_20)
fsoc_set_project_warnings(fsoc_virtual_actuator)
fsoc_enable_sanitizers(fsoc_virtual_actuator)

add_executable(fsoc_camera_calibrate apps/fsoc_camera_calibrate.cpp)
target_link_libraries(fsoc_camera_calibrate PRIVATE fsoc::camera_calibration)
fsoc_set_project_warnings(fsoc_camera_calibrate)
fsoc_enable_sanitizers(fsoc_camera_calibrate)

# ---------------------------------------------------------------------------
# OpenCV discovery (image/perception boundary only — never linked into fsoc_core)
# ---------------------------------------------------------------------------
set(FSOC_OPENCV_AVAILABLE OFF)
if(NOT FSOC_ENABLE_OPENCV STREQUAL "OFF")
    if(FSOC_ENABLE_OPENCV STREQUAL "ON")
        find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs dnn videoio)
    else()
        find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs dnn videoio)
    endif()
    if(OpenCV_FOUND)
        set(FSOC_OPENCV_AVAILABLE ON)
        message(STATUS "FSOC: OpenCV ${OpenCV_VERSION} found (${OpenCV_DIR}) - building Step 4 renderer + Step 5 detector")
    else()
        message(STATUS "FSOC: OpenCV not found - Step 4/5 image modules disabled. Install it with: brew install opencv")
    endif()
endif()

add_executable(step1_math_smoke apps/step1_math_smoke.cpp)
target_link_libraries(step1_math_smoke PRIVATE fsoc::core)
fsoc_set_project_warnings(step1_math_smoke)
fsoc_enable_sanitizers(step1_math_smoke)

add_executable(step2_trajectory_smoke apps/step2_trajectory_smoke.cpp)
target_link_libraries(step2_trajectory_smoke PRIVATE fsoc::core)
fsoc_set_project_warnings(step2_trajectory_smoke)
fsoc_enable_sanitizers(step2_trajectory_smoke)

add_executable(step3_observation_smoke apps/step3_observation_smoke.cpp)
target_link_libraries(step3_observation_smoke PRIVATE fsoc::core)
fsoc_set_project_warnings(step3_observation_smoke)
fsoc_enable_sanitizers(step3_observation_smoke)

add_executable(step6_pid_smoke apps/step6_pid_smoke.cpp)
target_link_libraries(step6_pid_smoke PRIVATE fsoc::control)
fsoc_set_project_warnings(step6_pid_smoke)
fsoc_enable_sanitizers(step6_pid_smoke)

# ---------------------------------------------------------------------------
# fsoc_render — synthetic image renderer (OpenCV-dependent; Step 4)
# ---------------------------------------------------------------------------
if(FSOC_OPENCV_AVAILABLE)
    add_library(fsoc_render src/renderer.cpp src/demo_disturbance.cpp)
    add_library(fsoc::render ALIAS fsoc_render)
    target_include_directories(fsoc_render
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${OpenCV_INCLUDE_DIRS}
    )
    target_compile_features(fsoc_render PUBLIC cxx_std_20)
    target_link_libraries(fsoc_render PUBLIC fsoc::core ${OpenCV_LIBS})
    fsoc_set_project_warnings(fsoc_render)
    fsoc_enable_sanitizers(fsoc_render)

    add_executable(step4_renderer_smoke apps/step4_renderer_smoke.cpp)
    target_link_libraries(step4_renderer_smoke PRIVATE fsoc::render)
    fsoc_set_project_warnings(step4_renderer_smoke)
    fsoc_enable_sanitizers(step4_renderer_smoke)

    # -----------------------------------------------------------------------
    # fsoc_perception — baseline beacon detector (Step 5)
    # Depends on fsoc::core + OpenCV (core, imgproc). MUST NOT depend on fsoc_render.
    # -----------------------------------------------------------------------
    add_library(fsoc_perception src/detector.cpp)
    add_library(fsoc::perception ALIAS fsoc_perception)
    target_include_directories(fsoc_perception
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${OpenCV_INCLUDE_DIRS}
    )
    target_compile_features(fsoc_perception PUBLIC cxx_std_20)
    target_link_libraries(fsoc_perception PUBLIC fsoc::core opencv_core opencv_imgproc)
    fsoc_set_project_warnings(fsoc_perception)
    fsoc_enable_sanitizers(fsoc_perception)

    add_executable(step5_detector_smoke apps/step5_detector_smoke.cpp)
    target_link_libraries(step5_detector_smoke PRIVATE fsoc::perception fsoc::render)
    fsoc_set_project_warnings(step5_detector_smoke)
    fsoc_enable_sanitizers(step5_detector_smoke)

    # -----------------------------------------------------------------------
    # fsoc_ai_perception — Stage-3 learned detector + Safe Hybrid seam.
    # AiBeaconDetector (OpenCV-DNN / ONNX, TinyBeaconNet) + resolve_perception
    # (ADR-018 policy, pure decision function). Depends on fsoc::core + OpenCV
    # (core, imgproc, dnn). MUST NOT depend on fsoc_render.
    # -----------------------------------------------------------------------
    add_library(fsoc_ai_perception src/ai_beacon_detector.cpp src/perception.cpp)
    add_library(fsoc::ai_perception ALIAS fsoc_ai_perception)
    target_include_directories(fsoc_ai_perception
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${OpenCV_INCLUDE_DIRS}
    )
    target_compile_features(fsoc_ai_perception PUBLIC cxx_std_20)
    target_link_libraries(fsoc_ai_perception PUBLIC fsoc::core opencv_core opencv_imgproc opencv_dnn)
    fsoc_set_project_warnings(fsoc_ai_perception)
    fsoc_enable_sanitizers(fsoc_ai_perception)

    # -----------------------------------------------------------------------
    # fsoc_frame_source — Mobile Phone Camera-in-the-Loop milestone.
    # Pure I/O boundary (FrameSource interface + cv::VideoCapture adapter).
    # Depends ONLY on OpenCV core + videoio. MUST NOT depend on fsoc::core,
    # fsoc::perception, fsoc::ai_perception, or fsoc::render: a frame source
    # knows nothing about detection, perception mode, or world truth.
    # -----------------------------------------------------------------------
    if(TARGET opencv_videoio)
        add_library(fsoc_frame_source src/frame_source.cpp src/opencv_camera_frame_source.cpp)
        add_library(fsoc::frame_source ALIAS fsoc_frame_source)
        target_include_directories(fsoc_frame_source
            PUBLIC
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:include>
                ${OpenCV_INCLUDE_DIRS}
        )
        target_compile_features(fsoc_frame_source PUBLIC cxx_std_20)
        target_link_libraries(fsoc_frame_source PUBLIC opencv_core opencv_videoio)
        fsoc_set_project_warnings(fsoc_frame_source)
        fsoc_enable_sanitizers(fsoc_frame_source)
        set(FSOC_FRAME_SOURCE_AVAILABLE ON)
        message(STATUS "FSOC: OpenCV videoio present - phone-camera-in-the-loop targets enabled")
    else()
        set(FSOC_FRAME_SOURCE_AVAILABLE OFF)
        message(STATUS "FSOC: OpenCV videoio absent - phone-camera-in-the-loop targets disabled")
    endif()

    if(FSOC_FRAME_SOURCE_AVAILABLE)
        # -------------------------------------------------------------------
        # fsoc_live_frame_publisher — atomic, frame-identity-safe local file
        # transport for fsoc_live's long-running (no finite CSV) output. Pure
        # I/O sink: depends only on OpenCV core/imgcodecs, never on
        # fsoc::core/perception/control. See docs/LIVE_DATA_AUDIT.md section 2
        # for the gap this closes.
        # -------------------------------------------------------------------
        add_library(fsoc_live_frame_publisher src/live_frame_publisher.cpp)
        add_library(fsoc::live_frame_publisher ALIAS fsoc_live_frame_publisher)
        target_include_directories(fsoc_live_frame_publisher
            PUBLIC
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:include>
                ${OpenCV_INCLUDE_DIRS}
        )
        target_compile_features(fsoc_live_frame_publisher PUBLIC cxx_std_20)
        target_link_libraries(fsoc_live_frame_publisher PUBLIC opencv_core opencv_imgcodecs)
        fsoc_set_project_warnings(fsoc_live_frame_publisher)
        fsoc_enable_sanitizers(fsoc_live_frame_publisher)

        # -------------------------------------------------------------------
        # fsoc_live_support — real-camera perception/tracking/virtual-actuator
        # orchestration (LiveTrackingSession), the real-camera counterpart to
        # fsoc_simulation (which similarly backs fsoc_demo via
        # fsoc_demo_support). Depends on fsoc::core (camera geometry, target
        # tracker), fsoc::control (PID), fsoc::perception (classical
        # detector), fsoc::ai_perception (AI detector + Safe Hybrid), and
        # fsoc::frame_source (Frame/FrameSourceInfo types only — never
        # constructs a FrameSource itself; that stays the CLI app's job).
        # -------------------------------------------------------------------
        add_library(fsoc_live_support
            src/live_preprocessing.cpp
            src/live_tracking_session.cpp
        )
        add_library(fsoc::live_support ALIAS fsoc_live_support)
        target_include_directories(fsoc_live_support
            PUBLIC
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:include>
                ${OpenCV_INCLUDE_DIRS}
        )
        target_compile_features(fsoc_live_support PUBLIC cxx_std_20)
        target_link_libraries(fsoc_live_support
            PUBLIC fsoc::core fsoc::control fsoc::perception fsoc::ai_perception fsoc::frame_source
            fsoc::camera_calibration fsoc::virtual_actuator opencv_imgproc)
        fsoc_set_project_warnings(fsoc_live_support)
        fsoc_enable_sanitizers(fsoc_live_support)

        add_executable(fsoc_camera_probe apps/fsoc_camera_probe.cpp)
        target_link_libraries(fsoc_camera_probe PRIVATE fsoc::frame_source)
        fsoc_set_project_warnings(fsoc_camera_probe)
        fsoc_enable_sanitizers(fsoc_camera_probe)

        add_executable(fsoc_camera_view apps/fsoc_camera_view.cpp)
        target_link_libraries(fsoc_camera_view PRIVATE fsoc::frame_source opencv_imgcodecs opencv_imgproc)
        fsoc_set_project_warnings(fsoc_camera_view)
        fsoc_enable_sanitizers(fsoc_camera_view)

        add_executable(fsoc_live apps/fsoc_live.cpp)
        target_link_libraries(fsoc_live PRIVATE fsoc::live_support fsoc::camera_calibration
            fsoc::live_frame_publisher opencv_imgcodecs opencv_imgproc)
        fsoc_set_project_warnings(fsoc_live)
        fsoc_enable_sanitizers(fsoc_live)
    endif()

    # -----------------------------------------------------------------------
    # fsoc_simulation — the deterministic closed-loop integration layer (Step 7).
    # This is the ONE place the tested modules come together.
    # -----------------------------------------------------------------------
    add_library(fsoc_simulation src/simulation_runner.cpp)
    add_library(fsoc::simulation ALIAS fsoc_simulation)
    target_include_directories(fsoc_simulation
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${OpenCV_INCLUDE_DIRS}
    )
    target_compile_features(fsoc_simulation PUBLIC cxx_std_20)
    target_link_libraries(fsoc_simulation
        PUBLIC fsoc::core fsoc::render fsoc::perception fsoc::control fsoc::ai_perception)
    fsoc_set_project_warnings(fsoc_simulation)
    fsoc_enable_sanitizers(fsoc_simulation)

    add_executable(step7_closed_loop_smoke apps/step7_closed_loop_smoke.cpp)
    target_link_libraries(step7_closed_loop_smoke PRIVATE fsoc::simulation)
    fsoc_set_project_warnings(step7_closed_loop_smoke)
    fsoc_enable_sanitizers(step7_closed_loop_smoke)

    # -----------------------------------------------------------------------
    # fsoc_telemetry — observer-only telemetry + benchmarking (Step 8).
    # Consumes SimulationStepResult; never reaches back into the loop.
    # -----------------------------------------------------------------------
    add_library(fsoc_telemetry src/telemetry.cpp)
    add_library(fsoc::telemetry ALIAS fsoc_telemetry)
    target_include_directories(fsoc_telemetry
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${OpenCV_INCLUDE_DIRS}
    )
    target_compile_features(fsoc_telemetry PUBLIC cxx_std_20)
    target_link_libraries(fsoc_telemetry PUBLIC fsoc::simulation)
    fsoc_set_project_warnings(fsoc_telemetry)
    fsoc_enable_sanitizers(fsoc_telemetry)

    add_executable(step8_telemetry_smoke apps/step8_telemetry_smoke.cpp)
    target_link_libraries(step8_telemetry_smoke PRIVATE fsoc::telemetry)
    fsoc_set_project_warnings(step8_telemetry_smoke)
    fsoc_enable_sanitizers(step8_telemetry_smoke)

    # -----------------------------------------------------------------------
    # fsoc_visualization — engineering camera-view debug overlay (Step 9).
    # Observer-only: annotates a COPY; the perception frame is never touched.
    # -----------------------------------------------------------------------
    add_library(fsoc_visualization src/visualization.cpp)
    add_library(fsoc::visualization ALIAS fsoc_visualization)
    target_include_directories(fsoc_visualization
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${OpenCV_INCLUDE_DIRS}
    )
    target_compile_features(fsoc_visualization PUBLIC cxx_std_20)
    target_link_libraries(fsoc_visualization
        PUBLIC fsoc::simulation fsoc::telemetry opencv_core opencv_imgproc opencv_imgcodecs)
    # Optional MP4 export — only if this OpenCV build has videoio.
    if(TARGET opencv_videoio)
        target_link_libraries(fsoc_visualization PUBLIC opencv_videoio)
        target_compile_definitions(fsoc_visualization PUBLIC FSOC_HAS_VIDEOIO=1)
        message(STATUS "FSOC: OpenCV videoio present - Step 9 optional MP4 export enabled")
    else()
        message(STATUS "FSOC: OpenCV videoio absent - Step 9 uses the PNG path only")
    endif()
    fsoc_set_project_warnings(fsoc_visualization)
    fsoc_enable_sanitizers(fsoc_visualization)

    add_executable(step9_visualization_smoke apps/step9_visualization_smoke.cpp)
    target_link_libraries(step9_visualization_smoke PRIVATE fsoc::visualization)
    fsoc_set_project_warnings(step9_visualization_smoke)
    fsoc_enable_sanitizers(step9_visualization_smoke)

    # -----------------------------------------------------------------------
    # fsoc_validation — baseline acceptance / validation suite (Step 10).
    # Evaluation layer: runs the existing system, checks documented gates.
    # -----------------------------------------------------------------------
    add_library(fsoc_validation src/validation.cpp)
    add_library(fsoc::validation ALIAS fsoc_validation)
    target_include_directories(fsoc_validation
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${OpenCV_INCLUDE_DIRS}
    )
    target_compile_features(fsoc_validation PUBLIC cxx_std_20)
    target_link_libraries(fsoc_validation
        PUBLIC fsoc::simulation fsoc::telemetry fsoc::visualization)
    fsoc_set_project_warnings(fsoc_validation)
    fsoc_enable_sanitizers(fsoc_validation)

    add_executable(step10_validation_smoke apps/step10_validation_smoke.cpp)
    target_link_libraries(step10_validation_smoke PRIVATE fsoc::validation)
    fsoc_set_project_warnings(step10_validation_smoke)
    fsoc_enable_sanitizers(step10_validation_smoke)

    # -----------------------------------------------------------------------
    # fsoc_demo_support — demo/frontend packaging layer (Step 11). ADDITIVE.
    # Pure observer: copies SimulationStepResult + TelemetryRecord into a
    # frontend-oriented DemoSnapshot; owns DemoScenario presets + DemoSession.
    # No physics, no control, no networking, no JSON.
    # -----------------------------------------------------------------------
    add_library(fsoc_demo_support src/demo.cpp)
    add_library(fsoc::demo_support ALIAS fsoc_demo_support)
    target_include_directories(fsoc_demo_support
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${OpenCV_INCLUDE_DIRS}
    )
    target_compile_features(fsoc_demo_support PUBLIC cxx_std_20)
    target_link_libraries(fsoc_demo_support PUBLIC fsoc::simulation fsoc::telemetry)
    fsoc_set_project_warnings(fsoc_demo_support)
    fsoc_enable_sanitizers(fsoc_demo_support)

    add_executable(fsoc_demo apps/fsoc_demo.cpp)
    target_link_libraries(fsoc_demo PRIVATE fsoc::demo_support)
    fsoc_set_project_warnings(fsoc_demo)
    fsoc_enable_sanitizers(fsoc_demo)

    # =======================================================================
    # V2 AI PERCEPTION (post-v1_baseline; additive — no frozen module changes)
    # =======================================================================

    # -----------------------------------------------------------------------
    # fsoc_ai_datagen — domain-randomized synthetic frame synthesizer for
    # TinyBeaconNet dataset generation + AI evaluation. Depends on fsoc::core +
    # OpenCV core/imgproc only. MUST NOT depend on fsoc_render / fsoc_perception:
    # it re-uses the analytic Gaussian beacon model, not renderer code.
    # -----------------------------------------------------------------------
    add_library(fsoc_ai_datagen src/ai_frame_synth.cpp)
    add_library(fsoc::ai_datagen ALIAS fsoc_ai_datagen)
    target_include_directories(fsoc_ai_datagen
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${OpenCV_INCLUDE_DIRS}
    )
    target_compile_features(fsoc_ai_datagen PUBLIC cxx_std_20)
    target_link_libraries(fsoc_ai_datagen PUBLIC fsoc::core opencv_core opencv_imgproc)
    fsoc_set_project_warnings(fsoc_ai_datagen)
    fsoc_enable_sanitizers(fsoc_ai_datagen)

    add_executable(generate_ai_dataset apps/generate_ai_dataset.cpp)
    target_link_libraries(generate_ai_dataset PRIVATE fsoc::ai_datagen opencv_imgcodecs)
    fsoc_set_project_warnings(generate_ai_dataset)
    fsoc_enable_sanitizers(generate_ai_dataset)

    # -----------------------------------------------------------------------
    # ai_inference_benchmark — Stage-3 C++ OpenCV-DNN latency benchmark.
    # Loads the model once (excluded from timing), warms up, then times
    # >= 500 detect() calls. See docs/19 + Stage-3 checkpoint report §15.
    # -----------------------------------------------------------------------
    add_executable(ai_inference_benchmark apps/ai_inference_benchmark.cpp)
    target_link_libraries(ai_inference_benchmark PRIVATE fsoc::ai_perception fsoc::render)
    fsoc_set_project_warnings(ai_inference_benchmark)
    fsoc_enable_sanitizers(ai_inference_benchmark)

    # -----------------------------------------------------------------------
    # fsoc_stage4_eval — Stage-4 Classical/AI/Hybrid evaluation (additive,
    # evaluation-only). Depends on fsoc::ai_perception (detectors + Hybrid
    # policy), fsoc::ai_datagen (common-frame degraded synthesis), fsoc::render
    # + fsoc::core + fsoc::control (closed-loop degraded benchmark composes
    # PanTiltCamera/Trajectory/PIDController/SyntheticCameraRenderer directly
    # -- it does NOT link fsoc::simulation and does NOT reuse SimulationRunner,
    # see include/fsoc/stage4_closed_loop_bench.hpp).
    # -----------------------------------------------------------------------
    add_library(fsoc_stage4_eval
        src/stage4_degradation.cpp
        src/stage4_metrics.cpp
        src/stage4_scenarios.cpp
        src/stage4_common_frame_bench.cpp
        src/stage4_closed_loop_bench.cpp
    )
    add_library(fsoc::stage4_eval ALIAS fsoc_stage4_eval)
    target_include_directories(fsoc_stage4_eval
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${OpenCV_INCLUDE_DIRS}
    )
    target_compile_features(fsoc_stage4_eval PUBLIC cxx_std_20)
    target_link_libraries(fsoc_stage4_eval
        PUBLIC fsoc::core fsoc::control fsoc::render fsoc::perception fsoc::ai_perception fsoc::ai_datagen)
    fsoc_set_project_warnings(fsoc_stage4_eval)
    fsoc_enable_sanitizers(fsoc_stage4_eval)

    add_executable(stage4_evaluation apps/stage4_evaluation.cpp)
    target_link_libraries(stage4_evaluation PRIVATE fsoc::stage4_eval opencv_imgcodecs opencv_imgproc)
    fsoc_set_project_warnings(stage4_evaluation)
    fsoc_enable_sanitizers(stage4_evaluation)

    add_executable(stage4_tracker_ablation apps/stage4_tracker_ablation.cpp)
    target_link_libraries(stage4_tracker_ablation PRIVATE fsoc::stage4_eval)
    fsoc_set_project_warnings(stage4_tracker_ablation)
    fsoc_enable_sanitizers(stage4_tracker_ablation)

    add_executable(mvp_dynamic_scenarios apps/mvp_dynamic_scenarios.cpp)
    target_link_libraries(mvp_dynamic_scenarios PRIVATE fsoc::stage4_eval)
    fsoc_set_project_warnings(mvp_dynamic_scenarios)
    fsoc_enable_sanitizers(mvp_dynamic_scenarios)

    add_executable(mvp_latency_budget apps/mvp_latency_budget.cpp)
    target_link_libraries(mvp_latency_budget PRIVATE fsoc::demo_support)
    fsoc_set_project_warnings(mvp_latency_budget)
    fsoc_enable_sanitizers(mvp_latency_budget)
endif()

if(FSOC_BUILD_TESTS)
    enable_testing()

    add_executable(fsoc_step1_tests tests/step1_tests.cpp)
    target_link_libraries(fsoc_step1_tests PRIVATE fsoc::core)
    fsoc_set_project_warnings(fsoc_step1_tests)
    fsoc_enable_sanitizers(fsoc_step1_tests)
    add_test(NAME fsoc_step1_tests COMMAND fsoc_step1_tests)

    add_executable(fsoc_step2_tests tests/step2_tests.cpp)
    target_link_libraries(fsoc_step2_tests PRIVATE fsoc::core)
    fsoc_set_project_warnings(fsoc_step2_tests)
    fsoc_enable_sanitizers(fsoc_step2_tests)
    add_test(NAME fsoc_step2_tests COMMAND fsoc_step2_tests)

    add_executable(fsoc_step3_tests tests/step3_tests.cpp)
    target_link_libraries(fsoc_step3_tests PRIVATE fsoc::core)
    fsoc_set_project_warnings(fsoc_step3_tests)
    fsoc_enable_sanitizers(fsoc_step3_tests)
    add_test(NAME fsoc_step3_tests COMMAND fsoc_step3_tests)

    add_executable(fsoc_step6_tests tests/step6_tests.cpp)
    target_link_libraries(fsoc_step6_tests PRIVATE fsoc::control)
    fsoc_set_project_warnings(fsoc_step6_tests)
    fsoc_enable_sanitizers(fsoc_step6_tests)
    add_test(NAME fsoc_step6_tests COMMAND fsoc_step6_tests)

    add_executable(fsoc_target_tracker_tests tests/target_tracker_tests.cpp)
    target_link_libraries(fsoc_target_tracker_tests PRIVATE fsoc::core)
    fsoc_set_project_warnings(fsoc_target_tracker_tests)
    fsoc_enable_sanitizers(fsoc_target_tracker_tests)
    add_test(NAME fsoc_target_tracker_tests COMMAND fsoc_target_tracker_tests)

    # --- Mobile Phone Camera-in-the-Loop: OpenCV-free pieces, unconditional ---
    add_executable(fsoc_virtual_actuator_tests tests/virtual_actuator_tests.cpp)
    target_link_libraries(fsoc_virtual_actuator_tests PRIVATE fsoc::virtual_actuator)
    fsoc_set_project_warnings(fsoc_virtual_actuator_tests)
    fsoc_enable_sanitizers(fsoc_virtual_actuator_tests)
    add_test(NAME fsoc_virtual_actuator_tests COMMAND fsoc_virtual_actuator_tests)

    add_executable(fsoc_live_camera_calibration_tests tests/live_camera_calibration_tests.cpp)
    target_link_libraries(fsoc_live_camera_calibration_tests PRIVATE fsoc::camera_calibration)
    fsoc_set_project_warnings(fsoc_live_camera_calibration_tests)
    fsoc_enable_sanitizers(fsoc_live_camera_calibration_tests)
    add_test(NAME fsoc_live_camera_calibration_tests COMMAND fsoc_live_camera_calibration_tests)

    if(FSOC_OPENCV_AVAILABLE)
        add_executable(fsoc_step4_tests tests/step4_tests.cpp)
        target_link_libraries(fsoc_step4_tests PRIVATE fsoc::render)
        fsoc_set_project_warnings(fsoc_step4_tests)
        fsoc_enable_sanitizers(fsoc_step4_tests)
        add_test(NAME fsoc_step4_tests COMMAND fsoc_step4_tests)

        add_executable(fsoc_step5_tests tests/step5_tests.cpp)
        target_link_libraries(fsoc_step5_tests PRIVATE fsoc::perception fsoc::render)
        fsoc_set_project_warnings(fsoc_step5_tests)
        fsoc_enable_sanitizers(fsoc_step5_tests)
        add_test(NAME fsoc_step5_tests COMMAND fsoc_step5_tests)

        add_executable(fsoc_step7_tests tests/step7_tests.cpp)
        target_link_libraries(fsoc_step7_tests PRIVATE fsoc::simulation)
        fsoc_set_project_warnings(fsoc_step7_tests)
        fsoc_enable_sanitizers(fsoc_step7_tests)
        add_test(NAME fsoc_step7_tests COMMAND fsoc_step7_tests)

        add_executable(fsoc_step8_tests tests/step8_tests.cpp)
        target_link_libraries(fsoc_step8_tests PRIVATE fsoc::telemetry)
        fsoc_set_project_warnings(fsoc_step8_tests)
        fsoc_enable_sanitizers(fsoc_step8_tests)
        add_test(NAME fsoc_step8_tests COMMAND fsoc_step8_tests)

        add_executable(fsoc_step9_tests tests/step9_tests.cpp)
        target_link_libraries(fsoc_step9_tests PRIVATE fsoc::visualization)
        fsoc_set_project_warnings(fsoc_step9_tests)
        fsoc_enable_sanitizers(fsoc_step9_tests)
        add_test(NAME fsoc_step9_tests COMMAND fsoc_step9_tests)

        add_executable(fsoc_step10_tests tests/step10_tests.cpp)
        target_link_libraries(fsoc_step10_tests PRIVATE fsoc::validation)
        fsoc_set_project_warnings(fsoc_step10_tests)
        fsoc_enable_sanitizers(fsoc_step10_tests)
        add_test(NAME fsoc_step10_tests COMMAND fsoc_step10_tests)

        add_executable(fsoc_step11_tests tests/step11_tests.cpp)
        target_link_libraries(fsoc_step11_tests PRIVATE fsoc::demo_support fsoc::validation)
        target_compile_definitions(fsoc_step11_tests
            PRIVATE FSOC_PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
        fsoc_set_project_warnings(fsoc_step11_tests)
        fsoc_enable_sanitizers(fsoc_step11_tests)
        add_test(NAME fsoc_step11_tests COMMAND fsoc_step11_tests)

        # --- V2 AI PERCEPTION tests ---
        add_executable(fsoc_ai_datagen_tests tests/ai_datagen_tests.cpp)
        target_link_libraries(fsoc_ai_datagen_tests PRIVATE fsoc::ai_datagen opencv_core)
        fsoc_set_project_warnings(fsoc_ai_datagen_tests)
        fsoc_enable_sanitizers(fsoc_ai_datagen_tests)
        add_test(NAME fsoc_ai_datagen_tests COMMAND fsoc_ai_datagen_tests)

        # --- Stage 3: C++ AI detector (model load / preprocessing / decode /
        # parity / robustness / determinism). Needs models/ + tests/fixtures/
        # ai_parity/ resolved relative to the project root, not the build dir
        # or CWD, so another clone builds/runs this unmodified.
        add_executable(fsoc_ai_beacon_detector_tests tests/ai_beacon_detector_tests.cpp)
        target_link_libraries(fsoc_ai_beacon_detector_tests
            PRIVATE fsoc::ai_perception opencv_imgcodecs opencv_imgproc)
        target_compile_definitions(fsoc_ai_beacon_detector_tests
            PRIVATE FSOC_PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
        fsoc_set_project_warnings(fsoc_ai_beacon_detector_tests)
        fsoc_enable_sanitizers(fsoc_ai_beacon_detector_tests)
        add_test(NAME fsoc_ai_beacon_detector_tests COMMAND fsoc_ai_beacon_detector_tests)

        # --- Stage 3: Safe Hybrid policy (ADR-018) — pure resolve_perception
        # decision-table tests. No model / ONNX / filesystem dependency.
        add_executable(fsoc_hybrid_perception_tests tests/hybrid_perception_tests.cpp)
        target_link_libraries(fsoc_hybrid_perception_tests PRIVATE fsoc::ai_perception)
        fsoc_set_project_warnings(fsoc_hybrid_perception_tests)
        fsoc_enable_sanitizers(fsoc_hybrid_perception_tests)
        add_test(NAME fsoc_hybrid_perception_tests COMMAND fsoc_hybrid_perception_tests)

        # --- Stage 3: default-Classical SimulationRunner regression + full
        # CLASSICAL/AI/HYBRID SimulationRunner integration.
        add_executable(fsoc_perception_integration_tests tests/perception_integration_tests.cpp)
        target_link_libraries(fsoc_perception_integration_tests PRIVATE fsoc::simulation)
        target_compile_definitions(fsoc_perception_integration_tests
            PRIVATE FSOC_PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
        fsoc_set_project_warnings(fsoc_perception_integration_tests)
        fsoc_enable_sanitizers(fsoc_perception_integration_tests)
        add_test(NAME fsoc_perception_integration_tests COMMAND fsoc_perception_integration_tests)

        # --- Stage 4: determinism of scenario/degradation/benchmark tooling ---
        add_executable(fsoc_stage4_determinism_tests tests/stage4_determinism_tests.cpp)
        target_link_libraries(fsoc_stage4_determinism_tests PRIVATE fsoc::stage4_eval)
        target_compile_definitions(fsoc_stage4_determinism_tests
            PRIVATE FSOC_PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
        fsoc_set_project_warnings(fsoc_stage4_determinism_tests)
        fsoc_enable_sanitizers(fsoc_stage4_determinism_tests)
        add_test(NAME fsoc_stage4_determinism_tests COMMAND fsoc_stage4_determinism_tests)

        # --- Mobile Phone Camera-in-the-Loop: OpenCV/videoio-dependent pieces ---
        if(FSOC_FRAME_SOURCE_AVAILABLE)
            add_executable(fsoc_frame_source_tests tests/frame_source_tests.cpp)
            target_link_libraries(fsoc_frame_source_tests PRIVATE fsoc::frame_source)
            fsoc_set_project_warnings(fsoc_frame_source_tests)
            fsoc_enable_sanitizers(fsoc_frame_source_tests)
            add_test(NAME fsoc_frame_source_tests COMMAND fsoc_frame_source_tests)

            add_executable(fsoc_live_preprocessing_tests tests/live_preprocessing_tests.cpp)
            target_link_libraries(fsoc_live_preprocessing_tests PRIVATE fsoc::live_support)
            fsoc_set_project_warnings(fsoc_live_preprocessing_tests)
            fsoc_enable_sanitizers(fsoc_live_preprocessing_tests)
            add_test(NAME fsoc_live_preprocessing_tests COMMAND fsoc_live_preprocessing_tests)

            add_executable(fsoc_live_tracking_session_tests tests/live_tracking_session_tests.cpp)
            target_link_libraries(fsoc_live_tracking_session_tests PRIVATE fsoc::live_support)
            fsoc_set_project_warnings(fsoc_live_tracking_session_tests)
            fsoc_enable_sanitizers(fsoc_live_tracking_session_tests)
            add_test(NAME fsoc_live_tracking_session_tests COMMAND fsoc_live_tracking_session_tests)

            add_executable(fsoc_live_frame_publisher_tests tests/live_frame_publisher_tests.cpp)
            target_link_libraries(fsoc_live_frame_publisher_tests PRIVATE fsoc::live_frame_publisher)
            fsoc_set_project_warnings(fsoc_live_frame_publisher_tests)
            fsoc_enable_sanitizers(fsoc_live_frame_publisher_tests)
            add_test(NAME fsoc_live_frame_publisher_tests COMMAND fsoc_live_frame_publisher_tests)
        endif()
    endif()
endif()
