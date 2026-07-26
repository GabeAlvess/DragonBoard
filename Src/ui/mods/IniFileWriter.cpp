#include "ui/mods/IniFileWriter.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cwctype>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>
#include <vector>

namespace dragonboard::ui::mods
{
    namespace
    {
        struct ParsedDraftKey
        {
            std::size_t modIndex = 0;
            std::size_t fileIndex = 0;
            std::size_t settingIndex = 0;
        };

        struct PendingFile
        {
            std::filesystem::path path;
            std::filesystem::path temporary;
            std::string originalBytes;
            std::string updatedBytes;
        };

        std::optional<ParsedDraftKey> ParseDraftKey(std::string_view key)
        {
            ParsedDraftKey parsed;
            char separator1 = 0;
            char separator2 = 0;
            std::istringstream stream{ std::string(key) };
            if (stream >> parsed.modIndex >> separator1 >> parsed.fileIndex >>
                    separator2 >> parsed.settingIndex &&
                separator1 == ':' && separator2 == ':' && stream.eof()) {
                return parsed;
            }
            return std::nullopt;
        }

        std::string Sha256(const std::string& bytes)
        {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            DWORD objectLength = 0;
            DWORD hashLength = 0;
            DWORD copied = 0;
            std::vector<UCHAR> object;
            std::vector<UCHAR> digest;

            if (BCryptOpenAlgorithmProvider(
                    &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
                BCryptGetProperty(
                    algorithm,
                    BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&objectLength),
                    sizeof(objectLength),
                    &copied,
                    0) < 0 ||
                BCryptGetProperty(
                    algorithm,
                    BCRYPT_HASH_LENGTH,
                    reinterpret_cast<PUCHAR>(&hashLength),
                    sizeof(hashLength),
                    &copied,
                    0) < 0) {
                if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
                return {};
            }

            object.resize(objectLength);
            digest.resize(hashLength);
            if (BCryptCreateHash(
                    algorithm,
                    &hash,
                    object.data(),
                    static_cast<ULONG>(object.size()),
                    nullptr,
                    0,
                    0) < 0 ||
                BCryptHashData(
                    hash,
                    reinterpret_cast<PUCHAR>(
                        const_cast<char*>(bytes.data())),
                    static_cast<ULONG>(bytes.size()),
                    0) < 0 ||
                BCryptFinishHash(
                    hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
                if (hash) BCryptDestroyHash(hash);
                BCryptCloseAlgorithmProvider(algorithm, 0);
                return {};
            }
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);

            std::string result;
            result.reserve(digest.size() * 2);
            for (const auto byte : digest) result += std::format("{:02x}", byte);
            return result;
        }

        std::string ReadBytes(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            return {
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>() };
        }

