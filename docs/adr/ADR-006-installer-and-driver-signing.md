# ADR-006: Installer and driver signing strategy

- **Status:** Accepted (internal MVP); production signing gated per spec.md §13
- **Date:** 2026-09-09
- **Deciders:** Virtual Screen Recorder project
- **Related requirements:** FR-001..FR-007, NFR-020..NFR-023, AC-001, AC-010

## Context

Distribution requires one setup executable that installs the desktop application and the IDD
driver package, elevates only for installation/driver management (FR-002), runs the app
unelevated (FR-003), supports silent install/uninstall for testing (FR-007), preserves user
recordings on uninstall (AC-010), and — for the distributable MVP — presents a
production-signed driver catalog that Windows accepts without Test Mode (NFR-021).

## Decision

- **Installer technology: Inno Setup** (`installer/VirtualScreenRecorderSetup.iss`).
  - Preflight: Windows 11 x64 check (build ≥ 22000), no active recording process.
  - Copies the app, stages the driver payload, and installs the driver package via
    `pnputil /add-driver ... /install` in an elevated step; return codes are surfaced and
    logged (`installer` log).
  - Uninstall stops the app, removes the driver package (`pnputil /delete-driver ... /uninstall`),
    and preserves `%LOCALAPPDATA%\VirtualScreenRecorder` (settings, logs, recordings).
  - Silent support comes from Inno Setup's `/SILENT` `/VERYSILENT` `/SUPPRESSMSGBOXES` switches,
    documented in `installer/README.md` (FR-007).
- **Internal MVP signing:** the driver catalog is **test-signed** by
  `scripts/testsign.ps1` (self-signed code-signing cert, signtool, inf2cat). Test machines must
  enable Test Mode and trust the exported certificate. Certificates and keys are generated at
  build time on the developer machine and never committed (NFR-022, `.gitignore`).
- **Distributable MVP signing:** release pipeline hooks accept an EV/OV certificate
  identifier (and, for Windows 11 attestation signing, a partner-center submission step) without
  storing secrets in the repository; `ReleaseSigned` maps to Release binaries plus the signing
  step. This gate is validated per spec.md §13 (clean machine, Secure Boot, no Test Mode) before
  distribution.

## Alternatives considered

- **WiX/MSI** — deferred to the P1 enterprise assessment (T104); Inno Setup satisfies the
  internal MVP with less ceremony and better UX for a UMDF driver stage.
- **MSIX/Store** — rejected for MVP: driver side-loading and service installation are outside
  the MSIX model we need.
- **Signing during build (CI)** — rejected for MVP: secrets in CI conflict with NFR-022 until a
  dedicated signing service exists; signing is an explicit release step.

## Consequences

- (+) One Setup executable, deterministic preflight/install/removal, preserved user data.
- (+) No secrets in the repository; test signing is reproducible from source + scripts.
- (-) Internal users must manage Test Mode; clearly labeled in the installer welcome text and
  README.
- (-) Attestation-signed distribution additionally requires the Microsoft hardware dev center
  submission; tracked as release gate T093.

## Validation evidence

- Clean install/uninstall runs are part of `docs/manual-test-checklist.md` (Installer section)
  and must be executed on a disposable machine per T090; results are recorded there.
