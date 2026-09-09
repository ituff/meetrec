# Requirements traceability matrix (T082)

Maps every MUST (and relevant SHOULD) requirement from `specs/001-mvp/requirements.md` to at
least one automated test or manual/inspection step. Test IDs: `UT-*` = unit tests
(`tests/UnitTests`), `MV-*` = MediaValidator tool, `IT-*` = `tests/IntegrationTests/IntegrationTests.ps1`,
`MAN-*` = manual checklist sections (`docs/manual-test-checklist.md`), `CODE-*` = documented
source inspection, `DRV-*` = driver manual log.

## 1. Functional requirements

| Req | Summary | Verification |
|---|---|---|
| FR-001 | x64 installer installs app + driver | MAN-1, MAN-7 |
| FR-002 | Elevation only for install/driver removal | MAN-1; installer code inspection |
| FR-003 | App runs unelevated | MAN-1; app.manifest `asInvoker` (CODE) |
| FR-004 | Startup verifies OS/arch/instance/driver/graphics | CODE `Application::InitRuntime`, `IsWindows11OrLater`; MAN-1 negative rows |
| FR-005 | Second launch activates existing instance | CODE `EnsureSingleInstance`; MAN-1 |
| FR-006 | Startup failures actionable + error code | CODE error paths; MAN-1 |
| FR-007 | Silent install/uninstall | MAN-7 (`/VERYSILENT`) |
| FR-010 | Exactly one adapter + one monitor | DRV-2, DRV-7; driver CODE (`MaxMonitorsSupported=1`, single FinishInit) |
| FR-011 | Modes 720p30 / 1080p30 / 1080p60 | DRV-3; CODE mode tables; EDID generator asserts |
| FR-012 | 1080p30 preferred | DRV-3; CODE preferred index + EDID DTD1 |
| FR-013 | Locate monitor by stable identity | UT Identity.*; CODE TopologyService; DRV-9 |
| FR-014 | Verify active + extended desktop before recording | CODE `VerifyVirtualMonitorActive`, coordinator Prepare step; DRV-2 |
| FR-015 | Monitor interruption → safe stop + finalize | MAN-3; IT display-removal row; DRV-10 |
| FR-016 | UI enable/disable when idle | CODE `DisableVirtualDisplay`, tray commands; MAN-2 |
| FR-017 | Disable rejected while recording | UT StateMachine.DisplayDisableRejectedWhileActive; DRV-11 |
| FR-020 | Enumerate eligible top-level windows | CODE WindowEnumerator; MAN-3 |
| FR-021 | Icon + exe + title in list | MAN-3 (owner-draw list shows exe + title; icon via WM_GETICON) |
| FR-022 | Exclude recorder/shell/cloaked/tool windows | CODE eligibility predicate; MAN-3 |
| FR-023 | Persist original placement before move | CODE `CaptureAndMoveTo` capture step; MAN-3 |
| FR-024 | Window wholly inside virtual work area | UT Geometry.*; MAN-3 |
| FR-025 | Maximized → restore, move, re-maximize | CODE placement sequence; MAN-3 |
| FR-026 | Restore on stop / recoverable failure | CODE `TeardownSession` restore; MAN-3 |
| FR-027 | Primary-monitor fallback restore | CODE `Restore` fallback; MAN-3 |
| FR-028 | Target destroyed → `VSR-TARGET-LOST` | CODE consume-loop liveness poll; MAN-3 (AC-006) |
| FR-029 | New owned popups relocated | UT PopupTracker.*; CODE `PopupTracker` (WinEvent hook, no focus steal); MAN-3 popup row |
| FR-030 | UI states minimize target caveat | MAN-3; CODE start-dialog text |
| FR-040 | Capture targets virtual monitor only | CODE `CreateCaptureItemForMonitor`; MAN-4 isolation |
| FR-041 | GPU D3D11 frame path | CODE GraphicsDevice/WGC; ADR-003 |
| FR-042 | Verify frame size + pixel format | CODE session stats + format constant (B8G8R8A8); MV dimensions check |
| FR-043 | Tolerate dup/delayed frames; monotonic output | CODE writer clamp; UT Timeline.*; MV monotonic check |
| FR-044 | Fixed queue upper bound | UT FrameQueue.* (capacity 3) |
| FR-045 | Drop-oldest on full | UT FrameQueue.OverflowEvictsOldest |
| FR-046 | Captured/encoded/dropped counters | UT conservation test; UI shows counters (FR-077) |
| FR-047 | Capture continues when controller hidden/minimized | MAN-4 (AC-003); ADR-003 (no HWND dependency) |
| FR-048 | Closing preview never stops capture | CODE preview tap; MAN-5 |
| FR-049 | Cursor exclusion when supported | CODE `IsCursorCaptureEnabled` toggle; MAN-5 |
| FR-050 | H.264 in MP4 | MV h264 check; ADR-004 |
| FR-051 | Low/Standard/High presets | UT Preset.TableMatchesSpec; IT preset rows |
| FR-052 | Output path selected + remembered | MAN-6; CODE settings round trip |
| FR-053 | `Recording_yyyyMMdd_HHmmss.mp4` local time | UT OutputFile.Stamp* |
| FR-054 | No overwrite without authorization | UT OutputFile.CollisionAvoidedWithSuffix |
| FR-055 | Monotonic timestamps excluding pauses | UT Timeline.*; MV monotonic + duration |
| FR-056 | Stop flushes/finalizes/reports | CODE `Finalize`; MAN-4 idempotency row |
| FR-057 | `.partial.mp4` until finalization | UT partial naming; CODE; MAN-4 failure row |
| FR-058 | Atomic rename on success | CODE rename step; IT |
| FR-059 | Writability + space check before start | UT OutputFile.Rejects*/Accepts* |
| FR-060 | Safe stop on write failure / low disk | UT DiskGuard.*; MAN-4 (AC-007) |
| FR-061 | No audio track | MV single video stream; CODE (video-only sink) |
| FR-062 | Prefer hardware MFT | UT EncoderPolicy.*; probe log (`ENCODER_SELECTED`) |
| FR-063 | `VSR-ENCODER-UNAVAILABLE` before recording | UT EncoderPolicy.Rejects*; CODE prepare ordering (probe before file) |
| FR-070 | Start only from Ready + valid inputs | UT StateMachine.StartOnlyFromReady; CODE Start validations; MAN-5 |
| FR-071 | Pause keeps capture alive, no samples | CODE paused discard path; MAN-4 (AC-005) |
| FR-072 | Resume excludes paused duration | UT Timeline.*; MAN-4 (AC-005) |
| FR-073 | Idempotent Stop | CODE Stop/Finalize idempotency; MAN-4 |
| FR-074 | One recording session per user session | CODE named mutex `Local\VirtualScreenRecorder.Recording` |
| FR-075 | UI responsive during recording | CODE worker-thread pipeline; MAN-4/5 |
| FR-076 | Elapsed active time displayed | MAN-5; CODE snapshot timer |
| FR-077 | State + last failure displayed | MAN-5; CODE status panel + tray |
| FR-080 | Tray icon present | MAN-5; CODE TrayIcon |
| FR-081 | Close hides to tray during recording | CODE WM_CLOSE; MAN-5 |
| FR-082 | Tray Open/Pause-Resume/Stop/Exit per state | CODE tray menu; MAN-5 |
| FR-083 | Tooltip state + elapsed while recording | CODE TrayIcon::Update; MAN-5 |
| FR-084 | Exit during recording finalizes first | CODE exit confirmation; MAN-5 |
| FR-085 | Tray icon distinguishes states | CODE state colors; MAN-5 |
| FR-090 | Settings as UTF-8 JSON | UT Json/Settings; CODE store |
| FR-091 | Unknown fields ignored/preserved | UT Settings.UnknownFields* |
| FR-092 | Invalid settings → defaults + warning | UT Settings.InvalidTypes*; CODE load warning |
| FR-093 | Log record fields | UT Logger queue test; CODE record schema; MAN-6 |
| FR-094 | No content/credential logging | UT Sanitize.*; CODE sanitization rule; MAN-6 |
| FR-095 | Rotation ≤ 7 days / 50 MB | UT Logger rotation test; MAN-6 |
| FR-096 | Diagnostics package (SHOULD) | CODE Diagnostics.cpp; MAN-6 |
| P1/T100 | Owned-popup relocation (FR-029, SHOULD) | Implemented — see FR-029 row |
| P1/T103 | Local opt-in crash dumps | CODE `CrashReporter` (`enableCrashDumps` default off, never uploaded); settings UTs cover schema round trip |

