#include "updater.h"

#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef BONDERIUM_VERSION
#define BONDERIUM_VERSION "0.0.0"
#endif

#ifndef BONDERIUM_ENABLE_UPDATES
#define BONDERIUM_ENABLE_UPDATES 0
#endif

#if BONDERIUM_ENABLE_UPDATES
namespace
{
constexpr wchar_t release_host[] = L"api.github.com";
constexpr wchar_t release_path[] =
    L"/repos/finiksmacom11-rgb/Bonderium/releases?per_page=100";
constexpr wchar_t download_host[] = L"github.com";
constexpr wchar_t ignored_update_key[] = L"Software\\Bonderium";
constexpr wchar_t ignored_update_value[] = L"IgnoredUpdateTag";
constexpr std::size_t maximum_download_size = 100U * 1024U * 1024U;

struct SemanticVersion
{
    std::array<unsigned long long, 3> core{};
    std::vector<std::string> prerelease;
};

struct Release
{
    std::string tag;
    SemanticVersion version;
    bool is_prerelease = false;
    bool available = false;
};

class InternetHandle
{
public:
    explicit InternetHandle(HINTERNET handle) : handle_(handle) {}
    ~InternetHandle()
    {
        if (handle_ != nullptr)
        {
            WinHttpCloseHandle(handle_);
        }
    }

    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;

    HINTERNET get() const
    {
        return handle_;
    }

private:
    HINTERNET handle_;
};

bool parse_numeric_identifier(std::string_view text, unsigned long long& value)
{
    if (text.empty() || (text.size() > 1 && text[0] == '0'))
    {
        return false;
    }

    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parse_version(std::string_view text, SemanticVersion& version)
{
    if (!text.empty() && text[0] == 'v')
    {
        text.remove_prefix(1);
    }

    const std::size_t prerelease_position = text.find('-');
    const std::string_view core = text.substr(0, prerelease_position);
    std::size_t component_start = 0;
    for (std::size_t index = 0; index < version.core.size(); ++index)
    {
        const std::size_t separator = core.find('.', component_start);
        const std::size_t component_end =
            separator == std::string_view::npos ? core.size() : separator;
        if ((index < version.core.size() - 1 &&
                separator == std::string_view::npos) ||
            (index == version.core.size() - 1 &&
                separator != std::string_view::npos) ||
            !parse_numeric_identifier(
                core.substr(component_start, component_end - component_start),
                version.core[index]))
        {
            return false;
        }
        component_start = component_end + 1;
    }

    if (prerelease_position == std::string_view::npos)
    {
        return true;
    }

    std::string_view prerelease = text.substr(prerelease_position + 1);
    if (prerelease.empty())
    {
        return false;
    }
    while (!prerelease.empty())
    {
        const std::size_t separator = prerelease.find('.');
        const std::string_view identifier = prerelease.substr(0, separator);
        if (identifier.empty())
        {
            return false;
        }
        for (const char character : identifier)
        {
            const bool valid =
                (character >= '0' && character <= '9') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= 'a' && character <= 'z') ||
                character == '-';
            if (!valid)
            {
                return false;
            }
        }
        if (identifier.size() > 1 && identifier[0] == '0' &&
            std::all_of(identifier.begin(), identifier.end(),
                [](char character) {
                    return character >= '0' && character <= '9';
                }))
        {
            return false;
        }
        version.prerelease.emplace_back(identifier);
        if (separator == std::string_view::npos)
        {
            break;
        }
        prerelease.remove_prefix(separator + 1);
    }

    return true;
}

int compare_versions(const SemanticVersion& left, const SemanticVersion& right)
{
    if (left.core != right.core)
    {
        return left.core < right.core ? -1 : 1;
    }

    if (left.prerelease.empty() != right.prerelease.empty())
    {
        return left.prerelease.empty() ? 1 : -1;
    }

    const std::size_t count =
        (std::min)(left.prerelease.size(), right.prerelease.size());
    for (std::size_t index = 0; index < count; ++index)
    {
        const std::string& left_identifier = left.prerelease[index];
        const std::string& right_identifier = right.prerelease[index];
        const bool left_numeric = std::all_of(
            left_identifier.begin(), left_identifier.end(),
            [](char character) {
                return character >= '0' && character <= '9';
            });
        const bool right_numeric = std::all_of(
            right_identifier.begin(), right_identifier.end(),
            [](char character) {
                return character >= '0' && character <= '9';
            });
        if (left_numeric && right_numeric)
        {
            if (left_identifier.size() != right_identifier.size())
            {
                return left_identifier.size() < right_identifier.size() ? -1 : 1;
            }
            if (left_identifier != right_identifier)
            {
                return left_identifier < right_identifier ? -1 : 1;
            }
        }
        else if (left_numeric != right_numeric)
        {
            return left_numeric ? -1 : 1;
        }
        else if (left_identifier != right_identifier)
        {
            return left_identifier < right_identifier ? -1 : 1;
        }
    }

    if (left.prerelease.size() == right.prerelease.size())
    {
        return 0;
    }
    return left.prerelease.size() < right.prerelease.size() ? -1 : 1;
}

bool read_response(HINTERNET request, std::string& response)
{
    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    if (!WinHttpQueryHeaders(request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
            WINHTTP_NO_HEADER_INDEX) ||
        status_code != 200)
    {
        return false;
    }

    std::array<char, 8192> buffer{};
    for (;;)
    {
        DWORD bytes_available = 0;
        if (!WinHttpQueryDataAvailable(request, &bytes_available))
        {
            return false;
        }
        if (bytes_available == 0)
        {
            return true;
        }
        if (response.size() + bytes_available > maximum_download_size)
        {
            return false;
        }

        const std::size_t amount =
            (std::min)(buffer.size(), static_cast<std::size_t>(bytes_available));
        DWORD bytes_read = 0;
        if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(amount), &bytes_read))
        {
            return false;
        }
        if (bytes_read == 0)
        {
            return true;
        }
        response.append(buffer.data(), bytes_read);
    }
}

