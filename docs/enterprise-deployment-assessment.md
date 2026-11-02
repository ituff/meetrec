# Enterprise deployment assessment (P1 / T104)

- **Status:** Assessment — no implementation in the MVP
- **Date:** 2026-09-10
- **Related:** ADR-006, FR-007, NFR-021, T093

## Question

Can Virtual Screen Recorder be deployed and managed with enterprise tooling (Intune/SCCM/GPO),
and what would change versus the Inno Setup internal installer?

## Current packaging baseline

- One Inno Setup executable; elevated install; driver staged via `pnputil`.
- Silent switches: `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART`; exit codes follow Inno
  conventions. A scripted uninstall (`/UNINSTALL /SILENT` via the uninstaller entry point).
- Driver package: WHQL-free staged INF + catalog; test-signed internally, attestation-signed for
  distribution (T093 gate).

## Option A — keep Inno Setup, wrap for enterprise

- Intune/SCCM deploy the existing EXE with silent switches (Intune Win32 app packaging wraps
  EXE/MSI with detection rules — supported and common).
- Detection: file version of `MeetRec.exe` + registry `AppId` key.
- Cost: near zero. Limits: EXE wrappers are second-class in some reporting surfaces; upgrades
  rely on our own uninstall step.

## Option B — MSI (WiX) primary package

- MSI gives standard upgrades (ProductCode/UpgradeCode), per-machine repair, and native SCCM/
  GPO handling; elevated driver staging stays as a custom action (`pnputil`), which works but
  custom actions raise enterprise review friction and need careful scheduling (deferred,
  no-impersonate).
- Cost: ~1–2 weeks to port, plus CI changes. Gains: standard transacted install/uninstall,
  better reporting.
- Driver install inside MSI is the risky half either way; keeping it identical to today
  (`pnputil` invocation) minimizes behavior drift.

## Option C — MSIX/App Installer

- Rejected for the same reasons as ADR-006: driver staging and service-style operations are
  outside the MSIX container model for this product.

## Driver distribution (the real enterprise gate)

- Enterprises generally require a **WHQL/attestation-signed driver**; Windows accepts
  attestation-signed packages without user prompts on stock machines (the T093 gate). A WHQL
  pass adds HLK testing effort but removes SmartScreen/attestation caveats for strict shops.
- Deployment of the driver package itself can also be decoupled from the app: ship the INF/CAB
  to Intune "Windows driver update" or pre-stage via Configuration Manager driver packages —
  then the *app* MSI/EXE carries no custom-action risk at all. Recommended target state.

## Management surface worth adding (P2)

- Per-machine settings template via GPO/registry override of `settings.json` defaults
  (output folder lock, disable crash dumps, disable preview).
- Event Log publishing for install/driver failures (today: installer log + app JSON logs).
- An `--uninstall-quiet` app helper so MDM can remove the app before an in-place upgrade.

## Recommendation

1. **Now:** document Intune Win32 wrap of the Inno Setup EXE (Option A) — sufficient for pilots.
2. **MVP+1:** port the app payload to WiX MSI (Option B) for standard enterprise lifecycle,
   keeping `pnputil` staging identical.
3. **Driver via management-plane pre-staging** where enterprises prefer zero custom actions.
4. Update ADR-006 with the chosen path before implementation.