## 2. Non-functional requirements

| Req | Summary | Verification |
|---|---|---|
| NFR-001 | Idle CPU < 1 % | MAN-8 (T091 measurement) |
| NFR-002 | Idle working set < 60 MB (SHOULD) | MAN-8 |
| NFR-003 | Recording working set < 250 MB (SHOULD) | MAN-8 |
| NFR-004 | HW-encoded CPU < 15 % (SHOULD) | MAN-8; ADR-004 GPU path |
| NFR-005 | Start ≤ 5 s | MAN-8; dev-record timing output |
| NFR-006 | Finalize ≤ 10 s (30-min, SSD) | MAN-8 |
| NFR-007 | No unbounded memory (2-hour soak) | MAN-4/8; UT queue bounds; ADR-005 |
| NFR-010 | RAII everywhere | CODE (ComPtr/unique_ptr/RAII wrappers); review |
| NFR-011 | No exceptions across callbacks | CODE try/catch at WGC/MF/UI boundaries; review |
| NFR-012 | Controlled failure paths | CODE coordinator monitors; MAN-3/4 |
| NFR-013 | Target never left off-screen | CODE Restore guarantee; MAN-3 |
| NFR-014 | Repeated stop/shutdown safe | UT (Finalize idempotence by construction); MAN-4 |
| NFR-020 | Least privilege at runtime | MAN-1; manifest |
| NFR-021 | Signed catalog for distribution | ADR-006; T093 gate |
| NFR-022 | No secrets in scripts | CODE scripts review; .gitignore |
| NFR-023 | Controlled DLL loading / safe search | CODE (no LoadLibrary of user paths); manifest; review |
| NFR-024 | Unicode paths validated | CODE wide APIs throughout; UT unicode JSON |
| NFR-025 | No DRM/capture bypass | CODE inspection (no such capability present) |
| NFR-030 | Windows 11 x64 target | UT build config (x64-only); MAN-1 |
| NFR-031 | C++20 documented toolchain | Build compiles with /std:c++20; README pins versions; build manifest |
| NFR-032 | /W4 /WX first-party code | Directory.Build.props; full build passes (CODE) |
| NFR-033 | Static libs → one exe | CODE project layout; App.vcxproj references |
| NFR-034 | Driver separate project/binary | VirtualDisplayDriver.vcxproj; DRV |
| NFR-035 | Documented public interfaces | Header docs in each module; design.md §4 |
| NFR-036 | Deterministic scripts | scripts/build|test|clean.ps1; build manifest output |
| NFR-040 | Per-Monitor V2 DPI | manifest; MAN-5 DPI rows |
| NFR-041 | Keyboard accessibility | MAN-5; IsDialogMessage loop |
| NFR-042 | Accessible names/states (SHOULD) | MAN-5; standard controls |
| NFR-043 | Errors: what/cause/action | CODE Error.userMessage convention; UT ErrorCodes |
| NFR-044 | Close never triggers destructive action | CODE WM_CLOSE/exit confirmations; MAN-5/7 |

## Acceptance criteria

| AC | Verification |
|---|---|
| AC-001 | MAN-1, MAN-7, DRV-2 |
| AC-002 | MAN-4 isolation; DRV-14 |
| AC-003 | MAN-4 minimized row |
| AC-004 | MAN-3 restore rows |
| AC-005 | MAN-4 pause/resume row; IT |
| AC-006 | MAN-3 target-termination row |
| AC-007 | MAN-4 disk row; UT DiskGuard |
| AC-008 | MAN-4 two-hour row |
| AC-009 | MAN-1 single-instance row |
| AC-010 | MAN-7 uninstall row |

## Coverage gaps tracked

- T091/T092/T093 measurements and cross-machine runs require physical reference machines; the
  harness and checklists above are the deliverable until those runs execute.
