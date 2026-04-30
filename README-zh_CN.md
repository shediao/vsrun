# vsrun

[![CI](https://github.com/shediao/vsrun/actions/workflows/ci.yml/badge.svg)](https://github.com/shediao/vsrun/actions/workflows/ci.yml)
[![Release](https://github.com/shediao/vsrun/actions/workflows/release.yml/badge.svg)](https://github.com/shediao/vsrun/actions/workflows/release.yml)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

在 Visual Studio Developer Command Prompt 环境中运行命令 — 无需打开 Visual Studio。

`vsrun` 会查找已安装的 Visual Studio 实例，调用 `VsDevCmd.bat` 设置编译器工具链环境，然后执行你的命令。可以把它看作"VS 开发者命令提示符"的命令行替代品。

配套工具 `vs-install-dir` 用于打印匹配的 Visual Studio 实例的安装目录。

## 特性

- **自动发现 VS 实例** — 通过 [Setup Configuration API](https://learn.microsoft.com/zh-cn/dotnet/api/microsoft.visualstudio.setup.configuration) 扫描所有已安装的 Visual Studio
- **版本与产品过滤** — 可指定版本范围（如 `[17.0,18.0)`）和版本类型（Community、Professional、Enterprise）
- **工作负载过滤** — 要求安装特定的工作负载（如 `Microsoft.VisualStudio.Workload.NativeDesktop`）
- **架构选择** — 独立选择目标架构（`x86`、`x64`、`arm64`）和主机架构
- **灵活排序** — 按版本、日期或产品排序匹配的实例，然后选择第一个或最后一个
- **环境变量控制** — 可移除指定环境变量、从空环境启动，或在指定工作目录下运行
- **MSYS2 / Git Bash 支持** — 自动检测并处理 MSYS2 环境路径
- **Shell 补全** — 内置生成 bash、zsh、fish 补全脚本
- **兼容 Windows 7** — 目标 `_WIN32_WINNT=0x0601`

## 环境要求

- Windows 7 或更新版本
- 已安装 Visual Studio 2017 或更新版本（版本 15.0+），至少包含 Community、Professional 或 Enterprise 之一
- 构建需求：CMake 3.21+、C++20 编译器（MSVC、MinGW 或 Clang）

## 安装

### 预编译二进制文件

从 [Releases](https://github.com/shediao/vsrun/releases) 页面下载最新版本。

### 从源码构建

```bash
cmake -B build -S .
cmake --build build --config Release
```

构建会生成两个可执行文件：

```
build/
├── vsrun.exe              # 在 VS 开发环境中运行命令
└── vs-install-dir.exe     # 打印 VS 安装目录
```

## 用法

### vsrun

```
vsrun [选项] [CMDSTR...]
```

#### 选项

| 选项                       | 描述                                                                     |
| -------------------------- | ------------------------------------------------------------------------ |
| `--arch`                   | 目标 CPU 架构：`x86`、`x64`、`arm64`（默认：主机架构）                   |
| `--host-arch`              | 主机 CPU 架构：`x86`、`x64`、`arm64`（默认：主机架构）                   |
| `-v, --version`            | 版本范围，如 `[17.0,18.0)`（默认：`[16.0,)`）                            |
| `-c, --community`          | 筛选 Community 版本                                                      |
| `-p, --professional`       | 筛选 Professional 版本                                                   |
| `-e, --enterprise`         | 筛选 Enterprise 版本                                                     |
| `--product`                | 产品 ID 过滤，如 `Community`、`Professional`、`Enterprise`               |
| `--workload`               | 要求安装特定工作负载，如 `Microsoft.VisualStudio.Workload.NativeDesktop` |
| `--sort`                   | 排序实例，如 `version:asc,product:Professional-Enterprise-Community`     |
| `--first`                  | 使用第一个匹配的实例（默认）                                             |
| `--last`                   | 使用最后一个匹配的实例                                                   |
| `-C`                       | 运行命令前切换到指定工作目录                                             |
| `-u`                       | 移除指定环境变量                                                         |
| `-i, --ignore-environment` | 从空环境启动                                                             |
| `--list`                   | 列出所有匹配的 VS 实例，不执行命令                                       |
| `--check`                  | 检查是否有匹配的 VS 实例（通过退出码判断）                               |
| `--verbose`                | 启用详细调试输出                                                         |
| `-V`                       | 打印版本并退出                                                           |

#### 示例

```bash
# 在 VS 环境中查找 cmake 和 cl 的位置
vsrun where cmake cl

# 使用 VS 工具链构建 CMake 项目
vsrun "cmake -B build -S . -D CMAKE_BUILD_TYPE=Release && cmake --build build --config Release"

# 列出所有已安装的 VS 2022 实例
vsrun -v "[17.0,18.0)" --list

# 使用指定产品和架构运行
vsrun -e --arch arm64 "cl /?"     # Enterprise 版本，目标 ARM64

# 检查是否安装了兼容的 Visual Studio
vsrun --check && echo "已找到 VS" || echo "未找到 VS"

# 在指定目录以干净环境运行
vsrun -i -C "D:\MyProject" nmake
```

### vs-install-dir

```
vs-install-dir [选项]
```

打印匹配的 Visual Studio 实例的安装路径。支持与 `vsrun` 相同的筛选选项（`-v`、`--product`、`--workload`、`--sort`、`--first`、`--last`）。

```bash
# 打印最新 VS 2022 Professional 的安装目录
vs-install-dir -p -v "[17.0,18.0)" --last

# 打印所有安装目录
vs-install-dir
```

## Shell 补全

为你的 shell 生成补全脚本：

```bash
# Bash
vsrun --print-bash-complete > ~/.bash_completion.d/vsrun

# Zsh
vsrun --print-zsh-complete > ~/.zfunc/_vsrun

# Fish
vsrun --print-fish-complete > ~/.config/fish/completions/vsrun.fish
```

`vs-install-dir` 同样支持上述补全标志。

## 工作原理

1. `vsrun` 初始化 COM 并通过 Visual Studio Setup API 创建 `ISetupConfiguration2` 实例。
2. 枚举所有 VS 实例，按版本范围、产品 ID 和工作负载进行筛选。
3. 定位 `VsDevCmd.bat`（通常位于安装路径下的 `Common7\Tools\VsDevCmd.bat`）。
4. 生成一个 `cmd.exe` 进程，调用 `VsDevCmd.bat` 并传入 `-arch` 和 `-host_arch` 参数，然后通过 `&&` 链接用户的命令。
5. 转发当前进程的环境变量（对 MSYS2 环境有特殊处理）。

## 许可证

MIT — 参见 [Setup.Configuration.h](include/vssetup/Setup.Configuration.h) 头文件，其中包含 Microsoft 提供的 COM 接口定义，同样使用 MIT 许可证。
