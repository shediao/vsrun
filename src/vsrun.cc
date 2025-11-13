
#define NOMINMAX
#include <comdef.h>
#include <comutil.h>
#include <windows.h>

#include "Setup.Configuration.h"

_COM_SMARTPTR_TYPEDEF(ISetupInstance, __uuidof(ISetupInstance));
_COM_SMARTPTR_TYPEDEF(ISetupInstance2, __uuidof(ISetupInstance2));
_COM_SMARTPTR_TYPEDEF(IEnumSetupInstances, __uuidof(IEnumSetupInstances));
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration, __uuidof(ISetupConfiguration));
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration2, __uuidof(ISetupConfiguration2));
_COM_SMARTPTR_TYPEDEF(ISetupHelper, __uuidof(ISetupHelper));
_COM_SMARTPTR_TYPEDEF(ISetupPackageReference, __uuidof(ISetupPackageReference));

#include <exception>
#include <filesystem>
#include <iostream>
#include <subprocess/subprocess.hpp>

inline std::wstring to_wstring(const std::string_view str,
                               const UINT from_codepage = CP_UTF8) {
  if (str.empty()) {
    return {};
  }
  int size_needed = MultiByteToWideChar(from_codepage, 0, str.data(),
                                        (int)str.size(), NULL, 0);
  if (size_needed <= 0) {
    throw std::runtime_error("MultiByteToWideChar error: " +
                             std::to_string(GetLastError()));
  }
  std::wstring wstr(size_needed, 0);
  MultiByteToWideChar(from_codepage, 0, str.data(), (int)str.size(), &wstr[0],
                      size_needed);
  return wstr;
}

// Helper function to convert a UTF-16 std::wstring to a UTF-8 std::string
inline std::string to_string(const std::wstring_view wstr,
                             const UINT to_codepage = CP_UTF8) {
  if (wstr.empty()) {
    return {};
  }
  int size_needed = WideCharToMultiByte(to_codepage, 0, wstr.data(),
                                        (int)wstr.size(), NULL, 0, NULL, NULL);
  if (size_needed <= 0) {
    throw std::runtime_error("WideCharToMultiByte error: " +
                             std::to_string(GetLastError()));
  }
  std::string str(size_needed, 0);
  WideCharToMultiByte(to_codepage, 0, wstr.data(), (int)wstr.size(), &str[0],
                      size_needed, NULL, NULL);
  return str;
}

class win32_exception : public std::exception {
 public:
  win32_exception(_In_ DWORD code, _In_z_ const char* what) noexcept
      : std::exception(what), m_code(code) {}

  win32_exception(_In_ const win32_exception& obj) noexcept
      : std::exception(obj) {
    m_code = obj.m_code;
  }

  DWORD code() const noexcept { return m_code; }

 private:
  DWORD m_code;
};

class CoInitializer {
 public:
  CoInitializer() {
    hr = ::CoInitialize(NULL);
    if (FAILED(hr)) {
      throw win32_exception(hr, "failed to initialize COM");
    }
  }

  ~CoInitializer() {
    if (SUCCEEDED(hr)) {
      ::CoUninitialize();
    }
  }

 private:
  HRESULT hr;
};

std::vector<std::filesystem::path> GetInstallationPaths(std::string version) {
  std::vector<std::filesystem::path> result;
  ISetupConfigurationPtr configuration;
  if (auto hr = configuration.CreateInstance(__uuidof(SetupConfiguration));
      FAILED(hr)) {
    throw win32_exception(hr, "failed to create query class");
  }

  ISetupConfiguration2Ptr configuration2(configuration);
  IEnumSetupInstancesPtr instances;
  if (auto hr = configuration2->EnumInstances(&instances); FAILED(hr)) {
    throw win32_exception(hr, "failed to query all instances");
  }

  ISetupHelperPtr helper(configuration2);

  uint64_t minVersion = 0;
  uint64_t maxVersion = std::numeric_limits<uint64_t>::max();
  if (version.empty()) {
    version = "[1.0,]";
  }
  auto wVersion = to_wstring(version);
  if (auto hr =
          helper->ParseVersionRange(wVersion.c_str(), &minVersion, &maxVersion);
      FAILED(hr)) {
    throw win32_exception(hr, "failed to parse version range");
  }

  auto lcid = ::GetUserDefaultLCID();
  ISetupInstancePtr instance;
  while (instances->Next(1, &instance, NULL) == S_OK) {
    ISetupInstance2Ptr instance2(instance);

    bstr_t displayName;
    if (FAILED(instance2->GetDisplayName(lcid, displayName.GetAddress()))) {
      continue;
    }
    bstr_t installationPath;
    if (FAILED(instance2->GetInstallationPath(installationPath.GetAddress()))) {
      continue;
    }

    bstr_t version;
    if (FAILED(instance2->GetInstallationVersion(version.GetAddress()))) {
      continue;
    }
    uint64_t versionNumber;
    helper->ParseVersion(version.GetBSTR(), &versionNumber);
    if ((versionNumber <= maxVersion) && (versionNumber >= minVersion)) {
      result.push_back(installationPath.GetBSTR());
    }
  }
  return result;
}

int main(int argc, char* argv[]) {
  CoInitializer comInitializer;
  std::filesystem::path VcDevCmdPath;
  try {
    auto allInstallationPaths = GetInstallationPaths("[15.0,)");
    if (allInstallationPaths.size() == 0) {
      std::cerr << "No installation found" << '\n';
      return 1;
    }
    std::filesystem::path installationPath = allInstallationPaths[0];
    if (!is_directory(installationPath)) {
      std::cerr << "installation not a directory: " << installationPath << '\n';
      return 1;
    }
    VcDevCmdPath = installationPath / "Common7" / "Tools" / "VsDevCmd.bat";
    if (!is_regular_file(VcDevCmdPath)) {
      std::cerr << "installation not found or not a bat file: " << VcDevCmdPath
                << '\n';
      return 1;
    }

  } catch (const win32_exception& e) {
  }

  for (int i = 1; i < argc; i++) {
    std::clog << "[DEBUG]: `" << argv[i] << "`" << '\n';
  }

  std::vector<std::string> args{"cmd",         "/c",
                                "call",        VcDevCmdPath.string(),
                                "-no_logo",    "-host_arch=amd64",
                                "-arch=amd64", ">nul&&"};
  if (argc > 1) {
    std::copy(argv + 1, argv + argc, std::back_inserter(args));
  } else {
    args.push_back("cmd");
  }
  std::copy(begin(args), end(args),
            std::ostream_iterator<std::string>(std::cout, " "));
  std::cout << '\n';
  auto result = subprocess::run(args);

  return result;
}
