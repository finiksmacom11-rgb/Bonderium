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
constexpr wchar_t release_path[] = L"/repos/finiksmacom11-rgb/Bonderium/releases/latest";
constexpr wchar_t download_host[] = L"github.com";
constexpr wchar_t download_path[] =
    L"/finiksmacom11-rgb/Bonderium/releases/latest/download/Bonderium-windows-x64.zip";
constexpr std::size_t maximum_download_size = 100U * 1024U * 1024U;

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

bool parse_version(std::string_view text, std::array<unsigned long long, 3>& parts)
{
    if (text.size() < 2 || text[0] != 'v')
    {
        return false;
    }

    text.remove_prefix(1);
    for (std::size_t index = 0; index < parts.size(); ++index)
    {
        const std::size_t separator = text.find('.');
        const std::string_view component =
            index == parts.size() - 1 ? text : text.substr(0, separator);

        if (component.empty() ||
            (index != parts.size() - 1 && separator == std::string_view::npos))
        {
            return false;
        }

        const auto result = std::from_chars(
            component.data(), component.data() + component.size(), parts[index]);
        if (result.ec != std::errc{} || result.ptr != component.data() + component.size())
        {
            return false;
        }

        if (index != parts.size() - 1)
        {
            text.remove_prefix(separator + 1);
        }
        else if (separator != std::string_view::npos)
        {
            return false;
        }
    }

    return true;
}

bool is_newer_version(std::string_view latest)
{
    std::array<unsigned long long, 3> latest_parts{};
    std::array<unsigned long long, 3> current_parts{};
    if (!parse_version(latest, latest_parts))
    {
        return false;
    }

    std::string current = BONDERIUM_VERSION;
    current.insert(current.begin(), 'v');
    if (!parse_version(current, current_parts))
    {
        return false;
    }

    return latest_parts > current_parts;
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

bool get_latest_release(std::string& version)
{
    std::string response;
    if (!get_https(release_host, release_path, response))
    {
        return false;
    }

    constexpr std::string_view key = "\"tag_name\"";
    const std::size_t key_position = response.find(key);
    if (key_position == std::string::npos)
    {
        return false;
    }

    const std::size_t colon_position = response.find(':', key_position + key.size());
    if (colon_position == std::string::npos)
    {
        return false;
    }
    const std::size_t value_start = response.find('"', colon_position + 1);
    if (value_start == std::string::npos)
    {
        return false;
    }
    const std::size_t value_end = response.find('"', value_start + 1);
    if (value_end == std::string::npos)
    {
        return false;
    }

    version.assign(response, value_start + 1, value_end - value_start - 1);
    std::array<unsigned long long, 3> ignored{};
    return parse_version(version, ignored);
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

bool download_update(std::wstring& archive_path)
{
    std::string response;
    if (!get_https(download_host, download_path, response) || response.empty())
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
    std::string latest_version;
    if (!get_latest_release(latest_version))
    {
        return;
    }

    if (!IsWindow(owner) || !is_newer_version(latest_version))
    {
        return;
    }

    const std::wstring message =
        L"Доступна новая версия Bonderium " +
        std::wstring(latest_version.begin(), latest_version.end()) +
        L". Установить её сейчас?";
    if (MessageBoxW(owner, message.c_str(), L"Обновление Bonderium",
            MB_YESNO | MB_ICONINFORMATION) != IDYES)
    {
        return;
    }

    std::wstring archive_path;
    if (!download_update(archive_path))
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
