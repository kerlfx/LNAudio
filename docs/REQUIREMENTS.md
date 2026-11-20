# LNPlayer 需求基线

> 状态：Windows 完整版本开发阶段  
> 本文区分已经确认的项目要求、当前工程约定和仍待确认的事项。

## 1. 项目定位

LNPlayer 是一个面向 Windows 和 Android 的本地媒体播放器。

当前阶段只实现本地播放功能，不开发多端同步能力。各设备独立管理本地媒体和播放状态，不依赖中心服务器、P2P 网络或其他设备。

Windows 端同时提供音频和视频播放界面，两套 UI 属于同一个 LNPlayer 应用程序，不是两个独立产品或两套独立可执行程序。

## 1.1 开发平台顺序

- 当前开发阶段以 **Windows 完整版本** 为唯一交付目标。
- 在 Windows 版本达到完整功能、稳定性和发布要求之前，不启动 Android UI、JNI、Android 音频输出或 Android 视频输出的实现工作。
- Android 仍属于产品后续支持平台，不从整体架构中删除；但不得与 Windows 当前开发任务并行分散实现资源。
- Core 继续保持平台无关：不得依赖 Qt、Win32、D3D11、WASAPI 等 Windows 专有类型作为通用播放逻辑的公共接口。Windows 专有能力通过 Platform Audio/Video Output 实现。Core 优先采用 C++ 标准库；Standalone Asio、FFmpeg、SQLite 分别用于异步协调、媒体处理和音乐库持久化。
- Windows 完整版本以本文已确认的 Windows 音频、视频、音乐库、播放列表、窗口/进程管理和系统集成功能为范围。第 11 节列出的候选功能也纳入 Windows 完整版本规划；具体交互和参数在对应模块实现前细化。
- 本阶段不实现多端同步、远程控制、设备发现、P2P 或云端服务。

## 1.2 Windows 完整版本的实施顺序

1. **工程与依赖基础**：完善 Windows CMake 工程、Standalone Asio、FFmpeg、SQLite 和 Qt 部署配置。
2. **Core 播放运行时**：完成 Controller 命令队列、线程池任务调度、PlaybackSession 接入、异步操作结果、同步完成型接口和可靠销毁屏障。
3. **Windows 音频链路**：完成 FFmpeg 音频解封装/解码、音频时钟、重采样/格式转换、Windows 音频输出、设备选择及音量/静音控制。
4. **音频播放器与音乐库**：完成音频 UI、直接播放、目录导入/扫描/监控、SQLite 索引、元数据、重复文件位置关联、搜索/浏览、播放列表、最近播放、随机/循环播放、封面/歌词和均衡器。
5. **Windows 视频链路**：完成 FFmpeg 视频解码、硬件解码优先与软件回退、D3D11 Renderer、视频帧队列、PTS 调度、音视频同步、Seek/暂停/恢复和资源生命周期。
6. **视频播放器完整交互**：完成 PotPlayer 风格布局、播放列表、多音轨、内嵌/外挂字幕、快捷键、鼠标操作、全屏/窗口化、画面比例、逐帧、截图，以及规划中的滤镜和字幕样式。
7. **窗口与进程模型**：完成一个窗口对应一个播放器子进程、音频/视频模式启动、文件管理器打开文件、连续打开新窗口、进程间请求转发及独立退出。
8. **Windows 系统集成**：完成系统媒体按键、任务栏/系统媒体控制、后台音频播放、设置持久化、安装部署和发布验证。
9. **Windows 完整性审查**：按功能清单逐项验证媒体兼容性、错误恢复、并发/销毁、长时间播放、多窗口资源占用和安装环境运行。

上述顺序允许在实现中按依赖关系调整，但 Android 实现必须等 Windows 完整版本完成后再开始。

## 2. 已确认平台与技术选型

| 范围 | 方案 |
|---|---|
| 播放器核心 | C++ |
| 音视频处理 | FFmpeg |
| Windows UI | Qt 6 Widgets |
| Android UI | Kotlin 原生 UI |
| 音乐库持久化 | SQLite |
| 原生代码构建 | CMake |
| C++ 语言标准 | C++20 |

## 3. 架构与构建约定

### 3.1 共享核心

