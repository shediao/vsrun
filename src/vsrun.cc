
#define NOMINMAX

#include <comdef.h>
#include <comutil.h>
#include <oaidl.h>
#include <windows.h>
#include <winerror.h>

#include <algorithm>

#include "Setup.Configuration.h"

_COM_SMARTPTR_TYPEDEF(ISetupInstance, __uuidof(ISetupInstance));
_COM_SMARTPTR_TYPEDEF(ISetupInstance2, __uuidof(ISetupInstance2));
_COM_SMARTPTR_TYPEDEF(IEnumSetupInstances, __uuidof(IEnumSetupInstances));
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration, __uuidof(ISetupConfiguration));
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration2, __uuidof(ISetupConfiguration2));
_COM_SMARTPTR_TYPEDEF(ISetupHelper, __uuidof(ISetupHelper));
_COM_SMARTPTR_TYPEDEF(ISetupPackageReference, __uuidof(ISetupPackageReference));
_COM_SMARTPTR_TYPEDEF(ISetupInstanceCatalog, __uuidof(ISetupInstanceCatalog));

#include <argparse/argparse.hpp>
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

struct VisualStudio {
  uint64_t version_;
  std::wstring install_version_;
  std::wstring install_path_;
  std::wstring display_name_;
  std::wstring product_id_;
  bool is_complete_;
  bool is_prerelease_;
  bool is_complete() const { return is_complete_; }
  bool is_prerelease() const { return is_prerelease_; }
  bool is_product_match(std::wstring const& product_pattern) const {
    if (product_pattern == L"*") {
      return true;
    }
    return (L"Microsoft.VisualStudio.Product." + product_pattern) ==
           product_id_;
  }
  bool is_workload_match(std::wstring const& workload_pattern) const {
    return true;
  }
  bool is_version_match(uint64_t min, uint64_t max) const {
    return version_ >= min && version_ <= max;
  };
};

std::wostream& operator<<(std::wostream& out, VisualStudio const& vs) {
  out << L"version: " << vs.install_version_ << " (" << vs.version_ << ")"
      << L'\n';
  out << L"install: " << vs.install_path_ << L'\n';
  out << L"display: " << vs.display_name_ << L'\n';
  out << L"product: " << vs.product_id_ << L'\n';
  out << L"complete: " << std::boolalpha << vs.is_complete_ << L'\n';
  out << L"prerelease: " << vs.is_prerelease_ << L'\n';
  return out;
}

std::vector<VisualStudio> GetAllVisualStudio(ISetupConfiguration2Ptr& config) {
  std::vector<VisualStudio> result;
  IEnumSetupInstancesPtr instances;
  if (auto hr = config->EnumInstances(&instances); FAILED(hr)) {
    throw win32_exception(hr, "failed to query all instances");
  }

  ISetupHelperPtr helper(config);

  auto lcid = ::GetUserDefaultLCID();
  ISetupInstancePtr instance;
  while (instances->Next(1, &instance, NULL) == S_OK) {
    ISetupInstance2Ptr instance2(instance);

    bstr_t display_name;
    if (FAILED(instance2->GetDisplayName(lcid, display_name.GetAddress()))) {
      continue;
    }
    bstr_t install_path;
    if (FAILED(instance2->GetInstallationPath(install_path.GetAddress()))) {
      continue;
    }

    bstr_t install_version;
    if (FAILED(
            instance2->GetInstallationVersion(install_version.GetAddress()))) {
      continue;
    }
    uint64_t version;
    helper->ParseVersion(install_version.GetBSTR(), &version);

    ISetupPackageReferencePtr package;
    if (FAILED(instance2->GetProduct(&package)) || !package) {
      continue;
    }
    bstr_t product_id;
    if (FAILED(package->GetId(product_id.GetAddress()))) {
      continue;
    }

    VARIANT_BOOL is_complete{VARIANT_FALSE};
    instance2->IsComplete(&is_complete);

    VARIANT_BOOL is_prerelease{VARIANT_FALSE};
    ISetupInstanceCatalogPtr catalog;
    if (SUCCEEDED(instance2->QueryInterface(&catalog)) && !!catalog) {
      if (SUCCEEDED(catalog->IsPrerelease(&is_prerelease))) {
      }
    }

    result.push_back({.version_ = version,
                      .install_version_ = install_version.GetBSTR(),
                      .install_path_ = install_path.GetBSTR(),
                      .display_name_ = display_name.GetBSTR(),
                      .product_id_ = product_id.GetBSTR(),
                      .is_complete_ = (is_complete != VARIANT_FALSE),
                      .is_prerelease_ = (is_prerelease != VARIANT_FALSE)});
  }
  return result;
}

