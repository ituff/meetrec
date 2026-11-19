#include "UpdateChecker.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <vector>

#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>

#pragma comment(lib, "bcrypt.lib")

namespace vsr::update {

namespace {

constexpr wchar_t kDefaultReleaseUrl[] =
    L"https://api.github.com/repos/ituff/meetrec/releases/latest";
// Reverse proxy for GitHub release assets (China-accessible CDN); the
// original URL is appended verbatim after the prefix.
constexpr wchar_t kDownloadProxyPrefix[] = L"https://jscdn.msone.top/";
constexpr wchar_t kGitHubPrefix[] = L"https://github.com/";

std::wstring Trim(std::wstring text) {
    while (!text.empty() && iswspace(text.back())) text.pop_back();
    std::size_t start = 0;
    while (start < text.size() && iswspace(text[start])) ++start;
    return text.substr(start);
}

std::string Lowercase(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

} // namespace

std::wstring LatestReleaseUrl() {
    wchar_t overrideUrl[1024] = {};
    if (GetEnvironmentVariableW(L"VSR_UPDATE_API_URL", overrideUrl, 1024) > 0) {
        return std::wstring(overrideUrl);
    }
    return kDefaultReleaseUrl;
}

std::wstring ProxiedDownloadUrl(const std::wstring& url) {
    if (url.rfind(kGitHubPrefix, 0) == 0) {
        return std::wstring(kDownloadProxyPrefix) + url;
    }
    return url;
}

std::optional<std::string> FindChecksum(const std::string& sumsText,
                                        const std::wstring& fileName) {
    const std::string target = WideToUtf8(fileName);
    std::size_t pos = 0;
    while (pos < sumsText.size()) {
        const std::size_t end = sumsText.find('\n', pos);
        const std::string line =
            sumsText.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        const std::size_t space = line.find_first_of(" \t");
        if (space != std::string::npos) {
            std::string hash = line.substr(0, space);
            std::size_t nameStart = line.find_first_not_of(" \t", space);
            if (nameStart != std::string::npos) {
                std::string name = line.substr(nameStart);
                if (!name.empty() && name.front() == '*') name.erase(0, 1);
                while (!name.empty() &&
                       iswspace(static_cast<wint_t>(
                           static_cast<unsigned char>(name.back())))) {
                    name.pop_back();
                }
                if (Lowercase(name) == Lowercase(target) && hash.size() == 64) {
                    return Lowercase(hash);
                }
            }
        }
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return std::nullopt;
}

Result<UpdateCheck> CheckForUpdate(const std::string& currentVersion) {
    const auto current = SemanticVersion::Parse(currentVersion);
    if (!current) {
        return Error(ErrorCode::InternalState, L"This build's version is invalid.",
                     L"Cannot parse current version: " + Utf8ToWide(currentVersion));
    }
    auto response = HttpGetText(LatestReleaseUrl());
    if (!response) return response.TakeError();
    auto release = ParseLatestReleaseJson(response.Value().body);
    if (!release) return release.TakeError();

    UpdateCheck check;
    check.release = release.Value();
    check.available = release.Value().version > *current;
    return check;
}

Result<std::string> Sha256File(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        return Error(ErrorCode::InternalState, L"Could not read the update package.",
                     L"Failed to open " + path.wstring());
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                                    nullptr, 0))) {
        return ErrorFromWin32(GetLastError(), ErrorCode::InternalState,
                              L"Could not verify the update package.",
                              L"BCryptOpenAlgorithmProvider failed");
    }
    DWORD objectSize = 0;
    DWORD resultSize = 0;
    DWORD hashLength = 0;
    std::vector<unsigned char> object;
    std::vector<unsigned char> digest;
    Result<void> status;
    if (!BCRYPT_SUCCESS(BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &resultSize, 0)) ||
        !BCRYPT_SUCCESS(BCryptGetProperty(
            algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength),
            sizeof(hashLength), &resultSize, 0))) {
        status = ErrorFromWin32(GetLastError(), ErrorCode::InternalState,
                                L"Could not verify the update package.",
                                L"BCryptGetProperty failed");
    }
    if (status.IsOk() && hashLength != 32) {
        status = Error(ErrorCode::InternalState, L"Could not verify the update package.",
                       L"Unexpected SHA-256 digest length");
    }
    if (status.IsOk()) {
        digest.resize(hashLength);
        object.resize(objectSize);
        if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, object.data(), objectSize,
                                             nullptr, 0, 0))) {
            status = ErrorFromWin32(GetLastError(), ErrorCode::InternalState,
                                    L"Could not verify the update package.",
                                    L"BCryptCreateHash failed");
        }
    }

    std::vector<char> buffer(1024 * 1024);
    bool readFailed = false;
    while (status.IsOk()) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = file.gcount();
        if (read > 0) {
            if (!BCRYPT_SUCCESS(BCryptHashData(
                    hash, reinterpret_cast<PUCHAR>(buffer.data()),
                    static_cast<ULONG>(read), 0))) {
                status = ErrorFromWin32(GetLastError(), ErrorCode::InternalState,
                                        L"Could not verify the update package.",
                                        L"BCryptHashData failed");
                break;
            }
        }
        if (file.eof()) break;
        if (!file.good()) {
            readFailed = true;
            break;
        }
    }
    if (status.IsOk() && readFailed) {
        status = Error(ErrorCode::InternalState, L"Could not read the update package.",
                       L"Read failed for " + path.wstring());
    }
    if (status.IsOk() && !BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(),
                                                          static_cast<ULONG>(digest.size()),
                                                          0))) {
        status = ErrorFromWin32(GetLastError(), ErrorCode::InternalState,
                                L"Could not verify the update package.",
                                L"BCryptFinishHash failed");
    }

    if (hash != nullptr) BCryptDestroyHash(hash);
    if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!status) return status.TakeError();

    static constexpr char kHex[] = "0123456789abcdef";
    std::string text;
    text.reserve(digest.size() * 2);
    for (const unsigned char byte : digest) {
        text.push_back(kHex[byte >> 4]);
        text.push_back(kHex[byte & 0x0F]);
    }
    return text;
}