- Windows 和 Android 共用 C++ 播放器核心。
- 核心以动态库形式提供：Windows 生成 DLL，Android 生成 SO。
- 核心通过独立的对外接口供平台应用调用；C ABI 公共头文件位于 `core/include/lnplayer.h`。
- 核心内部使用 C++20 实现，不要求内部模块遵循 C 语言编程风格。
- Core 优先使用 C++ 标准库实现通用容器、字符串、数据模型、算法、智能指针等；异步调度、异步流程编排、定时任务和串行执行优先统一使用 Standalone Asio，不再为相同职责重复引入其他并发框架。
- Core 不依赖 Qt，也不使用 Qt 类型或 Qt 并发/时间设施；Standalone Asio 是已确认的必要基础设施，不使用 Boost.Asio。
- 内部头文件与对应的 .cpp 放在同一模块目录。
- 只有对外公开的 C ABI 头文件放在 `core/include/`，不再增加额外的 `lnplayer/` 子目录。
- Core 不依赖 Qt、Kotlin 或 JNI。

### 3.2 CMake 组织

- core/CMakeLists.txt 独立管理 Core 动态库及其构建配置。
- 仓库根目录的 CMakeLists.txt 只负责设置项目级基础配置并组织子目录，不承载 Core 的具体构建细节。
- Windows 和 Android 平台工程分别管理各自的应用构建，并通过 CMake 引入 Core。
- Core 的 CMake 配置应可在 Windows 原生 CMake 构建中使用，也应可由 Android Gradle External Native Build 调用。
- Android UI 不使用 CMake 构建：Kotlin UI 和 Android 应用由 Gradle 构建；Gradle 调用 CMake/NDK 构建 JNI 桥接层及 C++ Core。
- Android JNI 仅承担 Kotlin 与 Native 接口之间的桥接，不承载播放器业务逻辑；JNI 桥接与 Core 保持独立。
- 暂不为了模块边界强制拆分多个动态库。



### 3.3 平台职责

- Core 对外 C ABI 使用普通同步函数调用形式，接口按返回语义分为提交型、同步完成型和快速查询型。
- 提交型接口：首期 `open`、`seek`。请求成功进入唯一 Player 的受管理命令队列后立即返回提交结果及 operation_id；最终结果通过完成事件通知。
- 同步完成型接口：首期 `play`、`pause`、`stop`、`close`。调用线程等待实际操作完成后返回最终结果，不只是返回请求入队结果。
- 快速查询型接口：如 `get_state`、`get_position`、`get_duration`、`get_media_info`，只读取线程安全状态快照或缓存，不等待媒体探测、解码或阻塞式文件 I/O。
- 每个进程只有一个 Player。每个进程内的 Core Runtime 维护一个 `asio::io_context`、一套共享 `asio::thread_pool` 和唯一 Player；当前共享 `thread_pool` 固定为 2 个 worker，不按播放器负载创建大量线程。
- `io_context` 负责命令协调、任务分发、异步操作组合、结果回收、状态更新、事件排序和同步等待者唤醒；共享 `thread_pool` 执行有限时长的后台业务任务。
- 在适合的异步流程中优先使用 Asio C++20 Coroutine（`asio::awaitable`、`asio::co_spawn`、`asio::use_awaitable`）组织步骤与错误传播；优先使用 Asio Completion Token/Completion Handler 表达异步结果，避免为普通异步流程额外维护大量手写 `std::promise`/`std::future` 和嵌套回调。只有在 C ABI 同步完成语义或外部接口确有需要时，才在边界进行同步等待或结果适配。
- 定时和延迟调度优先使用 `asio::steady_timer`；需要对同一执行器上的状态变更、命令或回调进行串行化时优先使用 `asio::strand`。`strand` 用于执行顺序约束，不替代对跨执行器共享数据的必要同步。
- `asio::post` 用于明确切换到指定执行器，`asio::dispatch` 用于允许在当前执行器直接执行的场景，`asio::defer` 用于适合延后调度的连续异步流程；按语义选择，不机械替换。
- 同步完成型接口的等待发生在调用 C ABI 的线程，`io_context` worker 不得阻塞等待线程池业务任务。快速查询直接读取线程安全快照，是不经过 `io_context` 的明确例外。
- `open/seek` 的最终成功或失败通过携带 operation_id 的完成事件报告；`play/pause/stop/close` 的最终结果由函数返回。独立状态变化、播放结束和运行时错误通过事件通知。视频帧交付保持独立实时数据通道。
- Windows 使用 Qt 6 Widgets；Qt GUI 主线程可直接调用提交型接口和快速查询。同步完成型接口应由后台线程调用，避免阻塞 GUI。
- Android 使用 Kotlin 原生 UI，通过 JNI 桥接调用 Core；JNI 只承担参数转换、句柄桥接和事件转发。同步完成型接口应由后台 Dispatcher/线程调用，避免阻塞 Main Thread。
- 事件回调不得直接操作平台 UI 对象，必须投递到对应 UI 线程。
- 平台相关音频输出、视频渲染、设备管理和系统媒体控制与共享播放逻辑分离。
- UI 不直接承担音视频解码工作。
- FFmpeg 负责媒体探测、解封装和解码；Qt 负责 Windows 界面及窗口交互，不另行建立与 Core 并行的媒体播放管线。
- Core 管理视频帧队列与播放时钟调度；有音频时以 WASAPI `IAudioClock` 为主时钟，无音频视频使用 `std::chrono::steady_clock` 维护单调时钟，并由 Core 提供媒体播放位置。无音频时钟在暂停期间冻结，恢复时重新计时，Seek 和播放速度变化时重新锚定。平台 UI 只依据 Core 的播放位置和视频帧 PTS 调度呈现，不自行维护媒体时钟，也不使用 `QElapsedTimer` 计算媒体时间。帧就绪、显示节奏或平台呈现时机触发任务，经 Runtime `io_context` 协调后提交到共享 `thread_pool` 执行 GPU 呈现，不默认创建独立渲染线程或渲染线程池。视频帧交付不通过普通播放控制请求响应或主动事件回调逐帧传输。