bool get_https(const wchar_t* host, const wchar_t* path, std::string& response)
{
    InternetHandle session(WinHttpOpen(L"Bonderium",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0));
    if (session.get() == nullptr)
    {
        return false;
    }
    WinHttpSetTimeouts(session.get(), 1500, 2000, 3000, 3000);

    InternetHandle connection(
        WinHttpConnect(session.get(), host, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (connection.get() == nullptr)
    {
        return false;
    }

    InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", path,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (request.get() == nullptr)
    {
        return false;
    }

    constexpr wchar_t headers[] =
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";
    return WinHttpAddRequestHeaders(request.get(), headers,
               static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD) &&
        WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request.get(), nullptr) &&
        read_response(request.get(), response);
}

void skip_json_whitespace(std::string_view json, std::size_t& position)
{
    while (position < json.size() &&
        (json[position] == ' ' || json[position] == '\t' ||
            json[position] == '\r' || json[position] == '\n'))
    {
        ++position;
    }
}

bool parse_json_string(std::string_view json, std::size_t& position,
    std::string& value)
{
    if (position >= json.size() || json[position] != '"')
    {
        return false;
    }

    ++position;
    value.clear();
    while (position < json.size())
    {
        const char character = json[position++];
        if (character == '"')
        {
            return true;
        }
        if (character != '\\')
        {
            value.push_back(character);
            continue;
        }
        if (position >= json.size())
        {
            return false;
        }

        const char escaped = json[position++];
        switch (escaped)
        {
        case '"':
        case '\\':
        case '/':
            value.push_back(escaped);
            break;
        case 'b':
            value.push_back('\b');
            break;
        case 'f':
            value.push_back('\f');
            break;
        case 'n':
            value.push_back('\n');
            break;
        case 'r':
            value.push_back('\r');
            break;
        case 't':
            value.push_back('\t');
            break;
        default:
            return false;
        }
    }

    return false;
}

bool skip_json_string(std::string_view json, std::size_t& position)
{
    if (position >= json.size() || json[position++] != '"')
    {
        return false;
    }

    while (position < json.size())
    {
        const char character = json[position++];
        if (character == '"')
        {
            return true;
        }
        if (character != '\\')
        {
            continue;
        }
        if (position >= json.size())
        {
            return false;
        }

        const char escaped = json[position++];
        if (escaped == 'u')
        {
            if (json.size() - position < 4)
            {
                return false;
            }
            for (unsigned int digit = 0; digit < 4; ++digit)
            {
                const char hex = json[position++];
                if (!((hex >= '0' && hex <= '9') ||
                        (hex >= 'A' && hex <= 'F') ||
                        (hex >= 'a' && hex <= 'f')))
                {
                    return false;
                }
            }
        }
        else if (escaped != '"' && escaped != '\\' && escaped != '/' &&
            escaped != 'b' && escaped != 'f' && escaped != 'n' &&
            escaped != 'r' && escaped != 't')
        {
            return false;
        }
    }
    return false;
}

bool skip_json_value(std::string_view json, std::size_t& position)
{
    if (position >= json.size())
    {
        return false;
    }

    if (json[position] == '"')
    {
        return skip_json_string(json, position);
    }

    if (json[position] == '{' || json[position] == '[')
    {
        const char opening = json[position++];
        const char closing = opening == '{' ? '}' : ']';
        unsigned int depth = 1;
        bool in_string = false;
        bool escaped = false;
        while (position < json.size())
        {
            const char character = json[position++];
            if (in_string)
            {
                if (escaped)
                {
                    escaped = false;
                }
                else if (character == '\\')
                {
                    escaped = true;
                }
                else if (character == '"')
                {
                    in_string = false;
                }
                continue;
            }
            if (character == '"')
            {
                in_string = true;
            }
            else if (character == opening)
            {
                ++depth;
            }
            else if (character == closing && --depth == 0)
            {
                return true;
            }
        }
        return false;
    }

    const std::size_t start = position;
    while (position < json.size() && json[position] != ',' &&
        json[position] != '}' && json[position] != ']' &&
        json[position] != ' ' && json[position] != '\t' &&
        json[position] != '\r' && json[position] != '\n')
    {
        ++position;
    }
    return position > start;
}

bool parse_release_object(std::string_view json, Release& release)
{
    std::size_t position = 0;
    skip_json_whitespace(json, position);
    if (position >= json.size() || json[position++] != '{')
    {
        return false;
    }

    bool found_tag = false;
    bool found_prerelease = false;
    while (position < json.size())
    {
        skip_json_whitespace(json, position);
        if (position < json.size() && json[position] == '}')
        {
            ++position;
            break;
        }

        std::string key;
        if (!parse_json_string(json, position, key))
        {
            return false;
        }
        skip_json_whitespace(json, position);
        if (position >= json.size() || json[position++] != ':')
        {
            return false;
        }
        skip_json_whitespace(json, position);

        if (key == "tag_name")
        {
            found_tag = parse_json_string(json, position, release.tag);
            if (!found_tag)
            {
                return false;
            }
        }
        else if (key == "prerelease")
        {
            if (json.substr(position, 4) == "true")
            {
                release.is_prerelease = true;
                position += 4;
            }
            else if (json.substr(position, 5) == "false")
            {
                release.is_prerelease = false;
                position += 5;
            }
            else
            {
                return false;
            }
            found_prerelease = true;
        }
        else if (!skip_json_value(json, position))
        {
            return false;
        }

        skip_json_whitespace(json, position);
        if (position < json.size() && json[position] == ',')
        {
            ++position;
        }
        else if (position < json.size() && json[position] != '}')
        {
            return false;
        }
    }

    release.available = found_tag && found_prerelease &&
        parse_version(release.tag, release.version);
    return true;
}

void consider_release(Release& latest, Release candidate)
{
    if (!candidate.available)
    {
        return;
    }
    if (!latest.available ||
        compare_versions(candidate.version, latest.version) > 0)
    {
        latest = std::move(candidate);
    }
}

bool get_latest_releases(Release& stable, Release& beta)
{
    std::string response;
    if (!get_https(release_host, release_path, response))
    {
        return false;
    }

    std::size_t position = 0;
    skip_json_whitespace(response, position);
    if (position >= response.size() || response[position++] != '[')
    {
        return false;
    }

    bool parsed_release = false;
    while (position < response.size())
    {
        skip_json_whitespace(response, position);
        if (position < response.size() && response[position] == ']')
        {
            return parsed_release;
        }
        if (position >= response.size() || response[position] != '{')
        {
            return false;
        }

        const std::size_t object_start = position;
        if (!skip_json_value(response, position))
        {
            return false;
        }

        Release candidate;
        if (!parse_release_object(
                std::string_view(response).substr(
                    object_start, position - object_start),
                candidate))
        {
            return false;
        }
        if (candidate.available)
        {
            parsed_release = true;
            consider_release(candidate.is_prerelease ? beta : stable,
                std::move(candidate));
        }

        skip_json_whitespace(response, position);
        if (position < response.size() && response[position] == ',')
        {
            ++position;
        }
        else if (position >= response.size() || response[position] != ']')
        {
            return false;
        }
    }
    return false;
}

bool read_ignored_version(std::string& ignored_version)
{
    HKEY key = nullptr;
    const LSTATUS open_status =
        RegOpenKeyExW(HKEY_CURRENT_USER, ignored_update_key, 0, KEY_QUERY_VALUE, &key);
    if (open_status == ERROR_FILE_NOT_FOUND)
    {
        ignored_version.clear();
        return true;
    }
    if (open_status != ERROR_SUCCESS)
    {
        return false;
    }

    std::array<wchar_t, 128> value{};
    DWORD value_size = static_cast<DWORD>(sizeof(value));
    DWORD value_type = 0;
    const LSTATUS query_status = RegQueryValueExW(key, ignored_update_value,
        nullptr, &value_type, reinterpret_cast<BYTE*>(value.data()), &value_size);
    RegCloseKey(key);
    if (query_status == ERROR_FILE_NOT_FOUND)
    {
        ignored_version.clear();
        return true;
    }
    if (query_status != ERROR_SUCCESS || value_type != REG_SZ ||
        value_size < sizeof(wchar_t) || value_size > sizeof(value))
    {
        return false;
    }

    ignored_version.clear();
    for (const wchar_t character : value)
    {
        if (character == L'\0')
        {
            break;
        }
        if (character > 0x7f)
        {
            return false;
        }
        ignored_version.push_back(static_cast<char>(character));
    }
    return true;
}

bool write_ignored_version(std::string_view version)
{
    HKEY key = nullptr;
    const LSTATUS create_status = RegCreateKeyExW(HKEY_CURRENT_USER,
        ignored_update_key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (create_status != ERROR_SUCCESS)
    {
        return false;
    }

    std::wstring value(version.begin(), version.end());
    const DWORD value_size = static_cast<DWORD>(
        (value.size() + 1) * sizeof(wchar_t));
    const LSTATUS set_status = RegSetValueExW(key, ignored_update_value, 0,
        REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), value_size);
    RegCloseKey(key);
    return set_status == ERROR_SUCCESS;
}

std::wstring executable_path()
{
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
        static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
    {
        return {};
    }
    return std::wstring(path.data(), length);
}

std::wstring quote_argument(const std::wstring& value)
{
    std::wstring quoted = L"\"";
    for (const wchar_t character : value)
    {
        if (character == L'"')
        {
            quoted += L'\\';
        }
        quoted += character;
    }
    quoted += L'"';
    return quoted;
}

bool download_update(std::string_view tag, std::wstring& archive_path)
{
    const std::wstring download_path =
        L"/finiksmacom11-rgb/Bonderium/releases/download/" +
        std::wstring(tag.begin(), tag.end()) +
        L"/Bonderium-windows-x64.zip";
    std::string response;
    if (!get_https(download_host, download_path.c_str(), response) ||
        response.empty())
    {
        return false;
    }

    std::array<wchar_t, MAX_PATH> temp_directory{};
    const DWORD directory_length =
        GetTempPathW(static_cast<DWORD>(temp_directory.size()), temp_directory.data());
    if (directory_length == 0 || directory_length >= temp_directory.size())
    {
        return false;
    }

    std::array<wchar_t, MAX_PATH> temp_file{};
    if (GetTempFileNameW(temp_directory.data(), L"bon", 0, temp_file.data()) == 0)
    {
        return false;
    }
    archive_path = std::wstring(temp_file.data()) + L".zip";
    if (!MoveFileW(temp_file.data(), archive_path.c_str()))
    {
        DeleteFileW(temp_file.data());
        archive_path.clear();
        return false;
    }

    HANDLE file = CreateFileW(archive_path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        DeleteFileW(archive_path.c_str());
        archive_path.clear();
        return false;
    }

    std::size_t offset = 0;
    bool success = true;
    while (offset < response.size())
    {
        const DWORD chunk_size = static_cast<DWORD>((std::min)(
            response.size() - offset,
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD bytes_written = 0;
        if (!WriteFile(file, response.data() + offset, chunk_size, &bytes_written, nullptr) ||
            bytes_written == 0)
        {
            success = false;
            break;
        }
        offset += bytes_written;
    }

    if (!CloseHandle(file) || !success)
    {
        DeleteFileW(archive_path.c_str());
        archive_path.clear();
        return false;
    }
    return true;
}

void show_update_error(HWND owner)
{
    if (IsWindow(owner))
    {
        MessageBoxW(owner,
            L"Не удалось скачать обновление. Проверьте подключение к интернету и попробуйте позже.",
            L"Bonderium", MB_OK | MB_ICONERROR);
    }
}

void check_updates(HWND owner)
{
    Release latest_stable;
    Release latest_beta;
    if (!get_latest_releases(latest_stable, latest_beta))
    {
        return;
    }

    std::string ignored_version;
    if (!read_ignored_version(ignored_version))
    {
        if (IsWindow(owner))
        {
            MessageBoxW(owner,
                L"Не удалось прочитать сохранённый выбор обновлений.",
                L"Bonderium", MB_OK | MB_ICONERROR);
        }
        return;
    }

    SemanticVersion current_version;
    if (!parse_version(BONDERIUM_VERSION, current_version))
    {
        return;
    }

    Release selected_release;
    for (const Release* candidate : {&latest_stable, &latest_beta})
    {
        if (!candidate->available || candidate->tag == ignored_version ||
            compare_versions(candidate->version, current_version) <= 0)
        {
            continue;
        }
        consider_release(selected_release, *candidate);
    }

    if (!selected_release.available || !IsWindow(owner))
    {
        return;
    }

    const std::wstring version(
        selected_release.tag.begin(), selected_release.tag.end());
    const bool is_beta = selected_release.is_prerelease;
    const std::wstring message = is_beta
        ? L"Доступна предварительная бета-версия Bonderium " + version +
            L". Если отказаться, эта версия больше не будет предлагаться, "
            L"но новые бета- и стабильные версии будут появляться. "
            L"Установить её сейчас?"
        : L"Доступна новая версия Bonderium " + version +
            L". Если отказаться, эта версия больше не будет предлагаться, "
            L"но новые обновления будут появляться. Установить её сейчас?";
    if (MessageBoxW(owner, message.c_str(),
            is_beta ? L"Бета-обновление Bonderium" : L"Обновление Bonderium",
            MB_YESNO | MB_ICONINFORMATION) != IDYES)
    {
        if (!write_ignored_version(selected_release.tag) && IsWindow(owner))
        {
            MessageBoxW(owner,
                L"Не удалось сохранить отказ от этой версии. Она может быть "
                L"предложена снова при следующем запуске.",
                L"Bonderium", MB_OK | MB_ICONERROR);
        }
        return;
    }

    std::wstring archive_path;
    if (!download_update(selected_release.tag, archive_path))
    {
        show_update_error(owner);
        return;
    }

    const std::wstring app_path = executable_path();
    if (app_path.empty())
    {
        DeleteFileW(archive_path.c_str());
        show_update_error(owner);
        return;
    }

    std::wstring updater_path = app_path;
    const std::size_t separator = updater_path.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
    {
        DeleteFileW(archive_path.c_str());
        show_update_error(owner);
        return;
    }
    updater_path.resize(separator + 1);
    updater_path += L"updater.ps1";
    if (GetFileAttributesW(updater_path.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        DeleteFileW(archive_path.c_str());
        MessageBoxW(owner,
            L"Файл обновления не найден. Переустановите приложение через install.bat.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return;
    }

    const DWORD process_id = GetCurrentProcessId();
    std::wstring parameters =
        L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
        quote_argument(updater_path) + L" -ArchivePath " +
        quote_argument(archive_path) + L" -AppPath " +
        quote_argument(app_path) + L" -ProcessId " +
        std::to_wstring(process_id);
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", L"powershell.exe",
        parameters.c_str(), nullptr, SW_HIDE);
    if (reinterpret_cast<INT_PTR>(result) <= 32)
    {
        DeleteFileW(archive_path.c_str());
        show_update_error(owner);
        return;
    }

    PostMessageW(owner, WM_CLOSE, 0, 0);
}

DWORD WINAPI update_thread(LPVOID parameter)
{
    check_updates(static_cast<HWND>(parameter));
    return 0;
}
}
#endif

void check_for_updates(HWND owner)
{
#if BONDERIUM_ENABLE_UPDATES
    HANDLE thread = CreateThread(nullptr, 0, update_thread, owner, 0, nullptr);
    if (thread == nullptr)
    {
        MessageBoxW(owner, L"Не удалось запустить проверку обновлений.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(thread);
#else
    (void)owner;
#endif
}
