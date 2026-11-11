// MeetRec - single source of truth for the product version.
//
// App.rc includes this file too; the resource compiler ignores the C++-only
// wide/UTF-8 macros. Bump all values together and update
// docs/release/RELEASE-NOTES-<major>.<minor>.<patch>.md before tagging.
#pragma once

#define VSR_APP_VERSION_MAJOR 1
#define VSR_APP_VERSION_MINOR 2
#define VSR_APP_VERSION_PATCH 0

// RC syntax: FILEVERSION/PRODUCTVERSION take comma-separated numbers.
#define VSR_APP_VERSION_FILE 1,2,0,0
// RC string values (no L prefix).
#define VSR_APP_VERSION_STR "1.2.0.0"
// UI, --version, and updater display/compare.
#define VSR_APP_VERSION_W L"1.2.0"
#define VSR_APP_VERSION_UTF8 "1.2.0"
