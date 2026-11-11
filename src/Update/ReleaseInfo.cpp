#include "ReleaseInfo.h"

#include <Infrastructure/Json.h>
#include <Infrastructure/Strings.h>

namespace vsr::update {

namespace {

bool EndsWith(const std::string& text, const char* suffix) {
    const std::size_t length = std::char_traits<char>::length(suffix);
    return text.size() >= length &&
           text.compare(text.size() - length, length, suffix) == 0;
}

} // namespace

Result<ReleaseInfo> ParseLatestReleaseJson(const std::string& jsonText) {
    bool parsed = false;
    const json::Value root = json::Parse(jsonText, &parsed);
    if (!parsed || root.GetType() != json::Type::Object) {
        return Error(ErrorCode::InternalState, L"The update server returned invalid data.",
                     L"GitHub release JSON could not be parsed");
    }
    const json::Value& draft = root.Find("draft");
    const json::Value& prerelease = root.Find("prerelease");
    if ((draft.GetType() == json::Type::Boolean && draft.AsBool()) ||
        (prerelease.GetType() == json::Type::Boolean && prerelease.AsBool())) {
        return Error(ErrorCode::InternalState, L"The latest release is not a stable build.",
                     L"Latest GitHub release is draft or prerelease");
    }

    ReleaseInfo release;
    release.tagName = root.Find("tag_name").AsString();
    const auto version = SemanticVersion::Parse(release.tagName);
    if (!version) {
        return Error(ErrorCode::InternalState, L"The latest release has an invalid version.",
                     L"Unparsable tag_name: " + Utf8ToWide(release.tagName));
    }
    release.version = *version;
    release.htmlUrl = Utf8ToWide(root.Find("html_url").AsString());

    const std::string expectedZip =
        "MeetRec-" + version->ToString() + "-win64-with-ffmpeg.zip";
    bool haveFallbackZip = false;
    for (const json::Value& asset : root.Find("assets").GetArray()) {
        const std::string name = asset.Find("name").AsString();
        const std::wstring url = Utf8ToWide(asset.Find("browser_download_url").AsString());
        if (name == "SHA256SUMS.txt") {
            release.sumsUrl = url;
        } else if (name == expectedZip) {
            release.zipName = Utf8ToWide(name);
            release.zipUrl = url;
            release.zipSize =
                static_cast<std::uint64_t>(asset.Find("size").AsInt(0));
        } else if (!haveFallbackZip && EndsWith(name, "-win64-with-ffmpeg.zip")) {
            haveFallbackZip = true;
            release.zipName = Utf8ToWide(name);
            release.zipUrl = url;
            release.zipSize =
                static_cast<std::uint64_t>(asset.Find("size").AsInt(0));
        }
    }

    if (release.zipUrl.empty()) {
        return Error(ErrorCode::InternalState, L"The release package was not found.",
                     L"No *-win64-with-ffmpeg.zip asset in the latest release");
    }
    if (release.sumsUrl.empty()) {
        return Error(ErrorCode::InternalState,
                     L"The release checksum file was not found.",
                     L"No SHA256SUMS.txt asset in the latest release");
    }
    return release;
}

} // namespace vsr::update