### 3.4 Windows 工程现状与目标

- Windows 使用单一 `LNPlayer.exe`，通过 `--audio` / `--video` 启动对应界面；每个播放器子进程只创建一个窗口和一个 Player。
- UI 使用 Qt 6 Widgets，构建要求 Qt 6.12 或更新的 Qt 6 版本。
- Core 使用独立的 Standalone Asio 头文件依赖，不依赖 Boost；默认从仓库的 `3rd_cache/asio/asio/include` 或 `3rd_cache/asio/include` 查找 `asio.hpp`。
- Core Runtime 使用单线程 `asio::io_context` 和共享 `asio::thread_pool`。异步业务优先使用 Asio 原生执行器、Completion Token、C++20 Coroutine、`steady_timer` 和 `strand`；标准库线程/同步原语仅用于 Asio 不适合或平台接口明确要求的边界。
- 当前 Windows 已接入 FFmpeg 音频解码、WASAPI 输出和 SQLite 音乐库；视频模式已接入 FFmpeg 视频解码、Windows D3D11 Swap Chain 呈现、D3D11VA 尝试与软件回退。
- 视频帧通过 Windows 专用 `lnplayer_windows.h` Pull 接口交付，不扩展通用播放控制 C ABI。视频 UI 已具备 PTS 基础等待、全屏、快捷键、右键菜单、原始比例/16:9/4:3/拉伸切换、截图及上一帧/下一帧逐帧控制；逐帧历史缓存最多保留 120 帧。视频播放队列支持打开/拖放文件整体替换、追加视频文件、移除条目、上移/下移、拖动排序、双击切换、上一项/下一项、随机顺序、关闭/列表/单项循环及播放结束自动切换；视频播放队列已通过 QSettings 持久化文件列表、当前索引、随机播放和循环模式。视频文件的多音轨已支持 FFmpeg 枚举、C ABI 查询/选择和 Windows 右键菜单切换；切换要求暂停后执行，UI 会自动暂停并尝试恢复原播放状态。外挂 SRT 字幕支持 UTF-8、UTF-8 BOM、UTF-16 LE/BE BOM、UTF-32 LE/BE BOM 和系统本地编码回退；外挂 ASS/SSA 已支持按事件时间提取文本并处理基础换行/空格转义，但未实现 ASS 样式、定位和特效。字幕启用/禁用及 ±500 ms 时间调整已提供；内嵌字幕已接入 FFmpeg 字幕流枚举、默认轨道选择、字幕包队列、文本/ASS 字幕解码、轨道切换与禁用，并通过 Windows 视频帧回调将当前字幕文本交给现有叠加层；位图字幕和完整 ASS 渲染仍待完成。
- 无音频视频播放时钟由 Core 使用 `std::chrono::steady_clock` 实现；暂停冻结时钟，恢复重新计时，Seek 和播放速度变化时重新锚定时钟。Windows UI 不再使用 `QElapsedTimer` 维护视频媒体时钟。
- FFmpeg D3D11VA 已接入 Renderer 的共享 D3D11 Device。NV12/P010 硬件帧通过 AVFrame 引用交给 D3D11 Video Processor，转换到独立 BGRA 输出纹理，再复制到 Swap Chain back buffer；GPU event query 完成前保留 AVFrame，GPU 队列最多保留 4 帧，超出时丢帧。外部字幕通过透明 BGRA overlay 在 GPU 输出纹理上合成；截图从保留的 BGRA 输出纹理读回。共享设备不可用时仍保留 FFmpeg CPU transfer + BGRA 上传路径；Video Processor 初始化或呈现失败时，会保留 AVFrame 并按需调用 FFmpeg `av_hwframe_transfer_data()` 下载和转换为 BGRA，再走软件呈现路径。当前代码包含设备重建/软件回退的初步处理，但真实 GPU 拔出、驱动重置、多显卡切换及设备恢复验证不属于当前功能开发任务，留待 Windows 完整功能完成后统一评估。音频主时钟已接入 WASAPI `IAudioClock` 硬件位置计数，并结合待播放缓冲量映射媒体时间，硬件时钟查询失败时回退到缓冲量估算。外挂 SRT 字幕已支持基础加载和画面叠加；切换媒体时清理字幕数据。外挂字幕编码支持 UTF-8、UTF-8 BOM、UTF-16 LE/BE BOM、UTF-32 LE/BE BOM 和系统本地编码回退；ASS/SSA 已支持基础事件时间和文本提取，但样式、定位和特效尚未实现。外挂字幕支持时间偏移及启用/禁用；内嵌字幕文本轨道枚举、解码、切换和禁用已实现，位图字幕已接入 BGRA 叠加；完整 ASS 样式、定位和特效仍待完成。视频多音轨枚举与切换、上一帧/下一帧逐帧控制已实现；视频旋转元数据已通过 FFmpeg Display Matrix 解析并在软件输出路径执行 90°/180°/270° 旋转；色彩空间/HDR 处理及滤镜仍未实现。播放速度 0.25x–4.0x 已接入，当前通过重采样改变速度，因此音高也会变化。
- 本节描述的是当前工程状态，不代表 Windows 功能范围仅限于首期骨架；Windows 完整版本范围由第 1.1、1.2 节和后续 Windows 功能章节共同定义。

