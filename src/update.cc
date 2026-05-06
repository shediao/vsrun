#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "update.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

#pragma comment(lib, "winhttp.lib")

namespace {

// ---------------------------------------------------------------------------
// WinHTTP helpers
// ---------------------------------------------------------------------------
std::wstring to_utf16(std::string_view s) {
  if (s.empty()) {
    return {};
  }
  int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                   static_cast<int>(s.size()), nullptr, 0);
  if (needed <= 0) {
    return {};
  }
  std::wstring result(needed, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                      result.data(), needed);
  return result;
}

struct WinHttpHandle {
  HINTERNET handle{nullptr};
  ~WinHttpHandle() {
    if (handle) {
      WinHttpCloseHandle(handle);
    }
  }
  operator HINTERNET() const { return handle; }
  HINTERNET* operator&() { return &handle; }
};

// Perform a GET request, follow redirects, return response body as string.
std::string http_get(const std::wstring& url, const std::wstring& host) {
  // Split host and path
  std::wstring path = L"/";
  {
    // URL format: https://host/path...
    auto scheme_end = url.find(L"://");
    auto host_start = (scheme_end != std::wstring::npos) ? scheme_end + 3 : 0;
    auto path_start = url.find(L'/', host_start);
    if (path_start != std::wstring::npos) {
      path = url.substr(path_start);
    }
  }

  WinHttpHandle session{
      WinHttpOpen(L"vsrun-updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
  if (!session) {
    return {};
  }

  WinHttpHandle connect{
      WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)};
  if (!connect) {
    return {};
  }

  DWORD flags = WINHTTP_FLAG_SECURE;
  WinHttpHandle request{WinHttpOpenRequest(
      connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
  if (!request) {
    return {};
  }

  // GitHub API requires a User-Agent
  const wchar_t* headers =
      L"User-Agent: vsrun-updater\r\n"
      L"Accept: application/vnd.github+json\r\n";
  if (!WinHttpSendRequest(request, headers, static_cast<DWORD>(wcslen(headers)),
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
    return {};
  }

  if (!WinHttpReceiveResponse(request, nullptr)) {
    return {};
  }

  // Handle redirects (301, 302, 307, 308)
  DWORD status_code = 0;
  DWORD status_code_size = sizeof(status_code);
  WinHttpQueryHeaders(request,
                      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status_code,
                      &status_code_size, WINHTTP_NO_HEADER_INDEX);

  if (status_code == 301 || status_code == 302 || status_code == 307 ||
      status_code == 308) {
    // Get Location header
    DWORD loc_size = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION,
                        WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER,
                        &loc_size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
      std::wstring location((loc_size / sizeof(wchar_t)) + 1, L'\0');
      if (WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION,
                              WINHTTP_HEADER_NAME_BY_INDEX, location.data(),
                              &loc_size, WINHTTP_NO_HEADER_INDEX)) {
        location.resize(loc_size / sizeof(wchar_t));
        // Parse host from location
        auto scheme_end2 = location.find(L"://");
        auto host_start2 =
            (scheme_end2 != std::wstring::npos) ? scheme_end2 + 3 : 0;
        auto host_end2 = location.find(L'/', host_start2);
        std::wstring new_host =
            (host_end2 != std::wstring::npos)
                ? location.substr(host_start2, host_end2 - host_start2)
                : location.substr(host_start2);
        return http_get(location, new_host);
      }
    }
  }

  // Read response body
  std::string body;
  DWORD bytes_available = 0;
  while (WinHttpQueryDataAvailable(request, &bytes_available) &&
         bytes_available > 0) {
    std::vector<char> buffer(bytes_available);
    DWORD bytes_read = 0;
    if (WinHttpReadData(request, buffer.data(), bytes_available, &bytes_read)) {
      body.append(buffer.data(), bytes_read);
    }
  }

  return body;
}

// Download a file from `url` and save to `dest_path`.
bool download_file(const std::wstring& url,
                   const std::filesystem::path& dest_path) {
  // Parse host from URL
  auto scheme_end = url.find(L"://");
  auto host_start = (scheme_end != std::wstring::npos) ? scheme_end + 3 : 0;
  auto host_end = url.find(L'/', host_start);
  std::wstring host = (host_end != std::wstring::npos)
                          ? url.substr(host_start, host_end - host_start)
                          : url.substr(host_start);

  std::string data = http_get(url, host);
  if (data.empty()) {
    return false;
  }

  std::ofstream out(dest_path, std::ios::binary);
  if (!out) {
    return false;
  }
  out.write(data.data(), data.size());
  return out.good();
}

// ---------------------------------------------------------------------------
// Version comparison
// ---------------------------------------------------------------------------
// Compare semantic versions like "v1.2.3" or "1.2.3" or "1.2.3-4-gabcdef"
// Returns negative if a < b, 0 if equal, positive if a > b.
int compare_versions(std::string_view a, std::string_view b) {
  // Strip leading 'v'
  if (!a.empty() && (a[0] == 'v' || a[0] == 'V')) {
    a.remove_prefix(1);
  }
  if (!b.empty() && (b[0] == 'v' || b[0] == 'V')) {
    b.remove_prefix(1);
  }

  // Split into numeric parts
  auto next_number = [](std::string_view& s) -> int {
    int val = 0;
    while (!s.empty() && s[0] >= '0' && s[0] <= '9') {
      val = val * 10 + (s[0] - '0');
      s.remove_prefix(1);
    }
    return val;
  };

  for (int i = 0; i < 4; ++i) {
    int na = next_number(a);
    int nb = next_number(b);
    if (na != nb) {
      return na - nb;
    }

    // Skip non-digit separator ('.', '-', etc.)
    if (!a.empty() && (a[0] < '0' || a[0] > '9')) {
      a.remove_prefix(1);
    }
    if (!b.empty() && (b[0] < '0' || b[0] > '9')) {
      b.remove_prefix(1);
    }
  }

  return 0;
}

// ---------------------------------------------------------------------------
// Self-replace on Windows
// ---------------------------------------------------------------------------
// Because we cannot overwrite the running executable directly, we:
//   1. Download the new exe to %TEMP%\vsrun_new.exe
//   2. Write a batch script to %TEMP%\vsrun_update.bat
//   3. Launch the batch script detached
//   4. Exit the current process
// The batch script waits a moment, copies the new file over the old one,
// and cleans up.
void schedule_replace_and_exit(const std::filesystem::path& current_exe,
                               const std::filesystem::path& new_exe) {
  // Build the batch script
  auto temp_dir = std::filesystem::temp_directory_path();
  auto bat_path = temp_dir / "vsrun_update.bat";

  std::ofstream bat(bat_path);
  if (!bat) {
    std::cerr << "Failed to create update script." << std::endl;
    return;
  }

  bat << "@echo off\r\n";
  bat << ":loop\r\n";
  bat << "ping -n 2 127.0.0.1 >nul\r\n";
  auto old_exe_str = current_exe.string();
  auto new_exe_str = new_exe.string();
  auto old_bak = old_exe_str + ".old";

  bat << "move /Y \"" << old_exe_str << "\" \"" << old_bak << "\" 2>nul\r\n";
  bat << "if exist \"" << old_exe_str << "\" goto :loop\r\n";
  bat << "move /Y \"" << new_exe_str << "\" \"" << old_exe_str << "\"\r\n";
  bat << "if exist \"" << old_bak << "\" del \"" << old_bak << "\"\r\n";
  bat << "del \"%~f0\"\r\n";

  bat.close();

  // Launch the batch script detached
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi = {};

  std::wstring cmd_line = L"cmd.exe /c \"" + bat_path.wstring() + L"\"";

  if (CreateProcessW(nullptr, cmd_line.data(), nullptr, nullptr, FALSE,
                     CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr,
                     temp_dir.wstring().c_str(), &si, &pi)) {
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  } else {
    std::cerr << "Failed to launch update script." << std::endl;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool check_for_update(const std::string& current_version,
                      const std::string& repo_owner,
                      const std::string& repo_name,
                      const std::string& asset_name) {
  // 1. Query GitHub API for the latest release
  std::wstring api_url = to_utf16("https://api.github.com/repos/" + repo_owner +
                                  "/" + repo_name + "/releases/latest");
  std::wstring api_host = L"api.github.com";

  std::string response = http_get(api_url, api_host);
  if (response.empty()) {
    std::cerr
        << "Failed to query GitHub releases. Check your network connection."
        << std::endl;
    return false;
  }

  // 2. Parse JSON with nlohmann/json
  nlohmann::json release;
  try {
    release = nlohmann::json::parse(response);
  } catch (nlohmann::json::parse_error const& e) {
    std::cerr << "Failed to parse GitHub API response: " << e.what()
              << std::endl;
    return false;
  }

  // 3. Extract tag_name
  if (!release.contains("tag_name") || !release["tag_name"].is_string()) {
    std::cerr << "Failed to parse release tag from GitHub API response."
              << std::endl;
    return false;
  }
  std::string latest_tag = release["tag_name"].get<std::string>();

  // 4. Compare versions
  int cmp = compare_versions(latest_tag, current_version);
  if (cmp <= 0) {
    std::cout << "vsrun is up to date (" << current_version << ")."
              << std::endl;
    return false;
  }

  std::cout << "A new version is available: " << latest_tag
            << " (current: " << current_version << ")" << std::endl;

  // 5. Find the download URL for the requested asset
  std::string download_url;
  if (release.contains("assets") && release["assets"].is_array()) {
    for (auto const& asset : release["assets"]) {
      if (asset.contains("name") && asset["name"].is_string() &&
          asset["name"].get<std::string>() == asset_name &&
          asset.contains("browser_download_url") &&
          asset["browser_download_url"].is_string()) {
        download_url = asset["browser_download_url"].get<std::string>();
        break;
      }
    }
  }

  if (download_url.empty()) {
    std::cerr << "Asset '" << asset_name << "' not found in the latest release."
              << std::endl;
    return false;
  }

  std::cout << "Downloading " << asset_name << " ..." << std::endl;

  // 6. Download to a temp location
  auto temp_dir = std::filesystem::temp_directory_path();
  auto new_exe_path = temp_dir / ("vsrun_new_" + asset_name);

  if (!download_file(to_utf16(download_url), new_exe_path)) {
    std::cerr << "Failed to download the update." << std::endl;
    return false;
  }

  std::cout << "Downloaded successfully." << std::endl;

  // 7. Get current executable path
  wchar_t exe_path_buf[MAX_PATH];
  DWORD len = GetModuleFileNameW(nullptr, exe_path_buf, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    std::cerr << "Failed to determine current executable path." << std::endl;
    return false;
  }
  std::filesystem::path current_exe(exe_path_buf);

  // 8. Schedule replacement and exit
  std::cout << "Installing update... Please wait." << std::endl;
  schedule_replace_and_exit(current_exe, new_exe_path);

  // Exit the process to allow replacement
  std::exit(0);
}
