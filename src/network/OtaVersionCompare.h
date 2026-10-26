#pragma once

namespace ota_version {

// True when `latestTag` (a GitHub release tag_name, with or without a leading
// 'v') is strictly newer than `currentVersion` (CROSSPOINT_VERSION, which may
// carry a board suffix such as "-x4pro" or a build suffix such as "-rc+abc1234").
// Returns false when either input has no parseable MAJOR.MINOR.PATCH triple, so a
// malformed tag can never be reported as an update.
bool isNewer(const char* latestTag, const char* currentVersion);

}  // namespace ota_version
