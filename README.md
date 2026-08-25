# Real-time telemetry source overlay

This small directory contains the complete modified files and a cloud build
pipeline for the native telemetry fork. It is an overlay for the official
public `ange-yaghi/engine-sim` source at commit
`80a9075bf34f53cd03a59c01883bcb5ec91740f5` (v0.1.11a). It is not a
patch for the closed v0.1.14a binary.

## Recommended: build with GitHub Actions

1. Create an empty GitHub repository (public avoids hosted-runner charges).
2. Upload the **contents** of this directory, including `.github/`.
3. In GitHub, open **Actions** and select
   **Build engine-sim real-time telemetry**.
4. Choose **Run workflow**.
5. When the job finishes, download the artifact named
   `engine-sim-telemetry-windows-x64`.

The repository upload is about 150 KB. The runner downloads the pinned public
source and all build dependencies, applies this overlay, compiles with MSVC,
runs real HTTP smoke tests for `/health`, `/telemetry`, `/trace` and the
loopback-only `/control` endpoint, validates the dashboard and verifies that
the portable package cannot import an incompatible parent `es` directory. No
Visual Studio installation is required on the local PC.

## Apply

1. Clone the official repository recursively at the commit above.
2. Copy this directory's `CMakeLists.txt`, `include/` and `src/` into that
   clone, preserving paths and replacing the matching files.
3. Configure a Windows x64 MSVC build, preferably with
   `-DDISCORD_ENABLED=OFF -DDTV=OFF`.
4. Build `engine-sim-app` and copy its required runtime DLLs/assets alongside
   the new executable.

The overlay adds a loopback-only HTTP server, publishes native simulator data
once per frame (including while paused), records pressure and volume together
over 256 crank-cycle positions, and links the Windows `ws2_32` library. The
dashboard can safely control throttle, dynamometer braking/hold, ignition,
pause, rev limiter and a bounded ignition-advance correction. Remote control
is opt-in and disabling it releases the dyno and restores the original limiter.

The LIVE chart laboratory retains up to 120 seconds of samples, supports
configurable X/Y metrics, linear or moving-average interpolation, independent
left/right axes, derived BMEP, and CSV/PNG export.

For a local build, run `tools/check-realtime-build.ps1` from the runtime bundle
before attempting the full build. See `REALTIME_TELEMETRY.md` for prerequisites
and current verification status.
