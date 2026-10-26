#include <gtest/gtest.h>

#include "OtaVersionCompare.h"

namespace ota_version {

TEST(OtaVersionCompare, SameVersionWithTagPrefixIsNotNewer) {
  EXPECT_FALSE(isNewer("v1.2.0", "1.2.0-x4pro"));
}

TEST(OtaVersionCompare, SameVersionRcBuildIsOlder) {
  EXPECT_TRUE(isNewer("v1.2.0", "1.2.0-rc+abc1234"));
}

TEST(OtaVersionCompare, PatchBumpIsNewer) { EXPECT_TRUE(isNewer("v1.2.1", "1.2.0-x4pro")); }

TEST(OtaVersionCompare, PatchIsComparedNumerically) { EXPECT_TRUE(isNewer("v1.2.10", "1.2.9-x4pro")); }

TEST(OtaVersionCompare, OlderPatchIsNotNewer) { EXPECT_FALSE(isNewer("v1.1.9", "1.2.0-x4pro")); }

TEST(OtaVersionCompare, MinorBumpIsNewer) { EXPECT_TRUE(isNewer("v1.3.0", "1.2.0-x4pro")); }

TEST(OtaVersionCompare, MajorBumpIsNewer) { EXPECT_TRUE(isNewer("v2.0.0", "1.99.99-x4pro")); }

TEST(OtaVersionCompare, LowerMajorIsNotNewer) { EXPECT_FALSE(isNewer("v1.99.99", "2.0.0-x4pro")); }

TEST(OtaVersionCompare, MissingTagPrefixStillParses) { EXPECT_FALSE(isNewer("1.2.0", "1.2.0-x4pro")); }

TEST(OtaVersionCompare, EqualVersionsWithoutSuffix) { EXPECT_FALSE(isNewer("v1.1.0", "1.1.0")); }

TEST(OtaVersionCompare, IncompleteTripleIsNotNewer) { EXPECT_FALSE(isNewer("v1.2", "1.2.0-x4pro")); }

TEST(OtaVersionCompare, MalformedTagIsNotNewer) { EXPECT_FALSE(isNewer("not-a-version", "1.2.0-x4pro")); }

TEST(OtaVersionCompare, EmptyTagIsNotNewer) { EXPECT_FALSE(isNewer("", "1.2.0-x4pro")); }

TEST(OtaVersionCompare, MalformedCurrentVersionIsNotNewer) { EXPECT_FALSE(isNewer("v1.2.1", "not-a-version")); }

}  // namespace ota_version
