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

## 4. Player 对象模型与生命周期

### 4.1 对象职责与所有权

建议采用以下所有权关系：

- **Player Manager**：管理当前 Core Runtime 中创建的 Player Instance，负责实例创建、销毁和句柄登记。它不持有某个实例的媒体解码状态。
- **Player Instance**：代表一个逻辑播放器，拥有独立的播放状态、Playback Controller、Playlist、当前 Playback Session 和事件出口。
- **Playback Controller**：属于一个 Player Instance，串行协调该实例的播放命令和状态转换。它不直接承担 FFmpeg 解码工作。
- **Playlist**：属于 Player Instance，保存队列条目、当前条目位置及播放顺序策略。播放列表条目是媒体引用/描述，不持有打开中的 FFmpeg 上下文。
- **Playback Session**：属于 Player Instance，代表当前打开的一次媒体播放。持有媒体输入、FFmpeg format/codec 上下文、解码器、时钟、数据队列及会话级资源。
- **Media Pipeline**：由 Playback Session 创建和管理，负责探测、解封装、解码和同步；平台输出通过抽象接口接入。

Player Manager 与 Player Instance 是管理关系；Controller、Playlist 和 Session 是实例内部组成部分。不要把 Controller 或 Session 暴露成独立的顶层播放器实例。

~~~mermaid
classDiagram
    class PlayerManager {
        +createPlayer()
        +destroyPlayer()
    }
    class PlayerInstance {
        +id
        +state
    }
    class PlaybackController {
        +open()
        +play()
        +pause()
        +stop()
        +seek()
    }
    class Playlist {
        +add()
        +remove()
        +move()
        +select()
    }
    class PlaybackSession {
        +open()
        +start()
        +close()
    }
    class MediaPipeline {
        +demux()
        +decode()
        +flush()
    }
    PlayerManager "1" --> "0..*" PlayerInstance
    PlayerInstance "1" *-- "1" PlaybackController
    PlayerInstance "1" *-- "1" Playlist
    PlayerInstance "1" *-- "0..1" PlaybackSession
    PlaybackSession "1" *-- "1" MediaPipeline
~~~

### 4.2 实例数量与会话数量

- 一个 Player Instance 同一时刻最多拥有一个当前活动 Playback Session。
- 一个 Player Instance 可以在没有活动 Session 时存在，例如刚创建、尚未打开媒体或停止并关闭当前媒体之后。
- 一个 Player Instance 可以持有多个 Playlist 条目，但队列中的其他条目不会因此提前创建 FFmpeg 上下文。
- 切换到另一个媒体时，Controller 协调当前 Session 关闭，再为目标媒体建立新 Session。
- 首期不设计同一 Player Instance 内多个独立媒体同时播放；若未来需要预加载，可增加独立的准备态资源，但不能与当前活动 Session 混淆。

Windows 已确定一个窗口一个进程；建议每个 Windows 窗口进程创建一个 Player Instance。Core API 仍保留创建多个实例的能力，不把 Windows 的进程策略写死到共享 Core。Android 是否在单进程中创建多个实例，由 Android 产品行为决定。

### 4.3 Player Instance 生命周期

建议生命周期如下：

~~~mermaid
stateDiagram-v2
    [*] --> Created
    Created --> Idle: 初始化完成
    Idle --> Opening: 打开媒体
    Opening --> Ready: 探测及初始化成功
    Opening --> Error: 打开失败
    Ready --> Playing: 开始播放
    Playing --> Paused: 暂停
    Paused --> Playing: 恢复
    Playing --> Seeking: 定位
    Paused --> Seeking: 定位
    Seeking --> Playing: 定位完成且原状态为播放
    Seeking --> Paused: 定位完成且原状态为暂停
    Playing --> Ended: 播放结束
    Ended --> Playing: 从头重播
    Ready --> Idle: 关闭媒体
    Playing --> Idle: 停止并关闭媒体
    Paused --> Idle: 停止并关闭媒体
    Error --> Idle: 清理并返回空闲
    Idle --> Closing: 销毁实例
    Error --> Closing: 销毁实例
    Closing --> [*]
~~~

说明：

