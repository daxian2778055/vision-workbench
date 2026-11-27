# Performance baseline and acceptance

> Version: 1.0.0 (same as the CMake project version)
> Scope: end-to-end pipeline from image input to result output; does not include a real camera capture thread.

## 1. Why this document exists

Functional gates prove correctness and fail-closed behavior, not speed. This project turns performance readings into a reproducible, same-host relative-regression gate instead of an absolute millisecond line that would only produce false alarms on different CPUs, memory and load.

## 2. Measurement contract

- Test seat: `testPipelineBench()` in `tests/soak_test.cpp`, carried by ctest's `SoakTest`.
- Chain: a local synthetic 640x480 8-bit image (read from disk -> HALCON image -> OpenCV morphology -> Blob -> formula -> conditional count -> delay), 6 nodes, neighbors connected by port compatibility.
- Scheduling: `FlowMode::Continuous`, `loopIntervalMs=0` (full speed), executor's own stats logging disabled.
- Warm-up: idle for 1 second after start to exclude first-frame read/lazy-init cost from the window.
- Window: 8 seconds by default (`VFP_BENCH_SECONDS`), executor stats snapshot polled every 100 ms; does not depend on the performance panel UI.
- Two arms: `VFP_BENCH_DB=0/1` measure without and with per-round batched DB writes. The singleton cannot switch paths inside one process, so the script starts the executable twice.
- Output: one ASCII `BENCH-RESULT ...` line, stably parsed by `tools/measure_pipeline_bench.py`; optionally appended to `VFP_BENCH_REPORT`.

## 3. Frozen baseline (2026-11-27, measured on this host)

| Arm | rounds/s | wall ms per round | process CPU ms per round |
|---|---:|---:|---:|
| no DB | 144.65 | 6.390 | 14.751 |
| with DB | 134.82 | 6.891 | 15.133 |

These numbers are **not** acceptance targets; they are reference readings frozen on this host. Re-freeze explicitly when moving to another machine; never impose them on a field machine.

## 4. Gate semantics

- Entry: `tools/ci.ps1` runs `tools/measure_pipeline_bench.py` after all ctest suites pass; failure exits with code **20**.
- Judgement (only significant regressions are red):
  - wall ms per round <= baseline x 1.30
  - throughput >= baseline x 0.75
  - CPU ms per round <= baseline x 1.50
  - structural hard conditions: complete reading, >=3 rounds in the window, zero failed rounds, CPU sampling advanced
- Self-proof: `python tools/measure_pipeline_bench.py --selfcheck` proves every judgement can fail on its own, so GREEN cannot mean "not wired".
- Re-freeze: after a performance-affecting change, run both arms with `--seconds 8`, write the new numbers plus date and reason into `BASELINE` in the script, and update the table above. Widening tolerances alone is not a re-freeze.

## 5. Low-risk optimizations already landed

- Round-end send drain uses a condition variable instead of a 2 ms busy poll (`CommunicationNodeBase`).
- Performance panel still accumulates per-node statistics, but table refresh is throttled to 100 ms to avoid flooding the GUI event queue at high frame rates.
- `flowExecutionTime` now has an emit point, so the panel's flow-duration label is no longer a dead wire.

## 6. Not covered / follow-up

- **Real MVS camera capture**: this benchmark uses local images and does not represent the MVS acquisition path. Making capture asynchronous must first solve shared global-handle frame stealing, handle-destroy UAF, single-buffer races, and the 1:1 gating semantics of software/hardware triggers. It is not wired into the product path without hardware-in-the-loop.
- **Pure data plane ready**: `LatestFrameQueue` (bounded, latest-frame-wins, frame-number gaps/duplicates, local replacement separated from SDK packet loss) has its own unit test and can be wired once a camera is available.
- **Format conversions**: OpenCV nodes still pay repeated full-frame HImage<->Mat conversions; cross-node buffer reuse is an architecture-level change and has not been attempted.
- **Absolute acceptance targets**: require product/line inputs for target FPS, P99 frame latency and a continuous-run memory ceiling before the relative gate can be upgraded to absolute acceptance.