        bool IsWithin(
            const std::filesystem::path& candidate,
            const std::filesystem::path& root)
        {
            std::error_code error;
            const auto canonicalCandidate = std::filesystem::weakly_canonical(candidate, error);
            if (error) return false;
            const auto canonicalRoot = std::filesystem::weakly_canonical(root, error);
            if (error) return false;
            auto candidateIterator = canonicalCandidate.begin();
            for (auto rootIterator = canonicalRoot.begin();
                 rootIterator != canonicalRoot.end();
                 ++rootIterator, ++candidateIterator) {
                if (candidateIterator == canonicalCandidate.end()) return false;
                std::wstring left = rootIterator->wstring();
                std::wstring right = candidateIterator->wstring();
                std::transform(
                    left.begin(), left.end(), left.begin(),
                    [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
                std::transform(
                    right.begin(), right.end(), right.begin(),
                    [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
                if (left != right) return false;
            }
            return true;
        }

        std::string Trim(std::string value)
        {
            const auto first = std::find_if_not(
                value.begin(), value.end(),
                [](unsigned char character) { return std::isspace(character) != 0; });
            const auto last = std::find_if_not(
                value.rbegin(), value.rend(),
                [](unsigned char character) { return std::isspace(character) != 0; }).base();
            return first < last ? std::string(first, last) : std::string{};
        }

        std::optional<std::string> EncodeValue(
            std::string_view value,
            std::string_view encoding,
            std::string& error)
        {
            if (encoding != "cp1252") return std::string(value);
            if (value.empty()) return std::string{};
            const int wideLength = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0);
            if (wideLength <= 0) {
                error = "The entered INI value is not valid UTF-8.";
                return std::nullopt;
            }
            std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
            MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                wide.data(),
                wideLength);
            BOOL usedDefault = FALSE;
            const int encodedLength = WideCharToMultiByte(
                1252,
                WC_NO_BEST_FIT_CHARS,
                wide.data(),
                wideLength,
                nullptr,
                0,
                nullptr,
                &usedDefault);
            if (encodedLength <= 0 || usedDefault) {
                error =
                    "The entered value contains characters that this CP1252 INI cannot store.";
                return std::nullopt;
            }
            std::string encoded(static_cast<std::size_t>(encodedLength), '\0');
            WideCharToMultiByte(
                1252,
                WC_NO_BEST_FIT_CHARS,
                wide.data(),
                wideLength,
                encoded.data(),
                encodedLength,
                nullptr,
                &usedDefault);
            if (usedDefault) {
                error =
                    "The entered value contains characters that this CP1252 INI cannot store.";
                return std::nullopt;
            }
            return encoded;
        }

        bool ReplaceSettingLine(
            std::string& bytes,
            const IniSetting& setting,
            const std::string& replacement,
            std::string& error)
        {
            std::size_t lineStart = 0;
            for (std::size_t line = 1; line < setting.line; ++line) {
                const auto newline = bytes.find('\n', lineStart);
                if (newline == std::string::npos) {
                    error = std::format(
                        "Line {} for '{}' no longer exists.", setting.line, setting.key);
                    return false;
                }
                lineStart = newline + 1;
            }
            auto lineEnd = bytes.find('\n', lineStart);
            if (lineEnd == std::string::npos) lineEnd = bytes.size();
            auto contentEnd = lineEnd;
            if (contentEnd > lineStart && bytes[contentEnd - 1] == '\r') --contentEnd;
            const auto equals = bytes.find('=', lineStart);
            if (equals == std::string::npos || equals >= contentEnd) {
                error = std::format(
                    "Setting '{}' no longer has a value on line {}.",
                    setting.key,
                    setting.line);
                return false;
            }
            if (Trim(bytes.substr(lineStart, equals - lineStart)) != setting.key) {
                error = std::format(
                    "Setting '{}' moved or changed externally.", setting.key);
                return false;
            }

            auto valueStart = equals + 1;
            while (valueStart < contentEnd &&
                   (bytes[valueStart] == ' ' || bytes[valueStart] == '\t')) {
                ++valueStart;
            }
            bytes.replace(valueStart, contentEnd - valueStart, replacement);
            return true;
        }

    }

    IniSaveResult SaveIniDrafts(
        const IniCatalog& catalog,
        const std::unordered_map<std::string, std::string>& drafts)
    {
        IniSaveResult result;
        if (drafts.empty()) {
            result.message = "There are no INI changes to save.";
            return result;
        }

        using FileKey = std::pair<std::size_t, std::size_t>;
        std::map<FileKey, std::vector<std::pair<std::size_t, std::string>>> grouped;
        for (const auto& [key, value] : drafts) {
            const auto parsed = ParseDraftKey(key);
            if (!parsed || parsed->modIndex >= catalog.Mods().size()) {
                result.message = "The INI draft contains an invalid mod reference.";
                return result;
            }
            const auto& mod = catalog.Mods()[parsed->modIndex];
            if (parsed->fileIndex >= mod.files.size() ||
                parsed->settingIndex >= mod.files[parsed->fileIndex].settings.size()) {
                result.message = "The INI draft contains an invalid setting reference.";
                return result;
            }
            grouped[{ parsed->modIndex, parsed->fileIndex }].push_back(
                { parsed->settingIndex, value });
        }

        std::vector<PendingFile> pending;
        for (const auto& [fileKey, edits] : grouped) {
            const auto& mod = catalog.Mods()[fileKey.first];
            const auto& file = mod.files[fileKey.second];
            if (!file.effectiveProvider) {
                result.message = std::format(
                    "'{}' is overridden by '{}'; edit the effective provider instead.",
                    file.relativePath,
                    file.effectiveProviderName);
                return result;
            }
            const auto providerRoot =
                mod.overwrite ? catalog.OverwriteDirectory() :
                                catalog.ModsDirectory() / mod.folder;
            const auto path = providerRoot / std::filesystem::path(file.relativePath);
            if (!std::filesystem::is_regular_file(path) || !IsWithin(path, providerRoot)) {
                result.message = "Refused an INI path outside its authorized MO2 provider.";
                return result;
            }

            auto originalBytes = ReadBytes(path);
            if (Sha256(originalBytes) != file.sha256) {
                result.message = std::format(
                    "'{}' changed after the last scan. Refresh the list before saving.",
                    file.relativePath);
                return result;
            }
            if (file.encoding == "utf-16") {
                result.message = std::format(
                    "UTF-16 INI writing is not supported yet for '{}'.",
                    file.relativePath);
                return result;
            }

            auto updatedBytes = originalBytes;
            auto sortedEdits = edits;
            std::sort(
                sortedEdits.begin(),
                sortedEdits.end(),
                [&](const auto& left, const auto& right) {
                    return file.settings[left.first].line >
                        file.settings[right.first].line;
                });
            std::string error;
            for (const auto& [settingIndex, value] : sortedEdits) {
                const auto encodedValue = EncodeValue(value, file.encoding, error);
                if (!encodedValue ||
                    !ReplaceSettingLine(
                        updatedBytes,
                        file.settings[settingIndex],
                        *encodedValue,
                        error)) {
                    result.message = std::move(error);
                    return result;
                }
            }

            const auto temporary = path.parent_path() /
                (path.filename().wstring() + L".dragonboard.tmp");
            pending.push_back({
                path,
                temporary,
                std::move(originalBytes),
                std::move(updatedBytes) });
        }

        std::error_code error;
        for (auto& file : pending) {
            {
                std::ofstream stream(file.temporary, std::ios::binary | std::ios::trunc);
                stream.write(
                    file.updatedBytes.data(),
                    static_cast<std::streamsize>(file.updatedBytes.size()));
                stream.flush();
                if (!stream) {
                    result.message = "Could not stage an updated INI file.";
                    return result;
                }
            }
        }

        std::size_t committed = 0;
        for (; committed < pending.size(); ++committed) {
            const auto& file = pending[committed];
            if (!MoveFileExW(
                    file.temporary.c_str(),
                    file.path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                for (std::size_t rollback = 0; rollback < committed; ++rollback) {
                    auto& committedFile = pending[rollback];
                    {
                        std::ofstream stream(
                            committedFile.temporary,
                            std::ios::binary | std::ios::trunc);
                        stream.write(
                            committedFile.originalBytes.data(),
                            static_cast<std::streamsize>(
                                committedFile.originalBytes.size()));
                        stream.flush();
                    }
                    MoveFileExW(
                        committedFile.temporary.c_str(),
                        committedFile.path.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
                }
                for (std::size_t cleanup = committed; cleanup < pending.size(); ++cleanup) {
                    std::filesystem::remove(pending[cleanup].temporary, error);
                }
                result.message = std::format(
                    "Could not replace an INI file (Win32 {}). Changes were rolled back.",
                    GetLastError());
                return result;
            }
        }

        result.success = true;
        result.filesWritten = pending.size();
        result.message = std::format(
            "Saved {} INI file{}.",
            result.filesWritten,
            result.filesWritten == 1 ? "" : "s");
        return result;
    }
}
