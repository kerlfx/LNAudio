# LNPlayer 软件架构设计

> 状态：架构初步设计（部分边界已确认，内部对象模型、线程与输出实现仍待细化）  
> 关联需求：[`docs/REQUIREMENTS.md`](./REQUIREMENTS.md)

本文定义 LNPlayer 的整体分层、模块职责、平台运行模型和依赖方向。尚未确认的设计以“建议”标识，不应视为已经冻结的实现约束。

## 1. 架构目标与约束

- 面向 Windows 和 Android 的本地媒体播放器。
- 当前阶段不实现多端同步、设备发现、P2P、中心服务器或远程控制。
- Windows 与 Android 复用 C++ 播放器核心和媒体处理逻辑，平台 UI 与系统集成保持原生。
- Core 以动态库形式交付：Windows DLL、Android SO；对平台提供 C ABI。
- Core 内部使用 C++20，不依赖 Qt、Kotlin 或 JNI。
- FFmpeg 负责媒体探测、解封装和解码。
- SQLite 用于本地音乐库及相关持久化数据。
- UI 不直接操作 FFmpeg 上下文，也不承担媒体解码工作。
- 平台音频输出、视频渲染与共享播放控制逻辑分离。

## 2. 整体分层

~~~mermaid
flowchart TD
    subgraph Platform["平台应用层"]
        WA["Windows Audio UI<br/>Qt Widgets"]
        WV["Windows Video UI<br/>Qt Widgets"]
        AU["Android UI<br/>Kotlin"]
        PI["平台系统集成"]
    end
    subgraph Bridge["平台桥接层"]
        CABI["Core C ABI"]
        JNI["Android JNI Bridge"]
    end
    subgraph Core["共享 Player Core（C++20）"]
        PM["Player Manager"]
        PC["Playback Controller"]
        PS["Playback Session"]
        PL["Playlist"]
        ML["Music Library"]
        EM["Event Manager"]
    end
    subgraph Media["媒体处理层"]
        MP["Media Pipeline"]
        FF["FFmpeg"]
    end
    subgraph Output["平台输出适配层"]
        AO["Audio Output"]
        VO["Video Output"]
    end
    subgraph Storage["本地数据层"]
        DB["SQLite"]
        FS["本地媒体文件"]
    end
    WA --> CABI
    WV --> CABI
    AU --> JNI --> CABI
    PI -.-> AO
    PI -.-> VO
    CABI --> PM
    PM --> PC
    PM --> PL
    PM --> ML
    PM --> EM
    PC --> PS
    PS --> MP --> FF
    MP --> AO
    MP --> VO
    ML --> DB
    MP --> FS
~~~

### 2.1 平台应用层

- Windows Audio UI：音频播放器界面。
- Windows Video UI：视频播放器界面，当前阶段暂时采用 PotPlayer 风格 UI 与交互。
- Android UI：Kotlin 原生界面。
- 平台系统集成：文件选择、生命周期、系统媒体控制、音频焦点等平台能力。

### 2.2 平台桥接层

- Windows UI 通过 Core C ABI 调用共享核心。
- Android 通过 JNI Bridge 将 Kotlin 调用转发到 Core C ABI。
- JNI 只负责类型转换、句柄管理和调用桥接，不承载播放业务逻辑。
- 平台 UI 不直接依赖 Core 内部 C++ 类。

### 2.3 共享核心层

负责播放器实例生命周期、播放命令、播放状态、播放队列、音乐库访问和事件通知。Core 不创建平台窗口，也不依赖平台 UI 框架。

### 2.4 媒体处理层

负责媒体探测、解封装、音视频解码、时间戳处理、缓冲和音视频同步。FFmpeg 的上下文属于具体媒体播放会话，不在不同播放器实例之间共享。

### 2.5 平台输出适配层

- Audio Output：接收 Core/媒体管线提供的 PCM 音频数据并提交给平台音频设备。
- Video Output：接收解码后的视频帧并完成平台渲染对接。
- 输出实现通过抽象接口与共享播放逻辑隔离；具体 Windows、Android API 和渲染后端另行确定。

### 2.6 本地数据层

SQLite 保存音乐条目、元数据、文件位置关联、目录配置、索引状态和播放历史等结构化数据。原始媒体文件仍由用户文件系统管理，播放器不因导入或去重而移动、复制或删除原始文件。