Result<std::filesystem::path> DownloadVerified(const ReleaseInfo& release,
                                               const std::filesystem::path& destinationDir,
                                               const std::atomic<bool>& cancel,
                                               const DownloadProgress& onProgress) {
    std::error_code ec;
    std::filesystem::create_directories(destinationDir, ec);
    if (ec) {
        return Error(ErrorCode::OutputNotWritable,
                     L"Could not create the update folder.",
                     L"create_directories failed for " + destinationDir.wstring());
    }

    std::string expected;
    if (!release.sha256.empty()) {
        // The release JSON (fetched directly from api.github.com) carries the
        // package hash, so a download proxy cannot substitute another zip.
        expected = release.sha256;
    } else {
        // Older releases have no hash in the body: fetch SHA256SUMS.txt.
        // Prefer the download proxy for reachability, then GitHub directly.
        auto sums = HttpGetText(ProxiedDownloadUrl(release.sumsUrl));
        if (!sums) sums = HttpGetText(release.sumsUrl);
        if (!sums) return sums.TakeError();
        const auto found = FindChecksum(sums.Value().body, release.zipName);
        if (!found) {
            return Error(ErrorCode::InternalState,
                         L"The update checksum could not be found.",
                         L"SHA256SUMS.txt has no entry for " + release.zipName);
        }
        expected = *found;
    }

    const std::filesystem::path zipPath = destinationDir / release.zipName;
    if (std::filesystem::exists(zipPath, ec) && !ec) {
        auto cached = Sha256File(zipPath);
        if (cached && cached.Value() == expected) {
            std::error_code sizeEc;
            const std::uintmax_t size = std::filesystem::file_size(zipPath, sizeEc);
            if (!sizeEc && onProgress) {
                onProgress(static_cast<std::uint64_t>(size),
                           static_cast<std::uint64_t>(size));
            }
            return zipPath;
        }
    }

    const std::wstring directUrl = release.zipUrl;
    const std::wstring proxiedUrl = ProxiedDownloadUrl(directUrl);
    const bool usingProxy = proxiedUrl != directUrl;

    auto attempt = [&](const std::wstring& url) -> Result<std::filesystem::path> {
        auto downloaded = HttpDownloadFile(url, zipPath, cancel, onProgress);
        if (!downloaded) return downloaded.TakeError();
        if (cancel.load()) {
            return Error(ErrorCode::InternalState, L"The update download was cancelled.",
                         L"Download cancelled by the user");
        }
        auto actual = Sha256File(zipPath);
        if (!actual) {
            std::error_code removeEc;
            std::filesystem::remove(zipPath, removeEc);
            return actual.TakeError();
        }
        if (actual.Value() != expected) {
            std::error_code removeEc;
            std::filesystem::remove(zipPath, removeEc);
            return Error(ErrorCode::InternalState,
                         L"The update package failed verification.",
                         L"SHA-256 mismatch: expected " + Utf8ToWide(expected) +
                             L", got " + Utf8ToWide(actual.Value()));
        }
        return zipPath;
    };

    auto result = attempt(proxiedUrl);
    if (!result && usingProxy && !cancel.load()) {
        // The proxy can be down, rate-limited, or serve a stale asset. Keep
        // updates working by retrying the same asset on github.com directly.
        // New releases use the hash from the API JSON fetched directly from
        // api.github.com, so this retry remains verified even if the proxy is
        // compromised.
        log::Warning("Update", "UPDATE_PROXY_FALLBACK", L"",
                     L"Accelerated update download failed; retrying GitHub directly.",
                     {{"url", WideToUtf8(proxiedUrl)},
                      {"detail", WideToUtf8(result.GetError().technicalMessage)}});
        std::error_code removeEc;
        std::filesystem::remove(zipPath, removeEc);
        result = attempt(directUrl);
    }
    return result;
}

} // namespace vsr::update
