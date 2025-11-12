
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

#include <filesystem>
#include <iostream>
#include <subprocess/subprocess.hpp>

int main(int argc, char* argv[]) {
  CoInitializeEx(NULL, COINIT_MULTITHREADED);

  ISetupConfigurationPtr configuration;
  if (FAILED(configuration.CreateInstance(__uuidof(SetupConfiguration)))) {
    std::cerr << "Failed to create instance of SetupConfiguration" << std::endl;
    CoUninitialize();
    return 1;
  }

  ISetupConfiguration2Ptr configuration2(configuration);
  IEnumSetupInstancesPtr instances;
  if (FAILED(configuration2->EnumInstances(&instances))) {
    std::cerr << "Failed to enumerate instances" << std::endl;
    CoUninitialize();
    return 1;
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
    CoUninitialize();
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

  CoUninitialize();
  return result;
}
