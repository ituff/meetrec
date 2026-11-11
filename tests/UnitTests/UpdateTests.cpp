// MeetRec - unit tests for the updater (ADR-008).
#include "TestFramework.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <Update/ReleaseInfo.h>
#include <Update/SemanticVersion.h>
#include <Update/UpdateChecker.h>

using namespace vsr::update;

VSR_TEST(Update, SemanticVersionParsesAndCompares) {
    auto version = SemanticVersion::Parse("1.2.0");
    VSR_CHECK(version.has_value());
    VSR_CHECK_EQ(version->major, 1);
    VSR_CHECK_EQ(version->minor, 2);
    VSR_CHECK_EQ(version->patch, 0);
    VSR_CHECK_EQ(version->ToString(), std::string("1.2.0"));

    auto tagged = SemanticVersion::Parse("v1.10.3");
    VSR_CHECK(tagged.has_value());
    VSR_CHECK_EQ(tagged->minor, 10);
    VSR_CHECK(*SemanticVersion::Parse("1.10.0") > *SemanticVersion::Parse("1.9.9"));
    VSR_CHECK(*SemanticVersion::Parse("2.0.0") > *SemanticVersion::Parse("1.99.99"));
    VSR_CHECK(*SemanticVersion::Parse("1.2.0") == *SemanticVersion::Parse("v1.2.0"));

    VSR_CHECK(!SemanticVersion::Parse("1.2").has_value());
    VSR_CHECK(!SemanticVersion::Parse("1.2.3-beta.1").has_value());
    VSR_CHECK(!SemanticVersion::Parse("v").has_value());
    VSR_CHECK(!SemanticVersion::Parse("abc").has_value());
}

VSR_TEST(Update, FindChecksumHandlesShaSumsFormat) {
    const std::string sums =
        "d41d8cd98f00b204e9800998ecf8427e  something-else.zip\n"
        "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789  "
        "MeetRec-1.2.0-win64-with-ffmpeg.zip\n";
    const auto hash = FindChecksum(sums, L"MeetRec-1.2.0-win64-with-ffmpeg.zip");
    VSR_CHECK(hash.has_value());
    VSR_CHECK_EQ(*hash, std::string("abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"));

    VSR_CHECK(!FindChecksum(sums, L"MeetRec-9.9.9-win64-with-ffmpeg.zip").has_value());
}

VSR_TEST(Update, ParsesLatestReleaseAndSelectsAssets) {
    const std::string json = R"({
        "tag_name": "v1.2.0",
        "html_url": "https://github.com/ituff/meetrec/releases/tag/v1.2.0",
        "draft": false,
        "prerelease": false,
        "assets": [
            {"name": "SHA256SUMS.txt", "size": 123,
             "browser_download_url": "https://example.test/SHA256SUMS.txt"},
            {"name": "MeetRec-1.2.0-win64-with-ffmpeg.zip", "size": 200000000,
             "browser_download_url": "https://example.test/MeetRec-1.2.0-win64-with-ffmpeg.zip"}
        ]
    })";
    auto release = ParseLatestReleaseJson(json);
    VSR_CHECK(release.IsOk());
    if (release.IsOk()) {
        VSR_CHECK_EQ(release.Value().version.ToString(), std::string("1.2.0"));
        VSR_CHECK_EQ(release.Value().zipName,
                     std::wstring(L"MeetRec-1.2.0-win64-with-ffmpeg.zip"));
        VSR_CHECK_EQ(release.Value().zipSize, static_cast<std::uint64_t>(200000000));
        VSR_CHECK(release.Value().sumsUrl.find(L"SHA256SUMS.txt") != std::wstring::npos);
    }
}

VSR_TEST(Update, RejectsPrereleaseAndMissingAssets) {
    const std::string prerelease = R"({
        "tag_name": "v1.3.0",
        "draft": false,
        "prerelease": true,
        "assets": []
    })";
    VSR_CHECK(!ParseLatestReleaseJson(prerelease).IsOk());

    const std::string missingSums = R"({
        "tag_name": "v1.3.0",
        "html_url": "https://example.test",
        "draft": false,
        "prerelease": false,
        "assets": [
            {"name": "MeetRec-1.3.0-win64-with-ffmpeg.zip", "size": 1,
             "browser_download_url": "https://example.test/x.zip"}
        ]
    })";
    VSR_CHECK(!ParseLatestReleaseJson(missingSums).IsOk());
}

VSR_TEST(Update, Sha256MatchesKnownDigest) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / L"meetrec-sha256-test.txt";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "abc";
    }
    auto digest = Sha256File(path);
    VSR_CHECK(digest.IsOk());
    if (digest.IsOk()) {
        VSR_CHECK_EQ(digest.Value(),
                     std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}
