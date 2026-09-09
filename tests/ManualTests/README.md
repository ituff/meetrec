# Manual tests

Manual scenarios live in [`docs/manual-test-checklist.md`](../../docs/manual-test-checklist.md)
and driver-specific evidence in [`docs/driver-manual-test-log.md`](../../docs/driver-manual-test-log.md).

Automated pieces reachable from this folder:

- `../UnitTests` — unit test runner (`scripts\test.ps1`).
- `IntegrationTests\IntegrationTests.ps1` — end-to-end clip record + media validation.
- `IntegrationTests\MediaValidator` — test-only MP4 inspection tool (not shipped).