### 3.5 第三方依赖获取与集成

- CMake 使用官方 `FetchContent` 管理需要下载的第三方依赖；依赖统一缓存到仓库根目录的 `3rd_cache/`，不放入普通构建目录。
- `FetchContent` 既用于获取源码依赖，也用于下载已经编译完成的预编译 SDK。预编译 SDK 只下载和解压，不作为当前工程的子项目重新编译。
- FFmpeg 使用固定版本的 Windows MSVC x64 LGPL Shared 预编译 SDK，下载并解压到 `3rd_cache/ffmpeg`，通过头文件和导入库链接，运行时需要部署对应 DLL。
- SQLite 使用官方 SQLite Amalgamation（当前固定版本 3.50.4），从官方 ZIP 获取 `sqlite3.c` 和 `sqlite3.h`，将 `sqlite3.c` 直接编译进 Core 动态库；不单独部署或链接 `sqlite3.dll`。
- 下载地址和版本必须固定，并使用官方发布的哈希值校验归档文件。已有依赖目录时应复用本地内容，避免每次 CMake 配置重复下载。
- SQLite 头文件只供 Core 内部使用，不加入公共 C ABI，也不向平台 UI 暴露 SQLite 类型。

### 3.6 Windows 进程与窗口模型

- 音频 UI 和视频 UI 属于同一个 LNPlayer 应用程序，使用同一个应用程序可执行文件。
- 应用程序允许同时运行多个进程，不采用强制单实例限制。
- 每创建一个播放器窗口，都启动一个独立子进程；一个播放器窗口对应一个播放器子进程。
- **每个播放器子进程只有一个 Player**，并创建和管理该 Player 对应的窗口及媒体处理资源。
- 每个子进程独立运行 Qt GUI 主线程。窗口及其 QWidget 对象属于该进程的 GUI 主线程；媒体处理工作由 Core 后台任务承担，不在 GUI 主线程执行耗时解码。
- 不同窗口进程之间的 Player、Runtime、线程池、播放状态、媒体资源及 FFmpeg 上下文完全独立。关闭一个窗口时，只结束其对应的播放器子进程，不应影响其他窗口进程。
- 主进程/启动器的常驻策略、进程间通信方式和文件打开请求分发机制尚未确定。
- Android 每个应用进程同样只创建一个 Player；若未来引入额外 Android 进程，每个进程也各自只拥有一个 Player。

