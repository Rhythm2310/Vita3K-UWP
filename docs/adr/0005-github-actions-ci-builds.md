# GitHub Actions CI/CD for Xbox UWP Builds

## Status
Accepted

## Context
Compiling a large C++ UWP emulator requires specialized Windows SDKs, MSVC toolchains, and AppX packaging certs. Local building can consume significant machine resources and differ across local environments.

## Decision
1. Delegate all compilation, packaging, and artifact generation to GitHub Actions using `windows-2022` or `windows-latest` runners.
2. Structure the workflow (`.github/workflows/xbox-uwp.yml`) to generate comprehensive, verbose error and debug logs during CMake configuration, compilation, and MSIX packaging.
3. Automatically bundle and upload the ready-to-install Xbox Dev Mode package (`.msix` / `.appx` and certificate) as workflow artifacts upon every push.