## 3. 逻辑模块职责

| 模块 | 主要职责 | 不负责 |
|---|---|---|
| Application / Platform Integration | 平台入口、应用生命周期、系统 API 接入 | 共享播放业务 |
| Player Manager | 创建、销毁和管理 Player 实例 | 媒体解码细节 |
| Player Instance | 聚合单个播放器实例的控制、状态和资源 | 平台窗口创建 |
| Playback Controller | 接收播放命令并协调状态转换 | 直接操作 UI 控件 |
| Playback Session | 管理一次媒体播放所需的资源和状态 | 音乐库全局索引 |
| Media Pipeline | 探测、解封装、解码、同步和缓冲 | 平台窗口生命周期 |
| Audio Output | 音频设备输出及设备相关处理 | 音乐库管理 |
| Video Output | 视频帧呈现及渲染资源管理 | 播放列表业务 |
| Playlist | 播放队列、条目顺序和切换策略 | FFmpeg 解码 |
| Music Library | 音乐条目、元数据、文件位置及查询 | 原始文件搬移 |
| Storage | SQLite 访问、事务和数据迁移 | UI 展示 |
| Event Manager | 将 Core 状态和事件通知给调用方 | 跨进程窗口管理 |

上述为逻辑职责边界，不要求每个模块立即拆成独立动态库或静态库。物理拆分应由依赖关系和构建需求决定。

## 4. Player 对象模型

初步建议采用以下关系：

- Player Manager 管理多个 Player Instance。
- 每个 Player Instance 代表一个独立播放器运行实例。
- Player Instance 持有或协调 Playback Controller、当前 Playback Session、播放队列及事件出口。
- Playback Session 封装一次媒体播放所需的媒体输入、FFmpeg 上下文、解码状态、时钟和缓冲资源。
- 媒体切换时，旧会话资源应按明确的生命周期释放，新媒体建立新的会话资源。
- 不同 Player Instance 不得共享同一套可变 FFmpeg 播放上下文。
- 多个实例可以共享只读配置、不可变数据或 FFmpeg 库级公共资源，但不得因此共享媒体会话状态。

建议初期一个 Player Instance 同时只运行一个当前 Playback Session。是否需要预加载会话、无缝切换或多路媒体会话，留待播放需求和性能设计阶段确定。

## 5. Windows 运行模型

### 5.1 进程与窗口

已确认规则：

- 音频 UI 和视频 UI 属于同一个 LNPlayer 应用程序，使用同一应用程序可执行文件。
- 允许同时运行多个进程，不强制单实例。
- 每创建一个播放器窗口，启动一个独立子进程；一个窗口对应一个播放器子进程。
- 每个子进程创建并管理自己的窗口、Player Instance 和媒体处理资源。
- 每个子进程拥有独立 Qt GUI 主线程。
- QWidget 及其他 GUI 对象只在所属 GUI 主线程操作。
- 媒体解码等耗时任务不在 GUI 主线程执行。
- 关闭窗口只结束对应播放器子进程，不影响其他窗口进程。

~~~mermaid
flowchart TD
    A["文件打开 / 应用启动"] --> B["LNPlayer.exe"]
    B --> C{"本次启动的媒体类型"}
    C -->|音频| D["音频窗口子进程"]
    C -->|视频| E["视频窗口子进程"]
    D --> F["Qt GUI 主线程"]
    E --> G["Qt GUI 主线程"]
    F --> H["独立 Player Instance"]
    G --> I["独立 Player Instance"]
    H --> J["独立 Playback Session / FFmpeg"]
    I --> K["独立 Playback Session / FFmpeg"]
~~~

### 5.2 尚未确定

- 是否存在常驻主进程/启动器。
- 子进程的创建、回收和异常退出处理策略。
- 文件管理器连续打开文件时，是否复用已有窗口或始终创建新窗口。
- 如需向已有窗口发送打开请求，采用何种 IPC。
- 启动参数和媒体类型识别规则。

在这些事项确认前，不将某种 IPC 或常驻进程模型写成既定架构。

## 6. Android 运行模型

建议 Android 首期采用单应用进程模型：