- Created/Idle 表示实例存在，但没有活动媒体会话。
- Opening 表示正在创建会话、打开输入并探测媒体；此阶段可取消。
- Ready 表示媒体已成功打开并完成播放所需初始化，尚未开始或当前未运行。
- Playing/Paused 表示播放运行状态。
- Seeking 是一次异步定位过程；定位完成后恢复到定位前的播放/暂停状态。
- Ended 表示当前媒体自然播放结束，会话仍可保留以支持重播或读取最终状态。
- Error 表示当前打开或播放操作失败；错误处理后允许清理并再次打开媒体。
- Closing 表示实例正在销毁，不再接受新的播放命令。

状态图是逻辑状态模型。具体实现可将 Seeking 等短暂操作表示为操作状态而非顶层枚举，但对外行为必须保持一致。

### 4.4 命令语义

| 命令 | 建议语义 |
|---|---|
| create | 创建空闲 Player Instance |
| open(source) | 异步打开媒体；成功后进入 Ready，失败进入 Error |
| play | 从 Ready/Paused 恢复播放；Ended 时按重播策略处理 |
| pause | 暂停当前播放；没有活动会话时返回明确错误 |
| stop | 停止当前播放并释放当前媒体会话，回到 Idle；播放列表保留 |
| seek(position) | 发起异步定位；完成后恢复定位前的播放/暂停状态 |
| close | 关闭当前媒体会话并回到 Idle；与销毁 Player Instance 不同 |
| destroy | 停止接受命令，清理会话及实例资源并销毁实例 |

所有命令的错误码、同步/异步返回和取消语义由 C ABI 专题进一步确定。命令不能因异步执行而丢失顺序；同一 Player Instance 的状态转换由其 Controller 统一协调。

### 4.5 打开、切换与销毁顺序

建议媒体打开流程：

1. Controller 接收 open 命令并登记操作序号。
2. 若存在旧 Session，先使其停止接收新工作并执行关闭。
3. 创建新的 Playback Session。
4. 打开媒体输入、读取媒体信息并初始化所需解码器和输出资源。
5. 初始化成功后将新 Session 设为当前 Session，状态进入 Ready。
6. 通过事件通知调用方打开完成及媒体信息。

若打开失败，释放新 Session 已创建的资源并发出错误事件；不得留下半初始化的活动会话。

销毁顺序应遵循资源依赖的逆序：停止命令入口 → 取消/中断媒体读取与后台任务 → 停止输出 → 等待相关工作退出 → 释放解码器和格式上下文 → 释放队列及时钟 → 注销事件出口 → 销毁实例。具体线程 join 和中断机制在媒体管线及线程专题确定。


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

## 8. Core C ABI 接口与事件系统

Core 对外提供稳定的 C ABI，公共头文件位于 `core/include/lnplayer.h`。C ABI 是 Windows Qt UI 与 Android JNI Bridge 共同使用的边界；内部 C++ 类型、FFmpeg 上下文和线程对象均不得泄漏到该边界。

### 8.1 Runtime 与 Player 句柄

建议采用两级句柄：

- **Runtime Handle（`ln_runtime_t`）**：不透明句柄，代表一个 Core 运行环境，负责实例登记、模块级资源和事件分发。
- **Player Handle（`ln_player_t`）**：建议使用 `uint64_t` ID，由 Runtime 分配并在该 Runtime 内唯一，不暴露 Player Instance 的 C++ 指针。
- 一个 Runtime 可以创建多个 Player；每个 Player 对应一个独立 Player Instance，拥有自己的 Controller、Playlist、Playback Session 和事件出口。
- Player 销毁后，其 ID 失效；后续调用返回明确的无效句柄错误。ID 不应在同一 Runtime 生命周期内重新分配给另一个实例，以避免迟到命令误作用于新实例。
- Runtime 销毁前必须先销毁其所有 Player，或由 Runtime 按确定的顺序统一销毁它们；具体采用哪种 API 契约应在头文件定稿时固定。

不建议把内部 `PlayerInstance*` 直接作为公开句柄。整数 ID 更适合跨 JNI 保存和传递，也避免调用方解引用内部对象地址。Runtime 本身使用不透明指针句柄，调用方必须遵守创建/销毁配对规则，销毁后不得继续使用。

### 8.2 API 分组

建议公开 API 按职责分组，而不是暴露内部类方法：

