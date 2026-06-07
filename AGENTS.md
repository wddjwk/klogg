# klogg - AGENTS.md

## 项目概述

klogg 是一个高性能跨平台日志文件查看器，fork 自 [glogg](https://github.com/nicko170/glogg)。它专为处理 GB 级别的大型日志文件而设计，核心优势是：快速索引、并行搜索、低内存占用。

- **语言**: C++17
- **GUI 框架**: Qt 5/6 (Qt Widgets，非 QML)
- **构建系统**: CMake + CPM.cmake (包管理) + Ninja
- **许可证**: GPLv3
- **版本号**: CalVer 格式 (如 24.11.0)
- **支持平台**: Windows (MSVC), Linux (GCC/Clang), macOS (Clang)

## 目录结构

```
klogg/
├── 3rdparty/            # 第三方依赖管理 (CPM.cmake 配置)
│   ├── CMakeLists.txt   # 所有 CPM 依赖声明
│   └── dump_syms/       # 崩溃符号提取工具 (各平台)
├── cmake/               # CMake 辅助模块
│   ├── CPM.cmake        # CPM 包管理器 (bundled)
│   ├── CompilerFlags.cmake
│   ├── CompilerWarnings.cmake
│   ├── Sanitizers.cmake
│   ├── StaticAnalyzers.cmake
│   ├── StandardProjectSettings.cmake
│   ├── prepare_version.cmake  # 版本号生成 + Windows .rc
│   └── Find*.cmake      # hyperscan, uchardet, tbb, xxHash 等
├── src/                 # 全部源码 (11 个子模块)
├── tests/               # 单元测试和集成测试 (Catch2)
├── packaging/           # 平台打包
│   ├── windows/         # NSIS 安装脚本, Chocolatey, Scoop
│   ├── linux/           # DEB, RPM, AppImage, Arch AUR, Gentoo
│   └── osx/             # DMG 配置
├── scripts/             # 构建辅助脚本
│   ├── build.ps1        # Windows 构建主脚本 (prepare/build/release/clean/clangd)
│   ├── gen_changelog.py
│   └── release_stats.py
├── Resources/           # 应用图标 (.ico, .icns)
├── website/             # Hugo 项目网站
├── .github/             # CI/CD (GitHub Actions)
├── CMakeLists.txt       # 根 CMake 配置
├── BUILD.md             # 各平台编译指南
├── DOCUMENTATION.md     # 用户文档 (内嵌到应用中)
└── klogg_local.nsi      # 本地 NSIS 安装脚本
```

## 模块架构

`src/` 下包含 11 个静态库，按依赖顺序构建：

```
klogg_version          # 版本信息 (从 git 生成 version.h)
    │
klogg_logging          # 内部日志框架 (log.h, logger.h)
    │
klogg_utils            # 工具集: 剪贴板, CRC32, CPU信息, 原子标志, 线程, UUID, 进度
    │
klogg_settings         # 持久化配置: Persistable<T> CRTP, QSettings 封装
    │
klogg_crash_handler    # 崩溃报告: Sentry/Crashpad 集成 (可选)
    │
klogg_regex            # 正则引擎: Hyperscan + QRegularExpression, 布尔表达式
    │
klogg_filewatch        # 文件监控: efsw 库 + 轮询回退
    │
klogg_logdata          # 核心数据模型: 文件索引, 行存储, 搜索/过滤引擎
    │
klogg_versioncheck     # 版本更新检查 (Qt Network)
    │
klogg_ui               # 完整 GUI 层 (~45 个 widget 类)
    │
app/                   # 入口点: 3 个可执行文件
```

### 三个可执行文件

| 可执行文件 | 说明 |
|---|---|
| `klogg` | 主 GUI 应用 (WIN32 / macOS Bundle) |
| `klogg_portable` | 便携版，配置存储在 exe 旁边 (定义 `KLOGG_PORTABLE`) |
| `klogg_grep` | 命令行搜索工具，复用 LogData/LogFilteredData 引擎，无 GUI |

## 核心引擎

### 文件索引

文件 **不使用内存映射**，而是分块读取 (5 MiB/块)，通过 Intel TBB flow graph 构建流水线：

```
[IO 线程] 读取 5MiB 块 → [限流节点] 背压控制 → [解析节点] 扫描换行符构建行偏移数组 → [提交] 原子追加到 IndexingData
```

- 使用 `uchardet` 在第一个块上检测文件编码
- `xxHash (XXH64)` 计算文件头尾摘要，用于快速变更检测
- 文件增长时执行 `PartialIndexOperation`，从上次偏移继续索引

**关键文件**: `src/logdata/src/logdataworker.cpp`, `src/logdata/src/logdata.cpp`

### 行位置存储

两种存储后端，通过模板 `LinePosition<Storage>` 统一：

| 后端 | 实现 | 特点 |
|---|---|---|
| `SimpleLinePositionStorage` | `std::vector<int64_t>` + mimalloc | 每行 8 字节，简单快速 |
| `CompressedLinePositionStorage` | StreamVByte delta 编码，128 行一块 | SIMD 加速解压，内存占用大幅降低 |

通过 `KLOGG_USE_COMPRESSED_INDEX` 配置选项选择。

**关键文件**: `src/logdata/include/linepositionarray.h`, `src/logdata/include/compressedlinestorage.h`

### 搜索引擎

搜索使用 TBB flow graph 构建大规模并行流水线：

```
[主线程] 读取行块 (10000行/块)
    → [限流节点] 约束并发
    → [N 个匹配线程] 每个线程独立的 PatternMatcher (Hyperscan scratch space)
    → [结果合并节点] 串行写入 SearchData (SharedMutex 保护)
```

- N 默认为 CPU 核心数，可配置
- 搜索结果存储在 `roaring::Roaring64Map` (压缩位图)，即使百万级匹配也极低内存
- 支持搜索结果缓存 (`unordered_map`，可配置上限，默认 100 万行)

**关键文件**: `src/logdata/src/logfiltereddataworker.cpp`, `src/logdata/src/logfiltereddata.cpp`

### 行渲染

UI 需要显示行内容时，通过 `LogData::getLinesRaw()` 根据行偏移索引 seek 到对应位置按需读取，不在内存中缓存行内容。

## 正则表达式系统

### 双引擎架构

| 引擎 | 库 | 使用场景 |
|---|---|---|
| **Hyperscan** (主) | Intel Hyperscan / Vectorscan | 搜索和高亮，支持多模式编译 `hs_compile_multi()` |
| **QRegularExpression** (备) | Qt PCRE2 | Hyperscan 不可用时的回退；QuickFind |

- Hyperscan 需要 SSE4.2 指令集，不满足时自动降级
- Hyperscan 编译失败的模式自动启用 **prefilter 模式**: Hyperscan 做粗筛，QRegularExpression 做精确验证
- 用户可在设置中手动选择引擎

### 布尔表达式

支持 `"error" AND "timeout" OR "fatal"` 形式的布尔组合搜索：

- 使用 `exprtk` 库解析和求值布尔表达式
- 提取引号中的子模式，分别编译到 Hyperscan
- ≤4 个子模式时预计算 2^N 真值表，每行求值退化为查表

**关键文件**: `src/regex/src/regularexpression.cpp`, `src/regex/src/booleanevaluator.cpp`, `src/regex/src/hsregularexpression.cpp`

## UI 架构

基于 Qt Widgets（非 QML），核心 widget 层级：

```
QMainWindow (MainWindow)
├── QToolBar
├── TabbedCrawlerWidget (多标签页，每个标签是一个文件)
│   └── CrawlerWidget (QSplitter，单文件的完整视图)
│       ├── LogMainView (上半部：完整日志视图)
│       ├── Overview (右侧滚动条上的匹配热力图)
│       └── QTabWidget
│           └── FilteredView (下半部：搜索结果视图，可多标签)
├── QuickFindWidget (Ctrl+F 搜索栏)
├── TabbedScratchPad (文本转换工具面板)
├── PathLine (文件路径/状态行)
└── StatusBar (行号, 文件大小, 日期, 编码)
```

### 关键 UI 模式

**SignalMux** (`src/ui/include/signalmux.h`):
MainWindow 与当前活跃 CrawlerWidget 之间的信号多路复用器。切换标签页时自动断开旧连接、连接新标签，避免 N*M 信号连接。

**QuickFindMux** (`src/ui/include/quickfindmux.h`):
将 QuickFind 搜索分发到当前获得焦点的视图（主视图或过滤视图），通过 `QuickFindMuxSelectorInterface` 确定活跃的可搜索组件。

**AbstractLogView** (`src/ui/include/abstractlogview.h`):
自定义绘制 (`QAbstractScrollArea`)，处理所有渲染、选区、滚动、键盘导航、QuickFind、follow 模式、文本换行。使用 `QPixmap` 缓存文本区域。

**关键文件**: `src/ui/src/mainwindow.cpp`, `src/ui/src/crawlerwidget.cpp`, `src/ui/src/abstractlogview.cpp`

## 关键功能

| 功能 | 实现位置 | 说明 |
|---|---|---|
| 多标签/多窗口 | `TabbedCrawlerWidget`, `KloggApp` | 每标签一个文件，支持多窗口 |
| Follow 模式 (tail -f) | `FileWatcher` + `efsw` | 原生 OS 通知 + 轮询回退，250ms 节流 |
| 高亮器 | `HighlighterSet`, `MultiRegularExpression` | 正则着色规则集，多集同时激活，Hyperscan 多模式编译 |
| 书签 (Marks) | `LogFilteredData::marks_` (Roaring bitmap) | 用户标记行，O(1) 前后导航 |
| 布尔搜索 | `BooleanExpressionEvaluator` + exprtk | AND/OR/NOT 组合搜索 |
| 会话管理 | `Session`, `WindowSession`, `SessionInfo` | 持久化打开的文件、滚动位置、窗口几何 |
| Scratchpad | `ScratchPad` | 文本转换：base64, hex, URL, JSON/XML 格式化, CRC32 |
| 国际化 | Qt Linguist (`en.ts`, `zh_CN.ts`, `zh_TW.ts`) | 三种语言，可在设置中切换 |
| 快捷键 | `ShortcutAction` (~60+ 动作) | 完全可自定义，支持 Vim 风格键位 |
| 远程文件 | `Downloader` (QNetworkAccessManager) | HTTP/HTTPS 下载到临时文件后加载 |
| 崩溃报告 | `CrashHandler` (Sentry + Crashpad, 可选) | 崩溃转储 + GitHub issue 模板 |
| 单实例 | `KDSingleApplication` + CBOR IPC | 二次启动时传文件路径到主实例 |
| 版本检查 | `VersionChecker` (Qt Network) | 后台定期检查更新 |
| 压缩索引 | `CompressedLinePositionStorage` | StreamVByte delta 编码，大幅降低内存 |

## 构建系统

### 构建流程 (Windows)

通过 `scripts/build.ps1` 驱动，makefile 封装为 5 个 action：

| 命令 | 说明 |
|---|---|
| `make prepare` | 下载依赖 (Boost, Ragel, OpenSSL)，复制 FileAssociation.nsh |
| `make build` | CMake configure + build (Ninja, Release) |
| `make release` | 收集 DLL/资源，生成便携 ZIP + NSIS 安装包 |
| `make clean` | 清理 build/ (保留 _deps/ 和 _cpm_cache/) |
| `make clangd` | 生成 .clangd 配置供 IDE 使用 |

### CMake 选项

| 选项 | 默认 | 说明 |
|---|---|---|
| `KLOGG_BUILD_TESTS` | OFF | 构建测试 |
| `KLOGG_USE_LTO` | ON | 链接时优化 |
| `KLOGG_USE_SENTRY` | OFF | Sentry 崩溃报告 |
| `KLOGG_GENERIC_CPU` | OFF | 编译为通用 x86-64 (非 native) |
| `KLOGG_USE_HYPERSCAN` | ON (Win64) | Hyperscan 正则引擎 |
| `KLOGG_USE_VECTORSCAN` | ON (非 Win) | Vectorscan 正则引擎 (ARM/非 x86) |

### 编译器特性

- MSVC: `/W4`, `/bigobj`, `/fp:fast`, SSE4.1/SSE4.2 定义
- GCC/Clang x86: `-mmmx -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mpopcnt`, `-march=native`
- Apple ARM: `-march=native -mtune=generic`
- 所有平台: C++17 required

## 第三方依赖

### CPM 管理 (3rdparty/CMakeLists.txt)

| 库 | 用途 |
|---|---|
| **Intel TBB** | 并行流水线 (索引/搜索 flow graph) |
| **Hyperscan / Vectorscan** | 高性能正则引擎 |
| **CRoaring** | 压缩位图 (搜索结果/书签存储) |
| **mimalloc** | 快速内存分配器 |
| **simdutf** | 快速 Unicode 验证/转换 |
| **StreamVByte** | Varint delta 压缩 (行位置索引) |
| **xxHash** | 快速哈希 (文件变更检测) |
| **uchardet** | 字符编码检测 |
| **exprtk** | 布尔表达式求值 |
| **efsw** | 跨平台文件系统监控 |
| **KDSingleApplication** | 单实例应用 IPC |
| **KDToolBox** | KDSignalThrottler (信号节流) |
| **KF5Archive** | 压缩文件解压 |
| **robin_hood** | 高性能哈希表 |
| **type_safe** | 类型安全工具 |
| **backward-cpp** | 堆栈回溯 (测试用) |
| **Catch2** | 测试框架 |
| **maddy** | Markdown 转 HTML (内嵌文档) |
| **whereami** | 可执行文件路径检测 |
| **sentry-native** | 崩溃报告 (可选) |

### 外部管理 (build.ps1 prepare)

| 库 | 版本 | 用途 |
|---|---|---|
| **Boost** | 1.86.0 | Hyperscan 构建需要 |
| **Ragel** | 6.10 | 状态机编译器 (Hyperscan 构建需要) |
| **OpenSSL** | 1.1.1 | HTTPS 支持 |
| **Qt** | 5.9+ / 6.x | GUI 框架 (Core, Widgets, Concurrent, Network, Xml) |

## 配置系统

### Persistable CRTP 模式

```cpp
template <typename T, typename SettingsType>
class Persistable {
    static T& get();          // 获取单例
    static T& getSynced();    // 获取并从 QSettings 同步
    void save();              // 持久化到 QSettings
    virtual void saveToStorage(QSettings&) = 0;
    virtual void retrieveFromStorage(QSettings&) = 0;
};
```

### 两个配置文件

| 文件 | 内容 |
|---|---|
| `klogg.conf` (app_settings) | 字体、搜索设置、性能参数、快捷键、高亮器、样式、语言 |
| `klogg_session.conf` (session_settings) | 打开的文件、窗口几何、最近文件、收藏文件、搜索历史 |

- **便携模式** (`KLOGG_PORTABLE`): INI 格式，存储在 exe 旁边
- **安装模式**: Windows 用 INI (`%APPDATA%\klogg\`)，macOS 用 plist，Linux 用原生格式
- 两个文件均有版本号，支持迁移升级

### 主要配置类

- `Configuration` — 所有用户偏好 (字体、搜索引擎、文件监控、性能调优等)
- `SessionInfo` — 每窗口的会话数据
- `SavedSearches` — 最近搜索历史 (最多 50 条)
- `HighlighterSetCollection` — 高亮规则集
- `RecentFiles` / `FavoriteFiles` — 最近/收藏文件
- `PredefinedFilters` — 预定义过滤器

## 线程模型

```
┌─────────────────────────────────────────────────────┐
│                    UI 线程 (Qt Event Loop)           │
│  MainWindow, CrawlerWidget, AbstractLogView 渲染    │
│  接收所有 worker 信号 (Qt::QueuedConnection)         │
└───────────────┬─────────────────────────────────────┘
                │ signal/slot
┌───────────────┼─────────────────────────────────────┐
│          索引操作 (QThreadPool, max 1 thread)        │
│  ┌────────────┴───────────┐                         │
│  │ TBB flow graph:        │                         │
│  │ IO → limiter → parse  │  (流水线，IO 和解析重叠)  │
│  └────────────────────────┘                         │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│          搜索操作 (QThreadPool, max 1 thread 驱动)   │
│  ┌────────────────────────────────────────┐         │
│  │ TBB flow graph:                        │         │
│  │ 读取行块 → limiter → [matcher_0...N]  │         │
│  │                   → resultsQueue       │         │
│  │                   → matchProcessor     │         │
│  └────────────────────────────────────────┘         │
│  N = CPU 核心数 (可配置), 每个 matcher 独立 scratch   │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│          文件监控 (efsw 后台线程)                     │
│  OS 原生通知 (inotify/FSEvents/ReadDirectoryChanges) │
│  + 轮询回退 (可配置间隔)                              │
│  250ms 节流后发送到 UI 线程                           │
└─────────────────────────────────────────────────────┘
```

## 大文件处理策略

klogg 处理 GB 级文件不 OOM 的关键设计：

1. **不内存映射**: 5 MiB 块读取，内存占用有界
2. **压缩行索引**: StreamVByte delta 编码，128 行一块，大幅降低索引内存
3. **按需读行**: 行内容不缓存，显示时 seek 到偏移量直接读取
4. **Roaring 位图**: 搜索结果和书签用压缩位图存储，百万匹配也极低内存
5. **有界预取**: TBB limiter node 约束在途块数，防止内存膨胀
6. **mimalloc**: 所有内部容器使用快速低开销分配器
7. **文件句柄管理**: 支持"保持关闭"模式，仅在读时打开文件

## 开发指南

### 添加新功能的一般路径

1. **数据层**: 在 `src/logdata/` 中添加/修改数据模型
2. **正则层**: 在 `src/regex/` 中扩展匹配能力
3. **UI 层**: 在 `src/ui/` 中添加 widget 或扩展现有 widget
4. **配置**: 在 `Configuration` 中添加字段，在 `OptionsDialog` 中添加 UI
5. **构建**: 在 `src/CMakeLists.txt` 和对应子模块的 CMakeLists.txt 中更新依赖

### 关键入口点

- 应用启动: `src/app/main.cpp` → `KloggApp`
- 打开文件: `MainWindow::open()` → `Session::open()` → `LogData::attachFile()`
- 搜索: `CrawlerWidget::startNewSearch()` → `LogFilteredData::runSearch()`
- QuickFind: `MainWindow::displayQuickFindBar()` → `QuickFindMux::setNewPattern()`
- 高亮: `HighlighterSetCollection` → `MultiRegularExpression` → `AbstractLogView` 渲染

### 测试

```bash
cmake -DKLOGG_BUILD_TESTS=ON ..
cmake --build . --target klogg_tests    # 单元测试 (Catch2)
cmake --build . --target klogg_itests   # 集成测试 (Catch2 + Qt Test)
```