- Kotlin UI 管理 Activity、界面状态和 Android 生命周期。
- JNI Bridge 负责 Kotlin 与 Native Core 之间的调用。
- Core 管理 Player Instance 和媒体会话。
- 平台适配层负责 Android 音频输出、视频渲染和系统媒体集成。
- Android 后台播放、前台服务、MediaSession 和音频焦点策略仍需单独确认。

## 7. 线程与并发原则

当前只确认职责隔离，线程数量和调度策略尚未冻结。

| 执行职责 | 约束 |
|---|---|
| UI 主线程 | 处理窗口、控件和用户交互；不得执行耗时解码 |
| 播放命令执行 | 串行化同一 Player Instance 的状态变更，具体执行器待设计 |
| Demux | 执行媒体读取和解封装 |
| Audio Decode | 执行音频解码 |
| Video Decode | 执行视频解码 |
| Audio Output | 满足平台音频 API 的实时输出要求 |
| Video Render | 在平台允许的渲染线程/上下文中呈现视频帧 |

这些是逻辑职责，不意味着必须一项对应一个线程。后续结合 FFmpeg、平台音频 API、Qt 渲染机制和实际性能确定线程模型。

跨线程通信应定义清晰的命令、事件和资源所有权规则。不得从 Core 工作线程直接操作 Qt Widgets。

## 8. Core 接口与事件

已确认 Core 对外提供 C ABI，公共头文件位于 `core/include/lnplayer.h`。

接口设计阶段需要明确：

- Player 句柄的创建、销毁和有效期。
- 函数调用的线程安全约束。
- 字符串、缓冲区和媒体数据的内存所有权。
- 错误码及错误信息获取方式。
- 播放状态查询与事件回调。
- 回调线程、回调期间允许执行的操作以及注销规则。
- 异步命令的完成通知和取消语义。

建议采用“命令调用 + 事件通知”的交互方式：UI 发送播放命令，Core 在状态变化时发出事件；UI 不依赖高频轮询获取全部播放状态。具体 API 和回调 ABI 尚未设计完成。

## 9. 依赖方向与边界

目标依赖方向：

- 平台 UI → Core C ABI。
- Android UI → JNI Bridge → Core C ABI。
- Core 内部业务模块 → Core 抽象接口。
- Media Pipeline → FFmpeg。
- Media Pipeline → Audio/Video Output 抽象接口。
- 平台输出实现 → 对应平台 API。
- Music Library → Storage / SQLite。

禁止的依赖：

- Core → Qt Widgets。
- Core → Kotlin 或 JNI。
- UI → FFmpeg 内部上下文。
- 共享播放逻辑 → 某个具体平台窗口类。
- 音乐库去重逻辑 → 修改或删除用户原始媒体文件。

## 10. 关键设计待决事项

1. Player Manager、Player Instance、Playback Controller、Playback Session 的精确对象关系和生命周期。
2. 一个 Player Instance 是否始终只允许一个活动 Playback Session，以及预加载/无缝切换策略。
3. C ABI 的完整函数集合、句柄、错误码、内存所有权和回调约定。
4. 播放命令执行器、媒体工作线程、线程池及队列模型。
5. FFmpeg 上下文的创建、销毁、Seek、Flush 和中断机制。
6. 音视频时钟、同步、缓冲和丢帧策略。
7. Windows 与 Android 音频输出后端。
8. Windows 视频渲染后端及 Qt 图形资源对接方式。
9. Android 视频 Surface 生命周期与 Native 渲染资源管理。
10. SQLite 访问线程模型、事务边界和迁移策略。
11. Windows 多进程启动、文件打开分发和 IPC 方案。
12. Android 后台播放和系统媒体控制架构。

## 11. 后续设计顺序

1. Player Core 对象模型与生命周期。
2. C ABI 接口和事件系统。
3. FFmpeg 媒体管线与音视频同步。
4. 音频输出和视频渲染适配。
5. 线程、命令队列与资源所有权。
6. 音乐库、SQLite 和文件扫描架构。
7. Windows 多进程启动与文件请求分发。
8. Windows/Android 平台 UI 与系统集成。
9. 工程目录、依赖管理和构建配置。

每完成一个专题，应同步更新本文和需求基线，并明确区分已确认决策与待验证方案。
