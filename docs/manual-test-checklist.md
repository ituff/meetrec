# Manual test checklist (T082, T091, T092)

Release qualification checklist for the MVP. Each row records the expected evidence and a
result. Run on every reference machine (at least two Windows 11 x64 machines, two GPU vendors
where available — T092). Automated portions are marked ⚙; everything else is manual evidence.

## 1. Installation and startup (AC-001, AC-009)

| ✔ | Scenario | Expected | Result |
|---|---|---|---|
| ☐ | ⚙ Clean install with elevation (installer) | App + driver staged; Start Menu shortcut; install log written; no Test Mode warning suppressed | _ |
| ☐ | ⚙ Launch from Start Menu as standard user | App runs unelevated; no UAC prompt (FR-003) | _ |
| ☐ | Launch on unsupported OS (Win10 VM, optional) | Actionable error + `VSR-INIT-OS-UNSUPPORTED` in log (FR-006) | _ |
| ☐ | Second launch while running | Existing window activated; single process (FR-005) | _ |
| ☐ | Launch with driver package absent | `VSR-INIT-DRIVER-MISSING` surfaced with remediation text | _ |

## 2. Virtual display (AC-001)

| ✔ | Scenario | Expected | Result |
|---|---|---|---|
| ☐ | Start recording with display absent | Virtual display appears as extended monitor within 5 s (NFR-005) | _ |
| ☐ | Display Settings shows the monitor | Name "Virtual Recording Display"; modes 720p30 / 1080p30 (preferred) / 1080p60 | _ |
| ☐ | Ten start/stop cycles | No stale/duplicate monitors (T012 evidence in driver log) | _ |

## 3. Window placement and restoration (AC-004, AC-006)

| ✔ | Scenario | Expected | Result |
|---|---|---|---|
| ☐ | Move a normal window to the virtual display | Window wholly inside virtual work area, centered (FR-024) | _ |
| ☐ | Move a maximized window | Restored, moved, re-maximized on the virtual monitor (FR-025) | _ |
| ☐ | Stop recording | Window restored to original monitor/placement within 2 s (AC-004) | _ |
| ☐ | Close the target application during recording | Recording stops safely, finalizes, `VSR-TARGET-LOST` reported (AC-006) | _ |
| ☐ | Disconnect the virtual display mid-recording | Safe stop, finalize, `VSR-DISPLAY-DISCONNECTED` (FR-015) | _ |
| ☐ | Original monitor unplugged while recording | Restore lands visibly on the primary monitor (FR-027) | _ |
| ☐ | Mixed-DPI placement (100 % + 150 %) | Target fully visible on the virtual display; restored correctly (NFR-040) | _ |
| ☐ | Target list quality (Win32, Chromium, Office-style) | Eligible windows listed with icon, exe, title; recorder and shell excluded (FR-020..FR-022) | _ |
| ☐ | New target-process dialog during recording | Dialog relocated onto the virtual display without stealing focus (FR-029, T100) | _ |

## 4. Recording (AC-002, AC-003, AC-005, AC-007, AC-008)

| ✔ | Scenario | Expected | Result |
|---|---|---|---|
| ☐ | ⚙ Standard 30-minute recording; validate | MP4 plays; duration within 2 s of active time; monotonic timestamps (MediaValidator) | _ |
| ☐ | ⚙ Low and High preset 10-second records | Correct dimensions/fps per preset | _ |
| ☐ | Physical-desktop isolation | Unrelated windows/notifications never appear in output (AC-002) | _ |
| ☐ | Minimize/hide controller 5+ minutes | Recording continues; full interval present (AC-003) | _ |
| ☐ | ⚙ Pause 10 s between two 10 s runs | Output ≈ 20 s, not 30 s (AC-005) | _ |
| ☐ | Repeat Stop; Stop when idle; app exit during finalize | Idempotent; no double-finalize, no deadlock (FR-073, NFR-014) | _ |
| ☐ | Fill disk toward threshold | Auto stop + finalize + `VSR-OUTPUT-DISK-LOW` (AC-007) | _ |
| ☐ | Two-hour continuous recording | Responsive UI; bounded memory; playable file (AC-008, NFR-007) | _ |
| ☐ | Output filename/collision | `Recording_yyyyMMdd_HHmmss.mp4`; rerun in same minute yields `_1` suffix (FR-053/054) | _ |
| ☐ | Failed finalize path (revoke output dir permissions mid-write on a test dir) | `.partial.mp4` preserved; error includes path (FR-057, FR-056) | _ |

