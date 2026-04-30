# vsrun

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Run commands in a Visual Studio Developer Command Prompt environment — without opening Visual Studio.

`vsrun` finds your installed Visual Studio instances, calls `VsDevCmd.bat` to set up the compiler toolchain environment, and then executes your commands. Think of it as a drop-in replacement for manually launching the "Developer Command Prompt for VS".

There is also a companion tool `vs-install-dir` that simply prints the installation directory of matching Visual Studio instances.

## Features

- **Automatic VS discovery** — scans for all installed Visual Studio instances via the [Setup Configuration API](https://learn.microsoft.com/en-us/dotnet/api/microsoft.visualstudio.setup.configuration)
- **Version & product filtering** — target specific versions (e.g., `[17.0,18.0)`) and editions (Community, Professional, Enterprise)
- **Workload filtering** — require a specific workload to be installed (e.g., `Microsoft.VisualStudio.Workload.NativeDesktop`)
- **Architecture selection** — choose target arch (`x86`, `x64`, `arm64`) and host arch independently
- **Flexible sorting** — sort matching instances by version, date, or product before picking the first/last one
- **Environment control** — unset specific environment variables, start with an empty environment, or work in a custom working directory
- **MSYS2 / Git Bash support** — automatically detects and handles MSYS2 environment paths
- **Shell completions** — built-in generation of bash, zsh, and fish completion scripts
- **Windows 7 compatible** — targets `_WIN32_WINNT=0x0601`

## Requirements

- Windows 7 or later
- Visual Studio 2017 or later (version 15.0+) installed at least with one of Community, Professional, or Enterprise editions
- For building: CMake 3.21+, a C++20 compiler (MSVC, MinGW, or Clang)

## Installation

### Pre-built binaries

Download the latest release from the [Releases](https://github.com/shediao/vsrun/releases) page.

### Build from source

```bash
cmake -B build -S .
cmake --build build --config Release
```

The build produces two executables:

```
build/
├── vsrun.exe              # Run commands in VS Dev environment
└── vs-install-dir.exe     # Print VS installation directories
```

## Usage

### vsrun

```
vsrun [options] [CMDSTR...]
```

#### Options

| Option                     | Description                                                                        |
| -------------------------- | ---------------------------------------------------------------------------------- |
| `--arch`                   | Target CPU architecture: `x86`, `x64`, `arm64` (default: host arch)                |
| `--host-arch`              | Host CPU architecture: `x86`, `x64`, `arm64` (default: host arch)                  |
| `-v, --version`            | Version range for instances, e.g., `[17.0,18.0)` (default: `[16.0,)`)              |
| `-c, --community`          | Filter for Visual Studio Community edition                                         |
| `-p, --professional`       | Filter for Visual Studio Professional edition                                      |
| `-e, --enterprise`         | Filter for Visual Studio Enterprise edition                                        |
| `--product`                | Product ID filter, e.g., `Community`, `Professional`, `Enterprise`                 |
| `--workload`               | Require a specific workload, e.g., `Microsoft.VisualStudio.Workload.NativeDesktop` |
| `--sort`                   | Sort instances, e.g., `version:asc,product:Professional-Enterprise-Community`      |
| `--first`                  | Use the first matching instance (default)                                          |
| `--last`                   | Use the last matching instance                                                     |
| `-C`                       | Working directory before running commands                                          |
| `-u`                       | Unset environment variable(s)                                                      |
| `-i, --ignore-environment` | Start with an empty environment                                                    |
| `--list`                   | List all matching VS instances without executing                                   |
| `--check`                  | Check whether a matching VS instance is installed (exit code)                      |
| `--verbose`                | Enable verbose debug output                                                        |
| `-V`                       | Print version and exit                                                             |

#### Examples

```bash
# Find the cmake and cl executables in the VS environment
vsrun where cmake cl

# Build a CMake project with the VS toolchain
vsrun "cmake -B build -S . -D CMAKE_BUILD_TYPE=Release && cmake --build build --config Release"

# List all installed VS 2022 instances
vsrun -v "[17.0,18.0)" --list

# Run with a specific product and architecture
vsrun -e --arch arm64 "cl /?"     # Enterprise, target ARM64

# Check if a compatible Visual Studio is installed
vsrun --check && echo "VS found" || echo "VS not found"

# Run in a specific working directory with clean environment
vsrun -i -C "D:\MyProject" nmake
```

### vs-install-dir

```
vs-install-dir [options]
```

Prints the installation path(s) of matching Visual Studio instances. Supports the same filtering options as `vsrun` (`-v`, `--product`, `--workload`, `--sort`, `--first`, `--last`).

```bash
# Print install directory of the latest VS 2022 Professional
vs-install-dir -p -v "[17.0,18.0)" --last

# Print all install directories
vs-install-dir
```

## Shell Completions

Generate completion scripts for your shell:

```bash
# Bash
vsrun --print-bash-complete > ~/.bash_completion.d/vsrun

# Zsh
vsrun --print-zsh-complete > ~/.zfunc/_vsrun

# Fish
vsrun --print-fish-complete > ~/.config/fish/completions/vsrun.fish
```

`vs-install-dir` also supports the same completion flags.

## How It Works

1. `vsrun` initializes COM and creates a `ISetupConfiguration2` instance via the Visual Studio Setup API.
2. It enumerates all VS instances and filters them by version range, product ID, and workload.
3. It locates `VsDevCmd.bat` (typically at `Common7\Tools\VsDevCmd.bat` under the install path).
4. It spawns `cmd.exe` that calls `VsDevCmd.bat` with the requested `-arch` and `-host_arch` flags, then chains the user's commands via `&&`.
5. Environment variables from the current process are forwarded (with special handling for MSYS2 environments).

## License

MIT — see the [Setup.Configuration.h](include/vssetup/Setup.Configuration.h) header for the Microsoft-authored COM interface definitions, also MIT-licensed.
