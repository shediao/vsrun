
#define NOMINMAX

#include <comdef.h>
#include <comutil.h>
#include <fileapi.h>
#include <minwinbase.h>
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

std::wstring ToISO8601(const FILETIME* ft) {
  FILETIME lft;
  if (!::FileTimeToLocalFileTime(ft, &lft)) {
    return L"";
  }

  SYSTEMTIME st;
  if (!::FileTimeToSystemTime(&lft, &st)) {
    return L"";
  }

  wchar_t wz[20] = {L'\0'};
  auto cch = ::swprintf_s(wz, L"%04u-%02u-%02u %02u:%02u:%02u", st.wYear,
                          st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  return std::wstring(wz);
}

struct VisualStudio {
  uint64_t version_;
  FILETIME install_datetime_;
  std::wstring install_version_;
  std::wstring install_path_;
  std::wstring display_name_;
  std::wstring product_id_;
  bool is_complete_;
  bool is_prerelease_;
  std::vector<std::wstring> workloads_;
  bool is_complete() const { return is_complete_; }
  bool is_prerelease() const { return is_prerelease_; }
  bool is_product_match(std::wstring const& product_pattern) const {
    if (product_pattern == L"*") {
      return true;
    }
    std::wstring ipattern;
    std::transform(product_pattern.begin(), product_pattern.end(),
                   std::back_inserter(ipattern),
                   [](wchar_t c) { return std::tolower(c); });
    std::wstring iproduct_id;
    std::transform(product_id_.begin(), product_id_.end(),
                   std::back_inserter(iproduct_id),
                   [](wchar_t c) { return std::tolower(c); });
    if (!ipattern.starts_with(L"microsoft.visualstudio.product.")) {
      ipattern = L"microsoft.visualstudio.product." + ipattern;
    }
    return ipattern == iproduct_id;
  }
  bool is_workload_match(std::wstring const& workload_pattern) const {
    if (workload_pattern == L"*" && !workloads_.empty()) {
      return true;
    }

    for (auto const& workload : workloads_) {
      if (0 == _wcsicmp(workload_pattern.c_str(), workload.c_str())) {
        return true;
      }
    }
    return false;
  }
  bool is_version_match(uint64_t min, uint64_t max) const {
    return version_ >= min && version_ <= max;
  };
};

std::wostream& operator<<(std::wostream& out, VisualStudio const& vs) {
  out << L"Version: " << vs.install_version_ << " (" << vs.version_ << ")"
      << L'\n';
  out << L"Date: " << ToISO8601(&vs.install_datetime_) << L'\n';
  out << L"Install: " << vs.install_path_ << L'\n';
  out << L"Display: " << vs.display_name_ << L'\n';
  out << L"Product: " << vs.product_id_ << L'\n';
  out << L"Complete: " << std::boolalpha << vs.is_complete_ << L'\n';
  out << L"Prerelease: " << vs.is_prerelease_ << L'\n';
  // out << L"Workloads: ";
  // std::copy(vs.workloads_.begin(), vs.workloads_.end(),
  //           std::ostream_iterator<std::wstring, wchar_t>(out, L", "));
  // out << L'\n';
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

    FILETIME install_time;
    if (FAILED(instance2->GetInstallDate(&install_time))) {
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

    LPSAFEARRAY psa = nullptr;
    if (FAILED(instance2->GetPackages(&psa))) {
      continue;
    }
    std::unique_ptr<LPSAFEARRAY, decltype([](LPSAFEARRAY* ppsa) {
                      if (ppsa && *ppsa) {
                        if ((*ppsa)->cLocks) {
                          ::SafeArrayUnlock(*ppsa);
                        }
                        ::SafeArrayDestroy(*ppsa);
                      }
                    })>
        psa_guard(&psa);

    std::vector<std::wstring> workloads;

    ::SafeArrayLock(psa);

    auto begin = reinterpret_cast<ISetupPackageReferencePtr*>(psa->pvData);
    auto end = begin + psa->rgsabound[0].cElements;
    std::vector<ISetupPackageReferencePtr> all_packages{begin, end};
    for (auto package_ptr : all_packages) {
      bstr_t type;
      if (FAILED(package_ptr->GetType(type.GetAddress()))) {
        continue;
      }
      if (0 != _wcsicmp(L"Workload", type.GetBSTR())) {
        continue;
      }
      bstr_t id;
      if (FAILED(package_ptr->GetId(id.GetAddress()))) {
        continue;
      }
      workloads.push_back(id.GetBSTR());
    }

    result.push_back({.version_ = version,
                      .install_datetime_ = install_time,
                      .install_version_ = install_version.GetBSTR(),
                      .install_path_ = install_path.GetBSTR(),
                      .display_name_ = display_name.GetBSTR(),
                      .product_id_ = product_id.GetBSTR(),
                      .is_complete_ = (is_complete != VARIANT_FALSE),
                      .is_prerelease_ = (is_prerelease != VARIANT_FALSE),
                      .workloads_ = workloads});
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

  std::string version = "[16.0,)";
  std::string product_id = "*";  // Microsoft.VisualStudio.Product.*
  std::string sort_by = "";
  std::string select_workload = "*";
  std::optional<bool> select_one = std::nullopt;

  argparse::ArgParser parser{
      "vs-install-dir",
      R"(call C:\*\Microsoft Visual Studio\*\Common7\Tools\VsDevCmd.bat && %*)"};
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

  parser.add_option(
      "workload",
      "require workload, Microsoft.VisualStudio.Workload.NativeDesktop",
      select_workload);

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
          if (sort2[0] == "version" || sort2[0] == "date" ||
              sort2[0] == "time") {
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

  parser.add_flag("first", "select this first visualstudio", select_one);
  parser.add_negative_flag("last", "select this first visualstudio",
                           select_one);

  parser.add_alias("c,community", "product", "Community");
  parser.add_alias("p,professional", "product", "Professional");
  parser.add_alias("e,enterprise", "product", "Enterprise");

  try {
    parser.parse(argc, argv);
  } catch (std::exception const& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
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
    return EXIT_FAILURE;
  }

  auto all_vs = GetAllVisualStudio(vs_setup_config);
  if (all_vs.empty()) {
    return EXIT_FAILURE;
  }
  if (!std::any_of(begin(all_vs), end(all_vs),
                   [](auto const& vs) { return vs.is_complete(); })) {
    return EXIT_FAILURE;
  }

  std::vector<VisualStudio> all_match_visualstudios;
  for (auto vs : all_vs) {
    if (vs.is_complete_ && vs.is_version_match(version_min, version_max) &&
        vs.is_product_match(to_wstring(product_id)) &&
        vs.is_workload_match(to_wstring(select_workload))) {
      all_match_visualstudios.push_back(vs);
    }
  }
  if (all_match_visualstudios.size() == 0) {
    return EXIT_FAILURE;
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
      } else if (sort_name == "date" || sort_name == "time") {
        if (sort_func == "asc") {
          sort_functions.push_back([](VisualStudio const& a,
                                      VisualStudio const& b) {
            return CompareFileTime(&a.install_datetime_, &b.install_datetime_) <
                   0;
          });
        } else {
          sort_functions.push_back([](VisualStudio const& a,
                                      VisualStudio const& b) {
            return CompareFileTime(&a.install_datetime_, &b.install_datetime_) >
                   0;
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

  if (select_one.has_value()) {
    std::wcout << (select_one.value()
                       ? all_match_visualstudios.front().install_path_
                       : all_match_visualstudios.back().install_path_)
               << L'\n';
  } else {
    for (auto const& vs : all_match_visualstudios) {
      std::wcout << vs.install_path_ << L'\n';
    }
  }

  return EXIT_SUCCESS;
}
