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

VSR_TEST(Update, DownloadProxyOnlyRewritesGitHubAssetUrls) {
    const std::wstring github =
        L"https://github.com/ituff/meetrec/releases/download/v1.5.1/"
        L"MeetRec-1.5.1-win64-with-ffmpeg.zip";
    VSR_CHECK(ProxiedDownloadUrl(github) ==
              std::wstring(L"https://jscdn.msone.top/") + github);

    const std::wstring other = L"https://example.test/asset.zip";
    VSR_CHECK(ProxiedDownloadUrl(other) == other);

    const std::wstring api =
        L"https://api.github.com/repos/ituff/meetrec/releases/latest";
    VSR_CHECK(ProxiedDownloadUrl(api) == api);

    const std::wstring raw = L"https://raw.githubusercontent.com/a/b/c";
    VSR_CHECK(ProxiedDownloadUrl(raw) == raw);
}

VSR_TEST(Update, ParsesTrustedBodyChecksum) {
    const std::string json = R"({
        "tag_name": "v1.5.0",
        "html_url": "https://github.com/ituff/meetrec/releases/tag/v1.5.0",
        "draft": false,
        "prerelease": false,
        "body": "The package hash is written as SHA256: <hash> below.\n\nSHA256: 87EB46E7452061BD5F165C68F3D9B14C72D4F489F26EB8950BCFD44EBFCA6D38\n",
        "assets": [
            {"name": "SHA256SUMS.txt", "size": 103,
             "browser_download_url": "https://github.com/ituff/meetrec/releases/download/v1.5.0/SHA256SUMS.txt"},
            {"name": "MeetRec-1.5.0-win64-with-ffmpeg.zip", "size": 123,
             "browser_download_url": "https://github.com/ituff/meetrec/releases/download/v1.5.0/MeetRec-1.5.0-win64-with-ffmpeg.zip"}
        ]
    })";
    auto release = ParseLatestReleaseJson(json);
    VSR_CHECK(release.IsOk());
    if (release.IsOk()) {
        VSR_CHECK_EQ(release.Value().sha256,
                     std::string("87eb46e7452061bd5f165c68f3d9b14c72d4f489f26eb8950bcfd44ebfca6d38"));
    }

    const std::string noBodyHash = R"({
        "tag_name": "v1.4.0",
        "draft": false,
        "prerelease": false,
        "body": "release notes without a checksum",
        "assets": [
            {"name": "SHA256SUMS.txt", "size": 103,
             "browser_download_url": "https://example.test/SHA256SUMS.txt"},
            {"name": "MeetRec-1.4.0-win64-with-ffmpeg.zip", "size": 123,
             "browser_download_url": "https://example.test/MeetRec-1.4.0-win64-with-ffmpeg.zip"}
        ]
    })";
    auto older = ParseLatestReleaseJson(noBodyHash);
    VSR_CHECK(older.IsOk());
    if (older.IsOk()) VSR_CHECK(older.Value().sha256.empty());
}
