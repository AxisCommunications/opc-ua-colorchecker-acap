---
description: "Repository guidance for the OPC UA Color Checker ACAP: C++20 application code, manifest contracts, settings UI, packaging, builds, and validation."
applyTo: "**"
---

# OPC UA Color Checker ACAP

## Scope and architecture

- This is an AXIS ACAP v4 native application.
- Always write the official platform names as `AXIS OS` and `AXIS ACAP`.
- It acquires an NV12 VDO frame, converts it to BGR with OpenCV, and evaluates a color area.
- It publishes the boolean result through OPC UA, Axis events, and FastCGI endpoints.
- Keep existing component responsibilities:
  - `ImageProvider` handles VDO frames
  - `ColorArea` owns masks, averages, and tolerance
  - `ParamHandler` owns axparameter access and callbacks
  - `OpcUaServer` owns `open62541` and its thread lifecycle
  - `CgiHandler` owns FastCGI
  - `EventHandler` owns Axis event publication

## Contract changes

When adding or renaming a configurable parameter, update all applicable surfaces together:

- [`manifest.json`](../manifest.json): `paramConfig` name, type/range, and default.
- [`ParamHandler`](../include/ParamHandler.hpp) declarations and
  [implementation](../src/ParamHandler.cpp).
- [`html/js/opcuacolorchecker.js`](../html/js/opcuacolorchecker.js):
  `/axis-cgi/param.cgi` reads/writes with
  `Opcuacolorchecker.<Name>`.
- The settings HTML controls and relevant image-analysis behavior.

When changing CGI behavior, keep these aligned:

- [`manifest.json`](../manifest.json) `httpConfig`, using least-privileged `viewer` or `admin` access.
- [`CgiHandler`](../include/CgiHandler.hpp) routing and response status/error behavior.
- Settings UI URLs and payload expectations.
- Existing payload: `{"status": 0|1}` for `getstatus.cgi`.
- Existing payload: `{"R": ..., "G": ..., "B": ...}` for `pickcurrent.cgi`.

- Keep `MarkerShape` stable: manifest `0` is ellipse and `1` is rectangle.
- Keep those values aligned with the C++ `MarkerShape` enum and JavaScript `Shape`.
- Keep `appName` `opcuacolorchecker` consistent with the executable and local CGI URLs.

## C++ conventions

- Build standard is C++20 with `-Wall -Wextra -Werror`; retain explicit error handling.
- Follow [`.clang-format`](../.clang-format): LLVM, Allman braces, 4 spaces, 120 columns,
  no packed parameters.
- New C++ headers/sources use the existing Apache-2.0 Axis header; headers use `#pragma once`.
- Use PascalCase methods, trailing member underscores, GLib types at GLib boundaries, and
  established Yoda-style null/value comparisons.
- Use `LOG_I`, `LOG_E`, and `LOG_D` from [`include/common.hpp`](../include/common.hpp) for messages.
- Follow the existing `__FILE__/__FUNCTION__` context pattern for failures.
- `LOG_D` is compiled out unless `DEBUG_WRITE` is enabled.
- Frames from `GetLastFrameBlocking()` must always be returned with `ReturnFrame()`.
- Do not introduce unsynchronized access to shared image-analysis state.
- Scope third-party GCC diagnostic suppression narrowly with the established push/pop pattern.

## UI and packaging

- The settings UI is static HTML and vanilla JavaScript, with no bundler or framework.
- Preserve tab indentation in [`html/js/`](../html/js/) and the `fetch().then()` style for
  local flow changes.
- [`Dockerfile`](../Dockerfile) cross-compiles for `aarch64` and `armv7hf` with the AXIS ACAP SDK.
- Keep dependency versions and SHA256 values synchronized; Renovate manages those updates.
- Do not edit generated root artifacts: `*.eap`, `*_LICENSE.txt`, `opcuacolorchecker`, `pa*.conf`.
- Regenerate packages through the container build.

## Build and validation

- Build both packages with `make -j "$(nproc)" dockerbuild` or `make -j "$(nproc)" podmanbuild`.
- Use `make -j "$(nproc)" aarch64.docker` or `make -j "$(nproc)" armv7hf.docker` for a focused Docker build.
- There is no automated test suite. For C++ changes, run the relevant container build.
- For formatting, documentation, or configuration changes, run the matching Super-Linter check.
- `LINT.md` documents the full and focused linter commands.
- Validate every altered cross-file contract before finishing.
- Do not modify unrelated generated packages or dependency pins.