## 4. 音乐打开与播放方式

播放器同时支持：

1. **音乐库模式**：通过本地音乐库浏览和选择音乐进行播放。
2. **直接播放模式**：通过平台文件选择器选择单个音乐文件后立即播放，不要求先导入音乐库。

两种模式共用同一套播放器核心和播放控制逻辑。两种模式产生的播放记录均纳入最近播放历史。

## 5. 本地音乐目录与导入

- 支持添加多个本地音乐目录作为音乐库来源，并自动扫描目录中的音乐文件。
- 用户可以随时添加或移除已配置的音乐目录。
- Windows 与 Android 使用各自平台的目录选择和访问授权流程。
- 对已配置目录自动监控文件变化，并提供手动刷新音乐库的操作。
- 新增文件时加入音乐库；删除、重命名或移动文件时更新索引；文件内容变化时重新读取相关媒体信息。
- 播放器启动时检查目录状态，补偿监控遗漏或播放器未运行期间发生的变化。
- 支持手动选择单个音乐文件或整个目录加入音乐库；手动导入结果与自动扫描结果统一管理。
- 手动导入的文件或目录不要求位于已配置的自动扫描目录中。
- 同一音乐文件可以同时存在于不同目录；播放器不得因去重而删除、移动或修改原始文件。
- 音乐库识别重复音乐内容，避免将同一首音乐作为多个独立条目重复展示。
- 一个音乐条目可以关联多个实际文件位置。
- 删除某个文件位置时，只移除对应关联；只要仍有其他有效位置，音乐条目就继续保留。
- 当前已实现 SQLite 文件位置/曲目关联、SHA-256 文件内容去重、FFmpeg 音频元数据读取、递归/非递归目录扫描，以及 Windows 音频模式下的手动目录导入、音乐库搜索和曲目列表浏览。自动目录监控、启动补偿扫描、文件删除/重命名同步和单文件导入仍未实现。 目录重复扫描会先比较文件大小和修改时间；未变化的文件只更新时间戳，跳过完整 SHA-256 读取及 FFmpeg 元数据探测，只有新增或发生变化的文件才重新计算哈希和读取元数据。

## 6. 首期音频格式

首期支持 MP3、FLAC、WAV、AAC/M4A、OGG/Opus、WMA、AIFF 和 ALAC。

FFmpeg 负责媒体探测、解封装和解码。构建配置必须包含上述格式所需的解码器和解封装器，不能仅凭引入 FFmpeg 就默认格式均已启用。

## 7. 音乐库本地存储

- 使用 SQLite 作为本地音乐库数据库。
- 持久化音乐条目、媒体元数据、文件位置及关联关系。
- 持久化音乐目录配置和索引状态。
- Windows 和 Android 使用各自独立的本地数据库，不进行跨设备同步。
- 不复制或移动用户原始音乐文件。
- 当前已建立 SQLite schema v1：曲目、文件位置、扫描目录、播放历史、播放列表及播放列表条目表；通过 `PRAGMA user_version` 管理 schema 版本。Core Runtime 创建时初始化 `%LOCALAPPDATA%/LNPlayer/library.db`。

## 8. 最近播放记录

- 保存最近播放过的音乐，覆盖音乐库播放和直接播放。
- 记录最后播放时间和累计播放次数。
- 提供最近播放列表。
- 支持清空播放历史。
- 播放历史持久化到 SQLite。

## 9. Windows 视频播放器 UI

### 9.1 当前 UI 方案

- 当前阶段暂时采用 PotPlayer 风格的 UI，包括界面布局、控件组织和交互方式。
- 暂不采用此前讨论的现代化沉浸式 UI 方案。
- 本决定是当前阶段的临时 UI 方案，后续可以重新设计视觉风格；应尽量将界面表现与播放控制逻辑分离，降低更换 UI 风格的成本。
- 参考 PotPlayer 的界面与操作习惯，不要求逐像素复刻，也不意味着必须复制其专有实现。

### 9.2 界面组成

- **视频画面区域**：作为主要显示区域，负责呈现视频画面。
- **标题栏及窗口区域**：显示媒体和窗口信息，提供常规窗口控制。
- **播放控制区域**：提供播放、暂停、进度、音量等常用控制。
- **播放列表区域**：展示当前播放队列，并支持添加、移除和切换媒体。
- **菜单与设置入口**：通过主菜单和视频画面右键菜单访问播放及高级设置功能。

