
#define NOMINMAX

#include <comdef.h>
#include <comutil.h>
#include <fileapi.h>
#include <minwinbase.h>
#include <oaidl.h>
#include <windows.h>
#include <winerror.h>

#include <algorithm>
#include <argparse/argparse.hpp>
#include <exception>
#include <iostream>
#include <subprocess/subprocess.hpp>

#include "visualstudio.h"

int main(int argc, char* argv[]) {
  CoInitializer comInitializer;
  ISetupConfiguration2Ptr vs_setup_config([]() {
    ISetupConfigurationPtr configuration;
    if (auto hr = configuration.CreateInstance(__uuidof(SetupConfiguration));
        FAILED(hr)) {
      throw win32_exception(hr, "failed to create query class");
    }
    return configuration;
  }());
  ISetupHelperPtr helper(vs_setup_config);

  int debug_level = 0;
  std::string version = "[16.0,)";
  std::string product_id = "*";  // Microsoft.VisualStudio.Product.*
  std::string sort_by = "";
  std::string select_workload = "*";
  std::optional<bool> select_one = std::nullopt;

  argparse::ArgParser parser{
      "vs-install-dir",
      "find Visual Studio install directory based on version and product"};
  parser
      .add_option("v,version",
                  "A version range for instances to find. Example: "
                  "[17.0,18.0) will find versions 17.*.",
                  version)
      .checker(
          [&helper](std::string const& val) -> std::pair<bool, std::string> {
            auto wversion = to_wstring(val);
            uint64_t version_min, version_max;
            if (FAILED(helper->ParseVersionRange(wversion.c_str(), &version_min,
                                                 &version_max))) {
              return {false, "not a valid version range: " + val};
            }
            return {true, ""};
          });
  parser
      .add_option("product",
                  "One or more product IDs to find. Defaults to Community, "
                  "Professional, and Enterprise. Specify \"*\" by itself to "
                  "search all product instances installed",
                  product_id)
      .checker([](std::string const& val) { return check_product_id(val); });

  parser.add_option(
      "workload",
      "require workload, Microsoft.VisualStudio.Workload.NativeDesktop",
      select_workload);

  parser
      .add_option(
          "sort",
          "example `version:asc,product:Professional-Enterprise-Community`",
          sort_by)
      .checker([](std::string const& val) { return check_sort_by(val); });

  parser.add_flag("first", "select this first visualstudio", select_one);
  parser.add_negative_flag("last", "select this first visualstudio",
                           select_one);

  parser.add_alias("c,community", "product", "Community");
  parser.add_alias("p,professional", "product", "Professional");
  parser.add_alias("e,enterprise", "product", "Enterprise");
  parser.add_flag("verbose", "show verbose messages", debug_level);

  try {
    parser.parse(argc, argv);
  } catch (std::exception const& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  std::map<std::string, std::string> sort_by_map;
  if (!sort_by.empty()) {
    auto s1 = split(sort_by, ',', -1);
    for (auto const& s : s1) {
      auto s2 = split(s, ':', 1);
      sort_by_map[s2[0]] = s2[1];
    }
  }
  auto all_match_visualstudios =
      GetMatchedVisualStudios(vs_setup_config, version, product_id,
                              select_workload, sort_by_map, debug_level);

  if (all_match_visualstudios.empty()) {
    return EXIT_FAILURE;
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