std::string quote_path_if_needed(std::string&& p) {
  if (std::find(p.begin(), p.end(), ' ') != p.end()) {
    p.insert(p.begin(), '"');
    p.push_back('"');
  }
  return p;
}

int main(int argc, char* argv[]) {
  CoInitializer comInitializer;

#if defined(__aarch64__) || defined(_M_ARM64)
  std::string arch = "arm64";
  std::string host_arch = "arm64";
#else
  std::string arch = "x64";
  std::string host_arch = "x64";
#endif

  std::string version = "[16.0,)";
  std::string product_id = "*";  // Microsoft.VisualStudio.Product.*
  std::vector<std::string> user_cmds;
  int debug_level = 0;
  bool list_visual_studio = false;
  std::string sort_by = "";

  argparse::ArgParser parser{
      "vsrun",
      R"(call C:\*\Microsoft Visual Studio\*\Common7\Tools\VsDevCmd.bat && %*)"};
  parser.add_option("arch", "target cpu arch", arch)
      .choices({"x86", "x64", "arm64"});
  parser.add_option("host-arch", "host cpu arch", host_arch)
      .choices({"x86", "x64", "arm64"});
  parser.add_option("version",
                    "A version range for instances to find. Example: "
                    "[17.0,18.0) will find versions 17.*.",
                    version);
  parser
      .add_option("product",
                  "One or more product IDs to find. Defaults to Community, "
                  "Professional, and Enterprise. Specify \"*\" by itself to "
                  "search all product instances installed",
                  product_id)
      .choices({"Professional", "Enterprise", "Community", "*"});

  parser
      .add_option(
          "sort",
          "example `version:asc,product:Professional-Enterprise-Community`",
          sort_by)
      .checker([](std::string const& val) {
        auto sort1 = argparse::detail::split(val, ',', -1);
        auto sort_by_asc_desc = [](std::string const& val) {
          if (val == "asc" || val == "desc") {
            return std::pair<bool, std::string>{true, ""};
          } else {
            return std::pair<bool, std::string>{false, "asc/desc"};
          }
        };
        auto sort_by_product_checker = [](std::string const& val) {
          std::pair<bool, std::string> err_return = {
              false, "Professional,Enterprise,Community"};
          auto s = argparse::detail::split(val, '-', -1);
          if (s.size() != 3) {
            return err_return;
          }
          if (std::find(s.begin(), s.end(), "Professional") == s.end()) {
            return err_return;
          }
          if (std::find(s.begin(), s.end(), "Enterprise") == s.end()) {
            return err_return;
          }
          if (std::find(s.begin(), s.end(), "Community") == s.end()) {
            return err_return;
          }
          return std::pair<bool, std::string>{true, ""};
        };

        for (auto const& s1 : sort1) {
          auto sort2 = argparse::detail::split(s1, ':', -1);
          if (sort2.size() != 2) {
            return std::pair<bool, std::string>{false, s1};
          }
          if (sort2[0] == "version" || sort2[0] == "time") {
            if (auto [ret, msg] = sort_by_asc_desc(sort2[1]); ret) {
              continue;
            }
          } else if (sort2[0] == "product") {
            if (auto [ret, msg] = sort_by_product_checker(sort2[1]); ret) {
              continue;
            }
          }
          return std::pair<bool, std::string>{false, s1};
        }
        return std::pair<bool, std::string>{true, ""};
      });

  parser.add_alias("c,community", "product", "Community");
  parser.add_alias("p,professional", "product", "Professional");
  parser.add_alias("e,enterprise", "product", "Enterprise");

  parser.add_flag("list", "list all match visual studio infomation",
                  list_visual_studio);
  parser.add_flag("v,verbose", "show verbose messages", debug_level);

  parser.add_positional("command", "run command in vs dev environment",
                        user_cmds);

  try {
    parser.parse(argc, argv);
  } catch (std::exception const& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }

  ISetupConfiguration2Ptr vs_setup_config([]() {
    ISetupConfigurationPtr configuration;
    if (auto hr = configuration.CreateInstance(__uuidof(SetupConfiguration));
        FAILED(hr)) {
      throw win32_exception(hr, "failed to create query class");
    }
    return configuration;
  }());

  ISetupHelperPtr helper(vs_setup_config);
  uint64_t version_min, version_max;
  auto wversion = to_wstring(version);
  if (S_OK !=
      helper->ParseVersionRange(wversion.c_str(), &version_min, &version_max)) {
    std::cerr << version << '\n';
    return 1;
  }

  auto all_vs = GetAllVisualStudio(vs_setup_config);
  if (all_vs.empty()) {
    std::cerr << "No VisualStudio installation found" << '\n';
    return 1;
  }
  if (!std::any_of(begin(all_vs), end(all_vs),
                   [](auto const& vs) { return vs.is_complete(); })) {
    std::cerr << "No Completed VisualStudio installation found" << '\n';
    return 1;
  }

  std::vector<VisualStudio> all_match_visualstudios;
  for (auto vs : all_vs) {
    if (vs.is_complete_ && vs.is_version_match(version_min, version_max) &&
        vs.is_product_match(to_wstring(product_id))) {
      all_match_visualstudios.push_back(vs);
    }
  }
  if (all_match_visualstudios.size() == 0) {
    std::cerr << "Not Found ViusalStudio "
              << (product_id == "*" ? "Professional|Enterprise|Community"
                                    : product_id)
              << " " << version << " Installation" << '\n';
    return 1;
  }
  if (!sort_by.empty()) {
    std::vector<
        std::function<bool(VisualStudio const& a, VisualStudio const& b)>>
        sort_functions;
    auto sort1 = argparse::detail::split(sort_by, ',', -1);
    for (auto const& s1 : sort1) {
      auto sort2 = argparse::detail::split(s1, ':', -1);
      auto sort_name = sort2[0];
      auto sort_func = sort2[1];
      if (sort_name == "version") {
        if (sort_func == "asc") {
          sort_functions.push_back(
              [](VisualStudio const& a, VisualStudio const& b) {
                return a.version_ < b.version_;
              });
        } else {
          sort_functions.push_back(
              [](VisualStudio const& a, VisualStudio const& b) {
                return b.version_ < a.version_;
              });
        }
      } else if (sort_name == "product") {
        auto s = argparse::detail::split(sort_func, '-', -1);
        for (auto& i : s) {
          i = "Microsoft.VisualStudio.Product." + i;
        }
        sort_functions.push_back([s](VisualStudio const& a,
                                     VisualStudio const& b) {
          auto ai = std::find(s.cbegin(), s.cend(), to_string(a.product_id_));
          auto bi = std::find(s.cbegin(), s.cend(), to_string(b.product_id_));
          bool ret = (ai - s.begin()) < (bi - s.begin());
          return ret;
        });
      }
    }

    std::sort(all_match_visualstudios.begin(), all_match_visualstudios.end(),
              [&sort_functions](VisualStudio const& a, VisualStudio const& b) {
                for (auto& less_func : sort_functions) {
                  if (less_func(a, b)) {
                    return true;
                  }
                  if (less_func(b, a)) {
                    return false;
                  }
                }
                return false;
              });
  }

  if (list_visual_studio) {
    for (auto const& vs : all_match_visualstudios) {
      std::wcout << vs << '\n';
    }
  }

  std::filesystem::path installationPath =
      all_match_visualstudios[0].install_path_;
  if (!is_directory(installationPath)) {
    std::cerr << "installation not a directory: " << installationPath << '\n';
    return 1;
  }
  std::filesystem::path VcDevCmdPath =
      installationPath / "Common7" / "Tools" / "VsDevCmd.bat";
  if (!is_regular_file(VcDevCmdPath)) {
    std::cerr << VcDevCmdPath.string()
              << " not exists or not a bat file: " << VcDevCmdPath << '\n';
    return 1;
  }

  std::vector<std::string> args{"cmd.exe",
                                "/d",
                                "/c",
                                "call",
                                quote_path_if_needed(VcDevCmdPath.string()),
                                "-no_logo",
                                "-host_arch=" + host_arch,
                                "-arch=" + arch,
                                ">nul&&"};
  if (!user_cmds.empty()) {
    args.insert(args.end(), user_cmds.begin(), user_cmds.end());
    if (debug_level >= 1) {
      std::copy(begin(args), end(args),
                std::ostream_iterator<std::string>(std::cerr, " "));
      std::cerr << '\n';
    }

    return subprocess::run(args);
  }
  return 0;
}
