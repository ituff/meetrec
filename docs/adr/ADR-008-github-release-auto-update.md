# ADR-008: GitHub Releases-based auto-update for the portable package

- **Status:** Accepted (implemented)
- **Date:** 2026-11-11
- **Deciders:** MeetRec (Virtual Screen Recorder) project
- **Related requirements:** portable zip distribution (no installer), customer request 2026-11:
  automatic updates and update notifications

## Context

MeetRec ships as a portable zip: unzip into a writable folder, run `MeetRec.exe`. There is no
installer, no service, and no existing update channel. Customers need to learn about new
versions and update without hunting for the Releases page, but the app is unsigned and must
keep working without admin rights.

The release workflow already publishes, for every `v*` tag:

- `MeetRec-<version>-win64-with-ffmpeg.zip` (the portable package),
- `SHA256SUMS.txt` (SHA-256 of the zip),
- release notes embedded in the GitHub Release.

## Decision

We will implement an in-app updater driven by the GitHub Releases API:

- **Check**: `GET https://api.github.com/repos/ituff/meetrec/releases/latest`; parse
  `tag_name`, `html_url`, and the portable zip/`SHA256SUMS.txt` assets. Only stable
  (non-draft, non-prerelease) semantic versions newer than the running build are offered.
  Automatic checks run at most once per calendar day and can be disabled in
  **Settings → General → Check for updates automatically**. `VSR_UPDATE_API_URL` overrides the
  endpoint for QA/support.
- **Notify**: the *More* and tray menus show "Version X available"; a one-time non-activating
  toast appears once per version. A manual *Check for updates...* reports "up to date" or the
  error explicitly.
- **Download**: the zip is streamed into
  `%LOCALAPPDATA%\VirtualScreenRecorder\Updates\<version>\` with progress and cancellation.
  An existing file with a matching hash is reused.
- **Verify**: the expected digest is read from the release's `SHA256SUMS.txt`; the downloaded
  zip is hashed with BCrypt (SHA-256). A mismatch deletes the file and aborts.
- **Apply**: the running exe cannot replace itself. The app writes a one-shot PowerShell script
  (UTF-8 BOM), starts it hidden, and exits. The script waits for the PID, extracts the zip with
  the in-box `tar.exe`, copies the package root over the install folder, restarts `MeetRec.exe`,
  and deletes itself. Failures are logged to `Updates\update.log` and restart the old build.
- **Guard**: the install folder is probed for writability first. If it is not writable (for
  example a Program Files install), MeetRec does not attempt a copy and opens the release page
  instead.

## Alternatives considered

- **MSIX / installer-based updates** — rejected: breaks the portable, no-admin, unzip-and-run
  promise and adds signing/identity requirements the project does not have.
- **A third-party updater framework (Squirrel, WinSparkle, Omaha)** — rejected: extra binary
  and dependency surface, mismatch with the static, dependency-light product; the GitHub
  REST API + WinHTTP path is a few hundred lines.
- **Notify-only (open the Releases page)** — kept as the fallback for read-only installs, but
  rejected as the primary flow because customers asked for updates, not another download step.
- **Silent replacement without hash verification** — rejected: the build is unsigned, so the
  published SHA-256 is the only integrity check available; applying an unverified zip over the
  install folder is not acceptable.

## Consequences

- (+) One click from notification to updated portable install; no admin rights, no installer,
  no service.
- (+) Runs at below-normal priority and only performs two small GitHub API calls per day;
  automatic checks are opt-out.
- (+) The same verified artifact is used for manual downloads and in-app updates.
- (-) The updater relies on Windows PowerShell and `tar.exe` (both in-box on Windows 10 2004+).
- (-) Read-only installs cannot self-update; the release page is opened instead.
- (-) No delta updates: the full ~200 MB zip is downloaded each time.
- (-) The unsigned build and the generated script may be flagged by endpoint protection; the
  hash check limits accidental corruption but does not establish publisher identity.

## Validation evidence

- `UpdateTests` cover semantic version parsing/comparison, SHA256SUMS parsing, release JSON
  asset selection, prerelease rejection, and SHA-256 of a known vector. Suite: 102 tests,
  0 failing.
- Live GitHub API check from the built app returned the current release (1.1.0) and correctly
  reported "no update" for the 1.2.0 build.
- A local mock release server (v9.9.9, zip + `SHA256SUMS.txt`) exercised the complete path:
  check → download → SHA-256 verify → generated script → extract/copy → restart. The copied
  test install gained the zip's marker file and the restarted `MeetRec.exe` ran from the
  updated folder.
- The update window was rendered at 125% DPI and inspected in its download-complete state.