具体控件布局和显示方式暂以 PotPlayer 的常见交互为参考，在 Qt 界面实现阶段细化。

### 9.3 交互方式

参考 PotPlayer 的常见操作逻辑，首期规划包括：

- 右键视频画面打开上下文菜单及多级子菜单。
- 通过键盘快捷键执行播放控制、进度定位、音量调整和全屏切换。
- 通过鼠标操作视频画面、播放进度和音量控制。
- 支持窗口模式与全屏模式切换。
- 支持播放列表浏览、媒体切换及队列管理。
- 具体快捷键映射、鼠标行为、快进/快退步长和菜单项目在实现阶段进一步确认。

### 9.4 功能范围与优先级

| 优先级 | 功能 |
|---|---|
| P0 | 视频播放、暂停、停止、进度定位 |
| P0 | 音量、静音、播放速度 |
| P0 | 全屏、窗口化、画面比例 |
| P0 | 右键菜单、键盘快捷键、鼠标操作 |
| P1 | 多音轨、外挂字幕、内嵌字幕 |
| P1 | 播放列表、上一项、下一项 |
| P1 | 逐帧播放、截图 |
| P2 | 视频滤镜、音频滤镜、字幕样式 |
| P0 | 硬件解码优先及软件解码回退 |
| P2 | 高级视频渲染器和硬件解码高级配置 |

### 9.5 Qt Widgets 职责划分

Windows 视频 UI 可按以下职责组织组件，具体文件和类名在实现阶段确定：

- **VideoWindow**：窗口生命周期、整体布局及窗口模式管理。
- **VideoSurface / VideoContainer**：视频显示区域及画面交互；内部承载原生 Win32 子窗口，作为 D3D11 Renderer 的显示目标。
- **PlaybackControls**：播放控制、进度和音量等界面操作。
- **VideoContextMenu**：右键菜单及子菜单。
- **PlaylistPanel**：播放列表展示和操作。
- **PlayerController**：键盘快捷键、鼠标操作到 Core 播放接口的控制映射。

UI 通过 Core 对外接口执行播放操作，不直接访问 FFmpeg 上下文。Qt 管理视频区域的布局、尺寸和窗口生命周期；原生 Win32 子窗口承载 D3D11 Renderer。Qt GUI 线程不执行 FFmpeg 解码或 D3D11 GPU 呈现。

### 9.6 视频解码与渲染技术要求

#### 9.6.1 通用要求

- 视频解码采用硬件解码优先策略；当设备、驱动、媒体编码参数或运行环境不支持可用硬件解码路径时，允许回退到 FFmpeg 软件解码。
- Core 负责视频帧队列、帧引用生命周期、PTS 调度及与播放时钟的协调；平台 Renderer 负责取得待呈现帧并执行实际 GPU 绘制。
- Video Frame Queue 必须有界。根据播放时钟和显示能力处理过期帧或背压，不能因解码速度高于显示速度而无限积压。
- Renderer 采用 Pull 模型，帧就绪、`asio::steady_timer` 或平台显示回调触发后，经 Runtime `io_context`/对应 `strand` 协调并提交共享 `thread_pool` 获取可呈现帧；不采用独立忙轮询循环。FFmpeg 解码线程不直接调用 UI 或 GPU 呈现接口。
- 正常音视频播放以音频时钟为主时钟，视频按 PTS 与音频时钟同步；无音频流时使用独立视频时钟。
- Seek、切换媒体和关闭 Session 时必须清理或隔离旧时间线帧；暂停时保留最后一帧供静态显示。
- 软件帧和硬件帧均需支持。硬件帧尽可能直接交给 GPU，避免不必要的 GPU→CPU 下载及 CPU→GPU 重传；无法直接互操作时允许必要的数据传输或软件解码回退。
- 硬件帧的引用、所有权、同步、释放时机和队列背压必须明确；GPU 最后一次使用完成前不得释放底层帧资源或允许解码器复用。
- FFmpeg 内部硬件解码资源和平台图形资源不通过通用 C ABI 暴露；平台专用资源交接由对应平台适配层负责。
- 视频帧获取与 GPU 呈现作为受调度任务，由共享 `thread_pool` 执行；通过 Renderer 内部同步机制保护非线程安全的图形上下文，不默认创建独立渲染线程或线程池。不得无约束地让 GPU Present、vsync 等阻塞调用长期占用共享 worker；平台 API 的线程亲和性要求必须单独验证。

#### 9.6.2 Windows：Qt Widgets + Win32 + D3D11

