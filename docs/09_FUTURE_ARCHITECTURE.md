# Future Architecture After `v1_baseline`

Upgrade in controlled layers:

1. **State estimation:** add Eigen and implement EKF/UKF behind an estimator interface.
2. **Motion prediction:** future LOS/centroid prediction with uncertainty.
3. **Control:** add constrained MPC alongside PID, not as a destructive rewrite.
4. **Disturbance model:** vibration from PSD-shaped stochastic processes.
5. **Atmospheric optics:** Zernike/phase-screen approximations at the appropriate observation layer.
6. **Detection:** spatio-temporal anomaly detector for unresolved targets; lightweight CNN only for resolved targets where justified.
7. **Actuator realism:** acceleration, latency, backlash, quantization.
8. **Monte Carlo validation:** seeded scenario sweeps and comparative plots.

Keep PID as the interpretable reference baseline throughout judging.

## Real-camera sensing (implemented) and physical actuation (still future)

The Mobile Phone Camera-in-the-Loop milestone made the **sensing** side of this list
partially real ahead of schedule: `FrameSource` (`fsoc/frame_source.hpp`) is exactly the
swappable-interface boundary this section anticipated for camera input, and
`LiveTrackingSession` proves the existing Classical/AI/Hybrid perception, P0-v2 state
estimator, and PID controller all already worked unchanged on real frames. **Actuator
realism (item 7) is still entirely future** — `VirtualPanTiltActuator` is honest bookkeeping,
not hardware. See `docs/PHONE_CAMERA_METRICS.md` for the full architecture, the hardware-
ready `PanTiltActuator` interface sketch, and the rate-vs-position command mismatch a future
serial/servo adapter will need to solve.
