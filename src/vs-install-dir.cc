
#define NOMINMAX

#include <comdef.h>
#include <comutil.h>
#include <fileapi.h>
#include <minwinbase.h>
#include <oaidl.h>
#include <windows.h>
#include <winerror.h>

#include <argparse/argparse.hpp>
#include <exception>
#include <iostream>
#include <subprocess/subprocess.hpp>

#include "visualstudio.h"

int wmain(int argc, wchar_t* argv[]) {
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
  std::string version_range = "[16.0,)";
  std::string product_id = "*";  // Microsoft.VisualStudio.Product.*
  std::string sort_by = "";
  std::string select_workload = "*";
  std::optional<bool> select_one = std::nullopt;
  bool print_version{false};
  bool print_bash_completion{false};
  bool print_zsh_completion{false};
  bool print_fish_completion{false};

  argparse::ArgParser parser{
      "vs-install-dir",
      "find Visual Studio install directory based on version and product"};
  parser
      .add_option("v,version",
                  "A version range for instances to find. Example: "
                  "[17.0,18.0) will find versions 17.*.",
                  version_range)
      .validator(
          [&helper](std::string const& val) -> std::pair<bool, std::string> {
            auto wversion = to_version_range(to_wstring(val));
            uint64_t version_min, version_max;
            if (FAILED(helper->ParseVersionRange(wversion.c_str(), &version_min,
                                                 &version_max))) {
              return {false, "not a valid version range: " + val};
            }
            return {true, ""};
          });
  parser
      .add_option("product",
                  "One or more product IDs to find. Defaults to \"*\" to "
                  "search all product instances installed. "
                  "Specify product IDs like Community, Professional, or "
                  "Enterprise to narrow down the search.",
                  product_id)
      .validator([](std::string const& val) { return check_product_id(val); });

  parser.add_option(
      "workload",
      "Require a specific workload to be installed, e.g., "
      "Microsoft.VisualStudio.Workload.NativeDesktop for native desktop "
      "development.",
      select_workload);

  parser
      .add_option(
          "sort",
          "Sort the matching instances by the specified criteria. "
          "Example: `version:asc,product:Professional-Enterprise-Community` "
          "sorts by version ascending, then by product in the given order.",
          sort_by)
      .validator([](std::string const& val) { return check_sort_by(val); });

  parser.add_flag("first", "Select the first matching Visual Studio instance.",
                  select_one);
  parser.add_negative_flag("last",
                           "Select the last matching Visual Studio "
                           "instance.",
                           select_one);

  parser.add_alias("c,community", "product", "Community");
  parser.add_alias("p,professional", "product", "Professional");
  parser.add_alias("e,enterprise", "product", "Enterprise");
  parser.add_flag("verbose", "show verbose messages", debug_level);

  parser.add_flag("V", "Print version", print_version)
      .callback([&parser](bool v) {
        if (v) {
          std::cout << "vs-install-dir " << GIT_DESCRIBE << std::endl;
          std::exit(0);
        }
      });

  parser
      .add_flag("print-bash-complete", "Print bash completion script",
                print_bash_completion)
      .callback([&parser](bool v) {
        if (v) {
          parser.print_bash_complete(std::cout);
          std::exit(0);
        }
      })
      .hidden();
  parser
      .add_flag("print-zsh-complete", "Print zsh completion script",
                print_zsh_completion)
      .callback([&parser](bool v) {
        if (v) {
          parser.print_zsh_complete(std::cout);
          std::exit(0);
        }
      })
      .hidden();
  parser
      .add_flag("print-fish-complete", "Print fish completion script",
                print_fish_completion)
      .callback([&parser](bool v) {
        if (v) {
          parser.print_fish_complete(std::cout);
          std::exit(0);
        }
      })
      .hidden();

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
  auto all_match_visualstudios = GetMatchedVisualStudios(
      vs_setup_config, to_version_range(version_range), product_id,
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