- Qt 6 Widgets 负责播放器窗口、菜单、控制栏、布局和用户交互。
- 视频显示区域使用原生 Win32 子窗口（HWND）承载 D3D11 Renderer；Qt 管理其位置、尺寸及生命周期。
- D3D11 Renderer 管理与 HWND 关联的 Swap Chain；纹理采样、颜色转换和缩放等呈现任务由共享 `thread_pool` 执行，并通过内部同步机制保护 D3D11 Immediate Context 等共享图形上下文。Present 的阻塞特性及线程要求需在实现验证中确认。
- 首期优先采用 FFmpeg D3D11VA 硬件解码，DXVA2 作为兼容性候选。
- Windows Video Output 平台模块优先创建并管理 D3D11 Device，并将同一设备关联到 FFmpeg AVHWDeviceContext 与 D3D11 Renderer，减少跨设备资源复制。实际设备创建顺序和 FFmpeg 接入方式以集成验证为准。
- D3D11VA 硬件帧通过帧引用交给 Renderer；必须保证 GPU 最后一次读取完成后才释放对应帧引用，不能只传递裸纹理指针。
- 窗口 resize 调整 Swap Chain，不重建 Player 或 Playback Session。
- 首期不以 QOpenGLWidget 作为 D3D11VA 纹理的直接呈现路径，避免引入不必要的 D3D11/OpenGL 互操作复杂度。

#### 9.6.3 Android：Kotlin + SurfaceView + MediaCodec

- Kotlin UI 管理视频显示区域，首期优先评估 SurfaceView。
- Surface 创建、尺寸变化和销毁由 Android UI 生命周期通知 Native 平台输出模块；Surface 失效后必须停止向其提交帧。
- 首期优先评估 FFmpeg MediaCodec 硬件解码，但必须验证目标 FFmpeg 版本的 MediaCodec 输出模式、Surface 绑定能力、输出缓冲区释放及呈现时机控制。
- 评估 MediaCodec 直接输出 Surface 与硬件帧交给 Native Renderer 两种路径。前者可减少应用层帧搬运；后者更容易统一 PTS 调度，但需要验证硬件帧与 Android 图形 API 的互操作。
- 不预设所有 MediaCodec 输出都表现为普通 CPU 可读 AVFrame，也不预设 FFmpeg 自动处理任意 Surface 的绑定和显示时机。
- 最终输出路径根据目标 Android 版本、FFmpeg 构建和设备验证结果确定；平台专用资源和 Surface 生命周期由 Android 适配层封装。

## 10. 当前明确不做的内容

当前阶段不包含多端播放状态同步、设备发现与配对、P2P 通信、中心服务器或云端服务、跨设备音乐库同步，以及远程控制其他设备播放。

不得为了未来可能的多端同步而提前引入当前阶段不需要的网络或分布式模块。

## 11. Windows 完整版本功能范围补充

以下功能纳入 Windows 完整版本规划，不再作为 Android 开发前必须重新决定是否纳入的候选项；具体交互、数据模型和参数在对应模块实现前细化：

- 音乐播放列表管理。
- 随机播放与循环播放。
- 按歌曲、专辑、艺术家或流派浏览。
- 按歌曲名、专辑名和艺术家搜索。
- 专辑封面与歌词显示。
- 音频均衡器。
- 音频输出设备选择。
- 后台音频播放与 Windows 系统媒体控制。

无缝切歌、ReplayGain、独占/低延迟输出等未列入上述完整版本基线的扩展能力，仍需单独评估，不因本节自动纳入。

## 12. 待确认事项

### 12.1 音乐库

- 重复音乐内容的识别策略（文件哈希、音频指纹或组合策略）及性能取舍。
- 音乐库浏览维度和搜索能力的首期范围。
- 数据库表结构、迁移策略和播放列表持久化方案。

### 12.2 音频能力

- Windows 音频输出后端。
- Android 音频输出后端。
- 是否需要无缝切歌、ReplayGain、均衡器及独占/低延迟输出。
- 采样率、位深和声道处理策略。

### 12.3 Windows 视频与进程管理

- 主进程/启动器是否常驻，以及子进程创建和回收策略。
- 文件管理器连续打开文件时的进程启动和请求分发方式。
- 进程间通信方式及主进程是否负责窗口进程管理。
- Windows D3D11VA/DXVA2 的实际支持范围、D3D11 Device 共享方式、D3D11 资源同步与 GPU 完成检测。
- Android MediaCodec 的 Surface 绑定、输出缓冲区呈现时机及 Native Renderer 互操作路径。
- 视频帧队列容量、帧引用/释放契约、PTS 调度与无法直接互操作时的传输策略。
- 视频格式及容器支持范围。
- 快进/快退步长、音量步长及快捷键自定义配置。
- PotPlayer 风格界面的具体布局、菜单项目和控件细节。
- 播放列表面板的具体交互和持久化行为。
- 字幕格式、字幕渲染及字幕样式配置范围。