## 5. Controller, tray, preview (FR-070..FR-085)

| ✔ | Scenario | Expected | Result |
|---|---|---|---|
| ☐ | Commands per state | Start only Ready; Pause/Resume/Stop per snapshot; invalid commands disabled (FR-070) | _ |
| ☐ | Keyboard-only primary journey | Tab to list, preset, folder, start/stop; accessible names (NFR-041/042) | _ |
| ☐ | DPI 100 % / 150 % / 200 % | No clipped controls; WM_DPICHANGED live re-layout (NFR-040) | _ |
| ☐ | Close during recording | Hides to tray; recording continues (FR-081) | _ |
| ☐ | Tray menu commands | Open / Pause-Resume / Stop / Exit match state; tooltip shows state + elapsed (FR-082/083) | _ |
| ☐ | Tray icons | Visual state colors for Ready/Recording/Paused/Faulted (FR-085) | _ |
| ☐ | Exit during recording | Confirmation; stop + finalize before process exit (FR-084) | _ |
| ☐ | Restart Explorer while hidden | Tray icon recreated (TaskbarCreated) | _ |
| ☐ | Preview open/close/minimize repeatedly during 10-min recording | Output continuity unaffected; closing preview never stops capture (FR-048, T072) | _ |
| ☐ | Cursor option | When supported, pointer excluded by default; toggle honored (FR-049) | _ |

## 6. Settings, logs, diagnostics (FR-090..FR-096)

| ✔ | Scenario | Expected | Result |
|---|---|---|---|
| ☐ | Settings persisted | Output dir + preset remembered across runs (FR-052) | _ |
| ☐ | Corrupt `settings.json` | Defaults applied + warning log (FR-092) | _ |
| ☐ | Unknown settings field | Preserved on next save (FR-091) | _ |
| ☐ | Log review | UTC ts, severity, component, eventId, correlationId, message; no titles/content/tokens (FR-093/094) | _ |
| ☐ | Rotation | ≤ 50 MB / 7 days / 5 files retained (FR-095) | _ |
| ☐ | Diagnostics package | Zip contains logs + sanitized metadata; no video frames/credentials (FR-096) | _ |
| ☐ | Crash dumps opt-in (T103) | Off by default; enabled → forced-crash writes .dmp into CrashDumps; never uploaded | _ |

## 7. Installer lifecycle (AC-001, AC-010)

| ✔ | Scenario | Expected | Result |
|---|---|---|---|
| ☐ | ⚙ Silent install/uninstall switches | `/VERYSILENT` completes unattended (FR-007) | _ |
| ☐ | Upgrade install over previous version | Files replaced; settings preserved; driver re-staged | _ |
| ☐ | Uninstall | App removed; driver package removed; recordings + settings kept (AC-010) | _ |
| ☐ | Uninstall while recording (negative) | Blocked or safe stop with confirmation; never silent data loss (NFR-044) | _ |

## 8. Performance and soak (T091)

| ✔ | Metric | Target | Measured |
|---|---|---|---|
| ☐ | Package size (app + driver payload) | ≤ 20 MB (target) | _ |
| ☐ | Idle private working set (60 s settle) | < 60 MB (SHOULD) | _ |
| ☐ | 1080p30 recording private working set | < 250 MB (SHOULD) | _ |
| ☐ | Idle CPU | < 1 % (MUST, NFR-001) | _ |
| ☐ | HW-encoded 1080p30 CPU | < 15 % avg (SHOULD, NFR-004) | _ |
| ☐ | Start → Recording | ≤ 5 s (MUST, NFR-005) | _ |
| ☐ | Stop finalize of 30-min recording (local SSD) | ≤ 10 s (MUST, NFR-006) | _ |
| ☐ | 30-minute + 2-hour soak | No unbounded memory (NFR-007); monotonic timestamps | _ |

Record the reference machine spec (CPU, GPU, RAM, OS build) with the measurements and store the
signed-off report beside the build artifacts.

## 9. Cross-machine matrix (T092)

| Machine | OS build | GPU vendor/model | Result of sections 1-6 | Notes/defects |
|---|---|---|---|---|
| _ | _ | Intel _ | _ | _ |
| _ | _ | NVIDIA _ | _ | _ |
| _ | _ | AMD _ | _ | _ |

No open Critical/High defects may remain at sign-off (spec.md §14.6).
