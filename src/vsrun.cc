
#include <comdef.h>
#include <comutil.h>
#include <fileapi.h>
#include <minwinbase.h>
#include <oaidl.h>
#include <windows.h>
#include <winerror.h>

#include <algorithm>
#include <argparse/argparse.hpp>
#include <environment/environment.hpp>
#include <exception>
#include <filesystem>
#include <iostream>
#include <subprocess/subprocess.hpp>

#include "update.h"
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

#if defined(__aarch64__) || defined(_M_ARM64)
  std::string arch = "arm64";
  std::string host_arch = "arm64";
#else
  std::string arch = "x64";
  std::string host_arch = "x64";
#endif

  std::string version_range = "[16.0,)";
  std::string product_id = "*";  // Microsoft.VisualStudio.Product.*
  std::vector<std::string> user_cmds;
  int debug_level = 0;
  bool list_visual_studio = false;
  bool check_installed_or_not = false;
  std::string sort_by = "";
  bool select_the_first_one{true};
  std::string select_workload = "*";

  bool ignore_environment = false;

  std::string workdir;
  std::vector<std::string> uset_env_names;
  std::vector<std::pair<std::string, std::string>> set_env_vars;

  bool print_version{false};
  bool print_bash_completion{false};
  bool print_zsh_completion{false};
  bool print_fish_completion{false};
  bool check_update{false};

  argparse::ArgParser parser{
      "vsrun",
      R"(call C:\*\Microsoft Visual Studio\*\Common7\Tools\VsDevCmd.bat && %*)"};
  parser.add_option("arch", "target cpu arch", arch)
      .choices({"x86", "x64", "arm64"});
  parser.add_option("host-arch", "host cpu arch", host_arch)
      .choices({"x86", "x64", "arm64"});
  parser
      .add_option("v,version",
                  "A version range for instances to find. Example: "
                  "[17.0,18.0) will find versions 17.*.",
                  version_range)
      .validator(
          [&helper](std::string const& val) -> std::pair<bool, std::string> {
            if (val.empty()) {
              return {false, "version range is empty."};
            }
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

  parser
      .add_option("C",
                  "Change to the specified working directory before "
                  "running the command.",
                  workdir)
      .value_placeholder("workdir")
      .validator([](std::string const& dir) {
        if (std::filesystem::is_directory(dir)) {
          return std::pair<bool, std::string>{true, ""};
        } else {
          return std::pair<bool, std::string>{false, dir + " not a directory"};
        }
      });

  parser
      .add_option("u",
                  "Unset the specified environment variable(s) before "
                  "running the command.",
                  uset_env_names)
      .value_placeholder("name")
      .validator([](std::string const& name) {
        if (std::find(name.begin(), name.end(), '=') != name.end()) {
          return std::pair<bool, std::string>{false, name + " contain '='"};
        }
        return std::pair<bool, std::string>{true, ""};
      })
      .callback([&set_env_vars](auto const& values) {
        if (values.empty()) {
          return;
        }
        if (auto it = std::find_if(set_env_vars.begin(), set_env_vars.end(),
                                   [&values](auto const& item) {
                                     return item.first == values.back();
                                   });
            it != set_env_vars.end()) {
          set_env_vars.erase(it);
        }
      });

  parser
      .add_option("E",
                  "Set environment variable(s) for the command. "
                  "Format: key=value. Can be specified multiple times.",
                  set_env_vars, '=')
      .value_placeholder("key=value")
      .validator([](std::string const& kv) {
        auto eq_pos = kv.find('=');
        if (eq_pos == std::string::npos) {
          return std::pair<bool, std::string>{false, kv + " not key=value"};
        }
        if (eq_pos == 0) {
          return std::pair<bool, std::string>{
              false, "invalid key=value: " + kv + " (empty key)"};
        }
        return std::pair<bool, std::string>{true, ""};
      })
      .callback([&uset_env_names](auto const& values) {
        if (values.empty()) {
          return;
        }
        if (auto it = std::find(uset_env_names.begin(), uset_env_names.end(),
                                values.back().first);
            it != uset_env_names.end()) {
          uset_env_names.erase(it);
        }
      });

  parser.add_alias("c,community", "product", "Community");
  parser.add_alias("p,professional", "product", "Professional");
  parser.add_alias("e,enterprise", "product", "Enterprise");

  parser.add_flag("first", "select the first one to run(Default)",
                  select_the_first_one);
  parser.add_negative_flag("last", "select the last one to run",
                           select_the_first_one);

  parser.add_flag("list",
                  "List all matching Visual Studio instances with "
                  "detailed information.",
                  list_visual_studio);
  parser.add_flag("verbose", "show verbose messages", debug_level);

  parser.add_flag("V", "Print version", print_version)
      .callback([&parser](bool v) {
        if (v) {
          std::cout << "vsrun " << GIT_DESCRIBE << std::endl;
          std::exit(0);
        }
      });

  parser
      .add_flag("U,update",
                "Check for a newer version on GitHub and update if available.",
                check_update)
      .callback([&parser](bool v) {
        if (v) {
          check_for_update(GIT_DESCRIBE, "shediao", "vsrun", "vsrun.exe");
          // check_for_update exits the process if an update is performed.
          // If we get here, no update was needed/performed.
          std::exit(0);
        }
      });

  parser.add_flag("check",
                  "Check whether a matching Visual Studio instance "
                  "is installed.",
                  check_installed_or_not);

  parser.add_flag("i,ignore-environment", "start with an empty environment",
                  ignore_environment);

  parser.add_positional("CMDSTR",
                        "The command(s) to run inside the Visual "
                        "Studio Developer Command Prompt environment.",
                        user_cmds);

  parser.set_treat_remaining_as_positional();
  parser.usage_footer(R"==(Examples:
  vsrun where cmake cl

 # The command line string contains special characters: &<>()@^|
 vsrun cmake -B build -S . -D CMAKE_BUILD_TYPE=Release "&&" cmake --build build --config Release
  )==");

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

  if (check_installed_or_not) {
    if (all_match_visualstudios.empty()) {
      return EXIT_FAILURE;
    } else {
      return EXIT_SUCCESS;
    }
  }
  if (list_visual_studio) {
    for (auto const& vs : all_match_visualstudios) {
      std::wcout << vs << '\n';
    }
  }

  // run command
  if (all_match_visualstudios.empty()) {
    if (!list_visual_studio && !check_installed_or_not) {
      std::cerr << "Not Found ViusalStudio "
                << (product_id == "*" ? "Professional|Enterprise|Community"
                                      : product_id)
                << " " << version_range << " Installation" << '\n';
    }
    return EXIT_FAILURE;
  }

  if (!user_cmds.empty()) {
    std::filesystem::path installationPath =
        select_the_first_one ? all_match_visualstudios.front().install_path_
                             : all_match_visualstudios.back().install_path_;
    if (!is_directory(installationPath)) {
      std::cerr << "installation not a directory: " << installationPath << '\n';
      return EXIT_FAILURE;
    }
    std::filesystem::path VcDevCmdPath =
        installationPath / "Common7" / "Tools" / "VsDevCmd.bat";
    if (!is_regular_file(VcDevCmdPath)) {
      std::cerr << VcDevCmdPath.string()
                << " not exists or not a bat file: " << VcDevCmdPath << '\n';
      return EXIT_FAILURE;
    }

    std::vector<std::string> args{"cmd.exe",
                                  "/d",
                                  "/c",
                                  "call",
                                  VcDevCmdPath.string(),
                                  "-no_logo",
                                  "-host_arch=" + host_arch,
                                  "-arch=" + arch,
                                  ">nul&&"};

    std::map<std::wstring, std::wstring> envs;
    for (auto name : uset_env_names) {
      env::unset(name);
    }
    if (!ignore_environment) {
      envs = env::allutf16();
    }
    auto MSYSTEM = env::get("MSYSTEM");
    auto ORIGINAL_PATH = env::get("ORIGINAL_PATH");
    auto ORIGINAL_TEMP = env::get("ORIGINAL_TEMP");
    auto ORIGINAL_TMP = env::get("ORIGINAL_TMP");
    if (MSYSTEM && ORIGINAL_PATH && ORIGINAL_TEMP && ORIGINAL_TMP) {
      auto ORIGINAL_TEMP_DIR = std::filesystem::path(ORIGINAL_TEMP.value());
      auto ORIGINAL_TMP_DIR = std::filesystem::path(ORIGINAL_TMP.value());
      if (is_directory(ORIGINAL_TEMP_DIR) && is_directory(ORIGINAL_TMP_DIR)) {
        envs[L"PATH"] = to_wstring(ORIGINAL_PATH.value());
        envs[L"TEMP"] = ORIGINAL_TEMP_DIR.make_preferred().native();
        envs[L"TMP"] = ORIGINAL_TMP_DIR.make_preferred().native();
      }
    }

    for (auto const& [key, value] : set_env_vars) {
      envs[to_wstring(key)] = to_wstring(value);
    }

    args.insert(args.end(), user_cmds.begin(), user_cmds.end());
    if (debug_level >= 1) {
      std::copy(begin(args), end(args),
                std::ostream_iterator<std::string>(std::cerr, " "));
      std::cerr << '\n';
    }

    using subprocess::named_arguments::cwd;
    using subprocess::named_arguments::env;

    return subprocess::run(args, cwd = workdir, env = envs);
  } else {
    std::cerr << parser.usage() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
