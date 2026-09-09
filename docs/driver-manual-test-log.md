# Driver manual test log (T011, T012)

Record evidence here for every driver qualification run. One row per scenario, one table per
machine. Do not delete prior rows; append new runs. Attach WPP traces (`netsh trace` / tracefmt
or WPA) as files beside this log when failures occur.

## Reference machine

| Field | Value |
|---|---|
| Machine name | _ |
| Windows 11 build | _ |
| GPU (vendor/model/driver) | _ |
| WDK / SDK used to build | 10.0.26100.0 |
| Driver build | Debug / Release, commit _ |
| Signing | test-signed (cert thumbprint _) |
| Test Mode enabled | yes/no |
| Date / tester | _ |

## Scenarios

### T011 — install and modes

| # | Scenario | Expected evidence | Result (PASS/FAIL) | Evidence (log/trace ref) |
|---|---|---|---|---|
| 1 | Stage package: `scripts\install-driver.ps1` | pnputil exit 0; package present in driver store | _ | _ |
| 2 | Launch app, start recording (creates SwDevice) | Display Settings shows one extra extended monitor "Virtual Recording Display" | _ | _ |
| 3 | Enumerate modes on the virtual display | Exactly 1280x720@30, 1920x1080@30 (preferred/default), 1920x1080@60 offered; no others | _ | _ |
| 4 | Switch to each mode | Mode applies; desktop extends; no driver crash (UMDF host alive) | _ | _ |
| 5 | Disable + re-enable the display | Monitor reappears with same identity; no duplicate | _ | _ |
| 6 | Reboot with display disabled | Recoverable state; app can re-create the display | _ | _ |

### T012 — lifecycle and stable identity

| # | Scenario | Expected evidence | Result | Evidence |
|---|---|---|---|---|
| 7 | Ten create/destroy cycles (start/stop recording) | No stale or duplicate virtual monitors after any cycle (`ENUMDISPLAY_DEVICES` count stable) | _ | _ |
| 8 | Reboot after a session | No orphan virtual monitor at logon; known state recoverable | _ | _ |
| 9 | Identity stability across re-creation | TopologyService resolves the same stable identity regardless of GDI ordinal (DISPLAY2/DISPLAY3…) | _ | _ |
| 10 | Display removal during recording | Recording stops safely, MP4 finalized, `VSR-DISPLAY-DISCONNECTED` reported (FR-015) | _ | _ |
| 11 | Disable rejected during recording | Tray/command disabled; coordinator rejects with `VSR-INTERNAL-STATE` (FR-017) | _ | _ |
| 12 | WPP trace review | Lifecycle callbacks appear in trace (`VsrTraceGuid`); no error spikes during cycles | _ | _ |

### T022 — capture spike corroboration

| # | Scenario | Expected evidence | Result | Evidence |
|---|---|---|---|---|
| 13 | 60-second capture with controller minimized | Frames continue; counters advance (`--dev-record` output) | _ | _ |
| 14 | Physical monitor activity isolation | Move windows on the physical display; activity absent from virtual-display frames | _ | _ |

## Sign-off

- [ ] All scenarios PASS on this machine (date, tester: _)
- [ ] Deviations documented as issues/ADRs: _
