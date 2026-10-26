#include "OtaVersionCompare.h"

#include <cstdio>
#include <cstring>

namespace ota_version {
namespace {

const char* skipTagPrefix(const char* version) {
  if (version == nullptr) return nullptr;
  if (version[0] == 'v' || version[0] == 'V') return version + 1;
  return version;
}

}  // namespace

bool isNewer(const char* latestTag, const char* currentVersion) {
  if (latestTag == nullptr || currentVersion == nullptr) return false;

  const char* latest = skipTagPrefix(latestTag);
  const char* current = skipTagPrefix(currentVersion);

  int latestMajor = 0;
  int latestMinor = 0;
  int latestPatch = 0;
  int currentMajor = 0;
  int currentMinor = 0;
  int currentPatch = 0;

  if (sscanf(latest, "%d.%d.%d", &latestMajor, &latestMinor, &latestPatch) != 3) return false;
  if (sscanf(current, "%d.%d.%d", &currentMajor, &currentMinor, &currentPatch) != 3) return false;

  if (latestMajor != currentMajor) return latestMajor > currentMajor;
  if (latestMinor != currentMinor) return latestMinor > currentMinor;
  if (latestPatch != currentPatch) return latestPatch > currentPatch;

  // Equal segments: an RC build is a pre-release of the same version, so the
  // matching release tag counts as newer.
  if (strstr(currentVersion, "-rc") != nullptr) return true;

  return false;
}

}  // namespace ota_version
