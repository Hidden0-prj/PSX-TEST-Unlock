
# PSX-iTests

A basic CPU/GPU/SPU functionality test tool for the original Sony
PlayStation - a simple, cool-looking way to sanity-check that a console's
core hardware is actually working, rather than an emulator-accuracy
validation suite. If you're looking for the latter, check out
[JaCzekanski/ps1-tests](https://github.com/JaCzekanski/ps1-tests) instead.

Currently includes:

- **GPU: Color bar test pattern** - a classic vertical color bar pattern plus
  a gradient strip, for a quick visual check of color output and screen
  geometry.
- **CPU: Benchmark** - runs a fixed workload and reports an iterations/second
  score, timed against real vblank periods (NTSC/PAL aware) rather than the
  Timer registers directly.
- **SPU: Channel test** - steps through all 24 SPU channels individually,
  playing a short test tone on whichever one is selected, to confirm each
  channel actually outputs sound.
- Background music and UI sound effects, to prove the SPU can do more than
  just beep.

A GPU 3D test (spinning cube, using the GTE) is planned but not yet wired in.

## Credits and license

This project is built on top of
[spicyjpeg's ps1-bare-metal](https://github.com/spicyjpeg/ps1-bare-metal)
support library and build system, and reuses the menu/UI framework from
[spicyjpeg's ps1-ram-tester](https://github.com/spicyjpeg/ps1-ram-tester)
almost entirely as-is. The GPU, SPU, controller and reboot drivers
(`src/common/`), the standard library shim (`src/libc/`), the hardware
register definitions (`src/ps1/`), and the font/renderer/UI/modal framework
(`src/main/font.c`, `renderer.c`, `ui.c`, `modals.c`) are all his work,
carried over with minimal or no changes.

New for this project: the main menu (`src/main/mainmenu.c`), the three test
screens (`gpu_colorbars.c`, `cpu_bench.c`, `spu_channel_test.c`), and the
sound module (`sound.c`) that drives the UI sounds and looping BGM.

Everything in this repository is licensed under the MIT license (or the
functionally equivalent ISC license), same as the upstream projects it's
built on. See `LICENSE` for the full text. The only "hard" requirements are
attribution and preserving the license notice; you may otherwise freely use
any of the code for both non-commercial and commercial purposes.

## Building

This repository follows the same overall structure as ps1-bare-metal, so you
may refer to
[its build instructions](https://github.com/spicyjpeg/ps1-bare-metal#building-the-examples).

A working GitHub Actions workflow is included (`.github/workflows/build.yml`)
that builds its own MIPS toolchain from scratch (cached after the first run),
downloads the latest [mkpsxiso](https://github.com/Lameguy64/mkpsxiso) Linux
release automatically, and uploads the compiled CD image / executable as a
downloadable artifact - no local toolchain setup required if you'd rather
build in CI.

The build produces a `.bin`/`.cue` CD-ROM image pair (the format most
burning software and ODE loaders like Xstation/PicoStation expect) via
mkpsxiso, alongside the raw `.psexe` executable. If you're building
locally, install mkpsxiso yourself and either make sure it's on your
`PATH`, or pass its location manually via `-DMKPSXISO_PATH=/path/to/mkpsxiso`
when configuring the project.

## See also

- [psx-spx](https://psx-spx.consoledev.net/), the main hardware reference
  used throughout this project.
- If you need help or wish to discuss PS1 homebrew development more in
  general, you may want to check out the
  [PSX.Dev Discord server](https://discord.gg/QByKPpH).
