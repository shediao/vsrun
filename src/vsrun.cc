
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

#include "visualstudio.h"

std::string quote_path_if_needed(std::string&& p) {
  if (std::find(p.begin(), p.end(), ' ') != p.end()) {
    p.insert(p.begin(), '"');
    p.push_back('"');
  }
  return p;
}

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
      .checker(
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

  parser.add_option("C", "work directory", workdir)
      .value_help("workdir")
      .checker([](std::string const& dir) {
        if (std::filesystem::is_directory(dir)) {
          return std::pair<bool, std::string>{true, ""};
        } else {
          return std::pair<bool, std::string>{false, dir + " not a directory"};
        }
      });

  parser.add_option("u", "remove environment named <name>", uset_env_names)
      .value_help("name")
      .checker([](std::string const& name) {
        if (std::find(name.begin(), name.end(), '=') != name.end()) {
          return std::pair<bool, std::string>{false, name + " contain '='"};
        }
        return std::pair<bool, std::string>{true, ""};
      });

  parser.add_alias("c,community", "product", "Community");
  parser.add_alias("p,professional", "product", "Professional");
  parser.add_alias("e,enterprise", "product", "Enterprise");

  parser.add_flag("first", "select the first one to run(Default)",
                  select_the_first_one);
  parser.add_negative_flag("last", "select the last one to run",
                           select_the_first_one);

  parser.add_flag("list", "list all match visual studio infomation",
                  list_visual_studio);
  parser.add_flag("V,verbose", "show verbose messages", debug_level);

  parser.add_flag("check", "check require visual studio is installed",
                  check_installed_or_not);

  parser.add_flag("i,ignore-environment", "start with an empty environment",
                  ignore_environment);

  parser.add_positional("CMDSTR", "run command in vs dev environment",
                        user_cmds);

  parser.set_remaining_are_positional();
  parser.help_footer(R"==(Examples:
  vsrun where cmake cl

  # '&&' must be quoted
  vsrun cmake -B build -S . -D CMAKE_BUILD_TYPE=Release '&&' cmake --build build --config Release

  vsrun "cmake -B build -S . -D CMAKE_BUILD_TYPE=Release && cmake --build build --config Release"
  )==");

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
                                  quote_path_if_needed(VcDevCmdPath.string()),
                                  "-no_logo",
                                  "-host_arch=" + host_arch,
                                  "-arch=" + arch,
                                  ">nul&&"};

    std::map<std::string, std::string> envs;
    for (auto name : uset_env_names) {
      env::unset(name);
    }
    if (!ignore_environment) {
      envs = env::allutf8();
    }
    while (!user_cmds.empty() &&
           user_cmds.begin()->find('=') != std::string::npos) {
      auto tmp = split(*user_cmds.begin(), '=', 1);
      envs.insert({tmp[0], tmp.size() > 1 ? tmp[1] : ""});
      user_cmds.erase(user_cmds.begin());
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