| API 分组 | 典型操作 | 语义 |
|---|---|---|
| Runtime | create、destroy、get_version | 初始化和关闭 Core 运行环境 |
| Player | create、destroy | 创建和销毁播放器实例 |
| Event | set_callback、clear_callback | 注册或注销事件接收端 |
| Playback Command | open、play、pause、stop、seek、close | 提交播放操作 |
| Playback Query | get_state、get_position、get_duration、get_media_info | 获取当前状态快照 |
| Error | error_code_to_string、get_last_error（如确有需要） | 获取错误说明 |

API 命名统一使用 `ln_...` 前缀。导出函数使用 C linkage，并明确导出宏、调用约定和固定宽度整数类型。公共头文件只包含 C ABI 所需的标准 C 头文件，保证可由 C 和 C++ 编译器包含。

### 8.3 同步提交与异步执行

播放命令采用“同步提交、异步执行”模型：

1. UI 调用命令 API。
2. Core 检查句柄、参数和当前生命周期状态。
3. 如果命令可以接受，Core 分配一个 `operation_id` 并将命令提交给该 Player 的 Controller。
4. API 返回提交结果及操作编号；这不代表媒体操作已经完成。
5. Controller 按该 Player 的命令顺序执行操作。
6. 操作完成后，Core 发出对应完成事件，携带相同的 `operation_id` 和最终结果。

必须区分**提交结果**和**执行结果**。例如，`open` API 返回成功只表示打开请求已被接受；媒体是否成功打开，以 `OPEN_COMPLETED` 事件为准。

同一 Player 的状态变更命令按提交顺序协调；不同 Player 允许并发执行。查询 API 返回调用时刻的一致状态快照，不应等待媒体解码或网络/文件读取完成。首期不要求所有查询都产生异步事件。

建议首期命令语义如下：

| 命令 | 提交后的行为 |
|---|---|
| open(source) | 异步打开媒体，完成后进入 Ready 或 Error |
| play | 请求开始或恢复播放 |
| pause | 请求暂停 |
| stop | 停止播放、关闭当前 Session，回到 Idle，保留 Playlist |
| seek(position) | 异步定位，完成后恢复定位前的播放/暂停状态 |
| close | 关闭当前媒体 Session，回到 Idle，但不销毁 Player |

参数错误、无效句柄、命令无法提交等情况通过 API 的同步返回码报告；已经接受的操作，其最终成功或失败通过事件报告。命令取消 API、命令替换策略和销毁期间的取消细节仍需在实现前进一步确定。

### 8.4 事件结构与类型

建议采用统一事件结构，至少包含：

| 字段 | 含义 |
|---|---|
| struct_size | 结构体大小，用于 ABI 版本扩展 |
| type | 事件类型 |
| player | 事件所属 Player ID |
| sequence | 该 Player 内单调递增的事件序号 |
| operation_id | 关联的异步操作编号；非操作事件为 0 |
| result | 操作结果或错误码 |
| data / data_size | 可选的事件负载及其字节数 |

首期事件类型建议包括：

- `STATE_CHANGED`：Player 状态变化。
- `OPEN_COMPLETED`：媒体打开完成，携带成功/失败结果。
- `SEEK_COMPLETED`：定位完成。
- `PLAYBACK_ENDED`：媒体自然播放结束。
- `ERROR`：播放过程中发生错误。
- `MEDIA_INFO_READY`：媒体信息可供查询或展示。
- `POSITION_UPDATED`：可选的位置更新事件，频率需要限制，避免高频事件挤占其他控制事件。

事件负载使用有明确布局和版本规则的 C ABI 结构体，不传递 C++ 对象、STL 容器或 FFmpeg 类型。扩展结构体时保留 `struct_size`，新增字段只能追加，不能改变既有字段的含义和布局。

### 8.5 回调线程与事件顺序

- Core 不在 Qt GUI 线程或 Android UI 线程执行事件回调。
- 回调由 Core 自己的事件分发机制调用，平台层负责转发到 UI 线程。
- 同一 Player 的事件按序分发，且同一 Player 的回调不并发执行；不同 Player 的回调允许并发执行。
- `sequence` 在同一 Player 内单调递增，便于调用方识别事件顺序和排查遗漏。
- 回调应快速返回，不执行耗时工作，不等待同一 Player 的异步操作完成，也不直接操作 UI 控件。
- Qt 桥接层使用 queued connection 将事件转发到 GUI 线程；Android JNI Bridge 将事件转发到合适的 Kotlin 调度上下文，再由主线程更新 UI。
- 不得假定回调来自固定线程，也不得假定不同 Player 的回调运行在同一个线程。

