# Real-time telemetry source overlay

This small directory contains the complete modified files and a cloud build
pipeline for the native telemetry fork. It is an overlay for the official
public `ange-yaghi/engine-sim` source at commit
`85f7c3b959a908ed5232ede4f1a4ac7eafe6b630` (reported by that source as
v0.1.12a). It is not a patch for the closed v0.1.14a binary.

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
runs a real HTTP smoke test for `/health`, `/telemetry` and `/trace`, validates
the dashboard, then assembles a portable package. No Visual Studio installation
is required on the local PC.

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
over 256 crank-cycle positions, and links the Windows `ws2_32` library.

For a local build, run `tools/check-realtime-build.ps1` from the runtime bundle
before attempting the full build. See `REALTIME_TELEMETRY.md` for prerequisites
and current verification status.
