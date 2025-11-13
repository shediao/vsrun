
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

int main(int argc, char* argv[]) {
  CoInitializer comInitializer;

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
  std::filesystem::path newInstallationPath;
  uint64_t newestVersion{0};

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
    if (versionNumber > newestVersion) {
      versionNumber = newestVersion;
      newInstallationPath = installationPath.GetBSTR();
    }
  }

  if (newInstallationPath.empty()) {
    std::cerr << "No instances found" << std::endl;
    return 1;
  }
  for (int i = 1; i < argc; i++) {
    std::cout << i << ": `" << argv[i] << "`" << '\n';
  }

  std::vector<std::string> args{
      "cmd",
      "/c",
      "call",
      (newInstallationPath / "Common7" / "Tools" / "VsDevCmd.bat").string(),
      "-no_logo",
      "-host_arch=amd64",
      "-arch=amd64",
      ">nul&&"};
  if (argc > 1) {
    std::copy(argv + 1, argv + argc, std::back_inserter(args));
  } else {
    args.push_back("cmd");
  }
  auto result = subprocess::run(args);

  return result;
}