### 12.4 平台集成

- C ABI v1 已冻结公开头文件与基础契约：不透明句柄、每进程唯一 Player、UTF-8 媒体源、固定返回码、快照、事件回调和线程局部错误读取。接口细节见 `core/include/lnplayer.h`。
- C ABI 的唯一实例生命周期、Playback Controller 命令入口、open/seek 异步完成事件、同步完成型接口等待、线程安全快照、音量接口及 Windows 视频帧 Pull 接口已实现；FFmpeg 媒体后端已接入。视频文件的音轨枚举和切换通过 C ABI 提供独立接口；切换要求处于 Ready 或 Paused 状态。
- Android JNI 接口边界和 Native 生命周期管理。
- Android 后台播放、前台服务、通知和 MediaSession 要求。
- Windows 系统媒体按键、任务栏控制及快捷键要求。
- 两个平台的音频设备选择和音频焦点处理要求。

### 12.5 工程与依赖

- Qt 6 的具体版本。
- FFmpeg 的版本、构建方式、链接方式及许可证分发策略。
- Android NDK、SDK 和最低 Android 版本。
- Android 目标 ABI。
- Windows 最低支持版本及目标架构。
- 安装包、更新和发布方式。

## 13. 模块边界

以下为逻辑模块边界，具体拆分方式随实现推进：

- **Player Core（C++）**：统一播放控制、播放状态和播放生命周期。
- **Media（C++ / FFmpeg）**：媒体探测、解封装、解码及音视频数据处理。
- **Music Library（C++ / SQLite）**：本地文件索引、元数据和音乐库查询。
- **Playlist（C++）**：播放队列与播放列表逻辑。
- **Platform Audio Output**：封装 Windows、Android 各自的音频输出实现。
- **Platform Video Output**：封装 Windows 等平台的视频帧输出与渲染对接。
- **Windows Audio UI（Qt 6 Widgets）**：Windows 音频播放器界面。
- **Windows Video UI（Qt 6 Widgets）**：Windows 视频播放器界面，当前阶段暂时采用 PotPlayer 风格的 UI 和交互方式；视觉风格未来可调整。
- **Android UI（Kotlin）**：移动端交互和系统 UI 集成。
- **Android JNI Bridge**：连接 Kotlin 与 Native Core 的平台桥接层。

模块是否需要独立成库，应根据接口和依赖关系决定，不为拆分而拆分。


## 14. 需求与实现原则

1. 先确认功能行为和模块边界，再实现具体功能。
2. 已确认技术选型、工程约定与候选建议必须明确区分。
3. 不为了未来可能的多端同步而提前增加网络协议或分布式架构。
4. 两个平台尽可能复用播放、媒体处理和音乐管理逻辑；平台 UI 与系统集成保持原生。
5. Windows 音频与视频采用不同 UI，但属于同一个 LNPlayer 应用程序。
6. Windows 每个播放器窗口运行在独立子进程中；每个子进程只有一个 Player，并独立持有媒体处理上下文。
7. Android 每个应用进程只有一个 Player。
8. C ABI 使用普通同步函数调用，但各接口根据提交、同步完成和快速查询语义定义返回时机。
9. `play/pause/stop/close` 必须等待操作完成后返回；`open/seek` 首期采用提交后返回并通过完成事件报告最终结果。
10. 本文随需求确认持续更新，并作为后续实现的需求依据。

- [x] Windows D3D11VA decoded-frame lifetime retention and D3D11 Video Processor presentation for NV12/P010 surfaces.
- [x] Keep the software BGRA rendering path for software-decoded video.
- [x] Add on-demand FFmpeg CPU transfer fallback when D3D11 Video Processor initialization or presentation fails.
- [x] Capture the retained BGRA output texture for screenshots on the GPU hardware-frame path.
- [x] Composite external subtitle overlays on the GPU hardware-frame path.
- [x] Detect D3D11 renderer device recreation, re-inject a new device, or detach it and force software decoding when renderer initialization fails. Rebuild the media backend while restoring playback position/state. Runtime validation on real GPUs and drivers remains pending.
