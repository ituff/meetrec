# Integration test harness (T082)

End-to-end verification for a machine with the driver package staged
(`scripts\install-driver.ps1`) and the application built
(`scripts\build.ps1 -Configuration Release`).

Run from an elevated Developer PowerShell:

```powershell
powershell -File tests\IntegrationTests\IntegrationTests.ps1 -Configuration Release
```

The script:

1. Runs the unit tests (`scripts\test.ps1`).
2. Creates/locates the virtual display via the app's `--dev-record` path.
3. Records 10-second clips per preset (`low`, `standard`, `high`).
4. Validates each MP4 with `MediaValidator.exe` (container, H.264, dimensions, fps, monotonic
   timestamps, full decode, duration tolerance ±2 s).
5. Writes a summary report to `artifacts\test-results\integration-report.json` and fails with a
   non-zero exit code on any invalid clip.

Media validation exit codes and checks are documented in
`tests\IntegrationTests\MediaValidator\MediaValidator.cpp`.