位置更新属于高频信息，建议通过可配置频率的事件或 UI 定时查询获取；状态变化、错误和操作完成事件必须优先且不能被位置更新淹没。

### 8.6 内存与字符串所有权

跨 C ABI 的内存规则必须明确且统一：

- 输入字符串使用 UTF-8。调用方提供的输入缓冲区只需在函数调用期间有效；Core 若需异步使用，必须在返回前复制所需内容。
- 输出数据优先采用调用方提供缓冲区的方式，并通过容量/实际长度参数报告所需空间，避免跨 DLL/SO 边界分配后由另一侧释放。
- 不向调用方返回需要由调用方使用 `free`、`delete` 或平台堆函数释放的 Core 内部内存。
- 事件结构和事件负载只在回调执行期间有效；需要跨回调保存的数据由调用方复制。
- 不在 ABI 中传递 `std::string`、`std::vector`、智能指针、引用或异常对象。

如果未来确实需要 Core 分配并返回大块数据，应提供配对的 Core 释放函数，且只能由该释放函数释放对应内存；首期没有必要引入这套复杂度。

### 8.7 错误码与错误信息

建议使用固定宽度整数定义稳定错误码，并按类别划分：

- 通用参数错误、无效句柄、资源状态错误。
- Runtime/Player 生命周期错误。
- 命令提交失败或当前状态不允许执行。
- 媒体输入、格式探测、解码和输出错误。
- 内存、内部资源和未知错误。

同步 API 返回码表示调用/提交阶段结果；异步操作的最终错误通过完成事件的 `result` 传递。错误码应稳定且可枚举，不能直接把 FFmpeg 内部错误值作为公开 ABI 错误码。需要展示的错误文本应通过稳定的错误码转换接口获得，或由错误事件提供 UTF-8 描述。

### 8.8 回调注销与对象销毁

- 注册回调时允许调用方传入 `user_data`，Core 只保存指针，不取得其内存所有权。
- 注销回调后，不再启动新的回调；同步注销需要等待已开始的回调退出，之后调用方才可释放 `user_data`。
- 不允许在该 Player 自身的回调中调用会等待该回调退出的同步注销或销毁操作，否则可能发生自等待死锁。
- Player 销毁时停止接受新命令，取消或终止未完成工作，停止事件分发并等待在途回调退出，然后释放 Session、Controller 和其他实例资源。
- Runtime 销毁必须保证其事件分发资源和所有 Player 均已停止，不能遗留回调访问已释放的桥接对象。

回调是通知机制，不拥有 Player 生命周期。平台层应在自身退出流程中明确注销回调并销毁 Player。

### 8.9 平台桥接约束

Windows Qt 与 Android JNI 共用同一套 C ABI 语义：

- Windows Qt 直接调用 C ABI；事件回调只负责将数据复制/封装后投递到 Qt GUI 线程。
- Android Kotlin 不直接持有 Core 内部对象；JNI Bridge 负责句柄映射、字符串转换、事件转发和线程切换。
- JNI 不重新实现播放状态机、命令顺序或媒体生命周期。
- 两个平台对提交结果、异步完成事件、错误码和内存有效期的理解必须一致。

本节定义的是接口契约和建议模型，不代表具体函数签名、枚举数值和结构体布局已经冻结。正式实现前应据此设计并审查 `core/include/lnplayer.h`。

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

1. Player 生命周期状态与命令语义的最终确认，以及打开失败、取消和销毁时序。
2. 一个 Player Instance 是否始终只允许一个活动 Playback Session，以及预加载/无缝切换策略。
3. C ABI 具体函数签名、枚举数值、结构体布局，以及命令取消和回调注销契约。
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

1. 复核并冻结 Player Core 对象模型与生命周期。
2. 根据 C ABI 契约设计并审查公共头文件 `core/include/lnplayer.h`。
3. FFmpeg 媒体管线与音视频同步。
4. 音频输出和视频渲染适配。
5. 线程、命令队列与资源所有权。
6. 音乐库、SQLite 和文件扫描架构。
7. Windows 多进程启动与文件请求分发。
8. Windows/Android 平台 UI 与系统集成。
9. 工程目录、依赖管理和构建配置。

每完成一个专题，应同步更新本文和需求基线，并明确区分已确认决策与待验证方案。
