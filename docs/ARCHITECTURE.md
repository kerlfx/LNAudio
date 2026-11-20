# LNPlayer 软件架构设计

> 状态：架构设计持续细化（视频输出总体边界已确定，平台资源交接细节仍需验证）  
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
        PM["Runtime（唯一 Player）"]
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

- Windows Qt UI 在 GUI 主线程直接调用 C ABI。提交型接口快速返回；查询型接口只读取可立即取得的状态或缓存，不执行耗时工作。
- Android Kotlin UI 通过 JNI 直接调用相同的 C ABI。提交型接口快速返回；可能耗时的查询或数据处理由 Android 后台机制承载，不能阻塞 Main Thread。
- C ABI 的提交型调用与业务执行异步完成是两回事：调用本身同步返回提交结果，Core 后台完成操作后通过独立事件通知。
- UI 不需要专用 Standalone Asio 调度层，也不需要为了普通提交型接口建立额外 UI 工作线程。
- Android JNI Bridge 只负责类型转换、句柄管理和调用桥接，不承载播放业务逻辑。
- 平台 UI 不直接依赖 Core 内部 C++ 类，也不要求 Android UI 引入 Standalone Asio。

### 2.3 共享核心层

负责播放器实例生命周期、播放命令、播放状态、播放队列、音乐库访问和事件通知。Core 不创建平台窗口，也不依赖平台 UI 框架。

### 2.4 媒体处理层

负责媒体探测、解封装、音视频解码、时间戳处理、缓冲和音视频同步。FFmpeg 的上下文属于具体媒体播放会话，不在不同播放器实例之间共享。

### 2.5 平台输出适配层

- Audio Output：接收 Core/媒体管线提供的 PCM 音频数据并提交给平台音频设备。
- Video Output：管理视频帧输出抽象、帧交付和平台渲染器之间的生命周期边界；Core 负责帧队列与播放时钟调度，平台 Renderer 负责 GPU 呈现。
- 视频解码采用硬件解码优先策略；硬件解码不可用或不支持当前媒体时允许回退软件解码。
- 渲染任务由帧就绪、显示节奏或平台呈现时机触发，经 Runtime `io_context` 协调后提交到共享 `thread_pool`；不默认创建独立渲染线程或渲染线程池。解码任务不直接调用 UI 或 GPU 呈现接口。
- 输出实现通过平台输出抽象与共享播放逻辑隔离。平台图形设备、Surface 和渲染资源由对应平台适配层管理，不暴露为通用 C ABI 类型。

### 2.6 本地数据层
#### 2.6.1 SQLite 音乐库存储实现

当前 Core 内部音乐库存储模块为 `MusicLibrary`，实现位于 `core/music_library.h/.cpp`，使用 SQLite Amalgamation 提供的 `sqlite3.h` API。

- 数据库默认位置：Windows `%LOCALAPPDATA%/LNPlayer/library.db`；其他平台使用 `XDG_DATA_HOME` 或用户数据目录。
- 数据库启用外键、WAL 日志模式和 5 秒 busy timeout。
- 使用 `PRAGMA user_version` 管理 schema 版本；当前初始 schema 版本为 1。
- `tracks` 保存逻辑曲目及元数据、内容哈希、累计播放次数和最近播放时间。
- `file_locations` 保存一个曲目的一个或多个文件位置、文件大小和修改时间。一个曲目可以关联多个路径；移除单一路径不应直接删除曲目或其他路径。
- `library_directories` 保存用户配置的扫描目录和递归设置。
- `playback_history` 保存播放历史。
- `playlists` 与 `playlist_items` 保存播放列表及其条目顺序。
- `MusicLibrary` 对单个 SQLite 连接的访问使用内部互斥锁；多条相关写入使用事务。
- 原始媒体文件由用户管理，音乐库只记录路径和元数据，不复制、移动或删除源文件。

当前实现提供数据库初始化、扫描目录配置写入、递归/非递归目录遍历、FFmpeg 音频流探测与标签读取、文件 SHA-256 去重、曲目/路径 upsert、播放历史写入和曲目搜索接口。Windows 音频模式提供目录导入入口和音乐库搜索/列表浏览，双击条目可直接打开文件。文件监控、删除/重命名同步、启动补偿扫描、最近播放 UI、播放列表编辑和单文件导入仍未完成。


SQLite 保存音乐条目、元数据、文件位置关联、目录配置、索引状态和播放历史等结构化数据。原始媒体文件仍由用户文件系统管理，播放器不因导入或去重而移动、复制或删除原始文件。

## 3. 逻辑模块职责

| 模块 | 主要职责 | 不负责 |
|---|---|---|
| Application / Platform Integration | 平台入口、应用生命周期、系统 API 接入 | 共享播放业务 |
| Runtime | 创建、销毁并管理进程内唯一 Player 及运行资源 | 媒体解码细节 |
| Player Instance | 聚合进程内唯一播放器的控制、状态和资源 | 平台窗口创建 |
| Playback Controller | 接收播放命令并协调状态转换 | 直接操作 UI 控件 |
| Playback Session | 管理一次媒体播放所需的资源和状态 | 音乐库全局索引 |
| Media Pipeline | 探测、解封装、解码、同步和缓冲 | 平台窗口生命周期 |
| Audio Output | 音频设备输出及设备相关处理 | 音乐库管理 |
| Video Output | 视频帧队列交接、呈现调度及输出生命周期协调 | 播放列表业务、具体平台 GPU 绘制 |
| Platform Renderer | 在共享 `thread_pool` 执行受调度的帧获取与 GPU 呈现任务，并遵守平台图形 API 的线程/上下文约束 | 媒体解封装、解码及播放状态机 |
| Playlist | 播放队列、条目顺序和切换策略 | FFmpeg 解码 |
| Music Library | 音乐条目、元数据、文件位置及查询 | 原始文件搬移 |
| Storage | SQLite 访问、事务和数据迁移 | UI 展示 |
| Event Manager | 将 Core 状态和事件通知给调用方 | 跨进程窗口管理 |

上述为逻辑职责边界，不要求每个模块立即拆成独立动态库或静态库。物理拆分应由依赖关系和构建需求决定。

## 4. Player 对象模型与生命周期


### 4.1 对象职责与所有权

每个进程只创建并持有一个 Player Instance。Core Runtime 负责该 Player 的运行环境和生命周期；不需要 Player Manager 管理多个 Player，也不提供进程内多 Player 调度。

- **Runtime**：管理进程内唯一 Player、一个 `io_context`、一套共享 `thread_pool`、事件分发器及其他运行时资源。
- **Player Instance**：代表当前进程唯一的逻辑播放器，拥有播放状态、Playback Controller、Playlist、当前 Playback Session 和事件出口。
- **Playback Controller**：串行协调唯一 Player 的播放命令、状态转换和媒体生命周期；不直接承担 FFmpeg 解码工作。
- **Playlist**：保存队列条目、当前条目位置及播放顺序策略。队列条目是媒体引用/描述，不持有打开中的 FFmpeg 上下文。
- **Playback Session**：代表当前打开的一次媒体播放，持有媒体输入、FFmpeg format/codec 上下文、解码器、时钟、数据队列及会话级资源。
- **Media Pipeline**：由 Playback Session 创建和管理，负责探测、解封装、解码和同步；平台输出通过抽象接口接入。

~~~mermaid
classDiagram
    class Runtime {
        +createPlayer()
        +destroyPlayer()
    }
    class PlayerInstance {
        +state
    }
    class PlaybackController {
        +open()
        +play()
        +pause()
        +stop()
        +seek()
        +close()
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
    Runtime "1" *-- "1" PlayerInstance
    PlayerInstance "1" *-- "1" PlaybackController
    PlayerInstance "1" *-- "1" Playlist
    PlayerInstance "1" *-- "0..1" PlaybackSession
    PlaybackSession "1" *-- "1" MediaPipeline
~~~


### 4.2 进程、Player 与会话数量

- **一个进程只有一个 Player Instance**，由该进程内的 Core Runtime 创建和管理。
- 一个 Player Instance 同一时刻最多拥有一个当前活动 Playback Session。
- 一个 Player Instance 可以在没有活动 Session 时存在，例如刚创建、尚未打开媒体或关闭当前媒体之后。
- 一个 Player Instance 可以持有多个 Playlist 条目，但队列中的其他条目不会因此提前创建 FFmpeg 上下文。
- 切换媒体时，由唯一的 Playback Controller 协调当前 Session 的关闭和目标媒体 Session 的建立。
- 首期不设计同一 Player 内多个独立媒体同时播放；播放列表中的其他条目不构成额外 Player。

Windows 每个播放器窗口对应一个独立子进程，因此每个窗口进程各自拥有一个 Player。音频 UI 和视频 UI 可以同时存在于不同进程。Android 每个应用进程同样只创建一个 Player。

不同进程之间不共享 Player、Runtime、线程池、FFmpeg 上下文或可变播放状态。多个进程可以同时播放，但这是操作系统进程级隔离，不是单进程内多 Player 并发。

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

C ABI 函数统一采用普通同步函数调用形式，但接口按完成语义分为三类：

- **提交型接口**：函数负责接收并提交操作请求，成功进入 Core 管理队列后立即返回。返回值表示请求是否被接受，不代表业务执行成功。
- **同步完成型接口**：函数提交操作后等待实际操作完成，再返回最终执行结果。等待发生在调用 C ABI 的线程；Core 的 `io_context` worker 不得阻塞等待业务任务。
- **快速查询型接口**：同步读取调用时刻可立即取得的线程安全状态快照或缓存并返回，不等待媒体探测、解码、文件 I/O 或其他耗时任务。

首期命令语义：

| 命令 | C ABI 行为 | 返回语义 |
|---|---|---|
| create | 创建进程内唯一 Player，快速返回句柄 | 创建结果 |
| open(source) | 提交媒体打开任务后立即返回 | 请求是否成功接收；最终结果由 `OPEN_COMPLETED` 事件报告 |
| play | 等待播放启动操作完成 | 实际操作成功或失败 |
| pause | 等待暂停操作完成 | 实际操作成功或失败 |
| stop | 等待停止操作完成 | 实际操作成功或失败 |
| seek(position) | 提交定位任务后立即返回 | 请求是否成功接收；最终结果由 `SEEK_COMPLETED` 事件报告 |
| close | 等待当前媒体关闭及关联资源清理完成 | 实际操作成功或失败 |
| get_state / get_position / get_duration / get_volume | 读取线程安全快照 | 当前可立即取得的值 |
| destroy | 执行 Player 生命周期销毁屏障 | 返回前句柄失效、在途工作退出且事件回调不再访问关联资源 |

`play/pause/stop/close` 是同步完成型接口。调用方线程等待操作最终结果；它们不通过操作完成事件代替函数返回值。状态变化等独立事件仍可正常分发。

`open/seek` 是提交型接口。每个被接受的请求分配 `operation_id`；请求必须在入口验证后进入受管理队列，成功入队是提交成功的线性化点。并发调用按实际入队顺序确定命令顺序。

同一进程只有一个 Player，因此只需一个 Playback Controller 协调该 Player 的命令顺序。共享线程池中的任务仍可并发执行，但不能依赖线程池的执行或完成顺序来保证命令语义。快速查询走线程安全快照路径，不等待 `io_context` 或线程池。

同步完成型接口不得从 Core 的 `io_context` worker 或该 Player 的事件回调中调用，避免等待自身负责推进的任务而形成死锁。平台 UI 若不能承受等待，应从平台后台线程调用这些接口。


### 4.5 打开、切换与销毁顺序

媒体打开流程：

1. 唯一的 Playback Controller 接收 `open` 请求并登记 `operation_id`。
2. 若存在旧 Session，先使其停止接收新工作，并协调旧 Session 关闭。
3. 创建新的 Playback Session。
4. 打开媒体输入、读取媒体信息并初始化所需解码器和输出资源。
5. 初始化成功后将新 Session 设为当前 Session，状态进入 Ready。
6. 通过 `OPEN_COMPLETED` 事件报告最终结果；媒体信息通过后续快速查询或专用数据接口读取。

若打开失败，释放新 Session 已创建的资源并发送失败完成事件；不得留下半初始化的活动会话。使用媒体代次（generation）隔离不同打开周期，旧任务的迟到结果不得覆盖当前 Session 状态。

`stop` 与 `close` 的职责应保持区分：`stop` 等待停止操作达到其定义的停止状态；`close` 等待当前媒体 Session 关闭及其关联资源清理完成。二者都必须在实际操作完成后才从 C ABI 返回。

Player 销毁是生命周期屏障，不是普通的异步播放命令。销毁顺序遵循资源依赖的逆序：停止接收新请求 → 使句柄失效并阻止新访问 → 取消/中断媒体读取与后台任务 → 停止输出 → 等待相关工作退出 → 释放解码器和格式上下文 → 释放队列及时钟 → 停止事件分发并等待在途回调退出 → 释放 Player 与 Runtime 资源。不得在该 Player 自身的回调中调用会等待该回调退出的同步销毁操作。

### 4.6 C ABI v1 契约

公开头文件为 `core/include/lnplayer.h`，只使用 C 兼容类型；平台 UI 只依赖该头文件，不接触 Core 内部 C++ 类、Asio、FFmpeg 或平台图形对象。

- 句柄使用不透明的 `lnplayer_t*`。每个进程最多创建一个实例；重复创建返回 `LNPLAYER_RESULT_ALREADY_EXISTS`。
- `lnplayer_create` 接收可选配置，配置通过 `struct_size` 支持后续扩展；创建成功后返回句柄。
- `lnplayer_destroy` 是同步销毁屏障。调用方必须先停止并发 API 调用；不得从事件回调内部调用。
- 媒体源通过 UTF-8 字符串传入；Core 不接管调用方字符串内存。
- `open/seek` 属于提交型接口，输出非零 `operation_id`；最终结果通过对应完成事件报告。operation_id 在单个 Player 生命周期内唯一。
- `play/pause/stop/close` 属于同步完成型接口，返回时操作必须已经完成；不得从 Core 事件回调或 io_context worker 调用。
- `lnplayer_get_snapshot` 只读取线程安全快照，不等待任务执行。位置和时长单位为毫秒；未知时长使用 -1。
- 音量使用 0–1000 整数范围，0 表示静音，1000 表示 100%。
- 事件回调运行在 Core 事件线程。事件结构及其中的 message 指针仅在回调期间有效；平台必须将 UI 更新投递到 UI 线程。
- 回调不得直接执行同步完成型操作或销毁 Player。回调可保存必要数据后立即返回。
- `lnplayer_get_last_error` 返回当前调用线程最近一次 C ABI 错误；所需缓冲区长度包含结尾 NUL。
- 返回码使用 `lnplayer_result_t`；接口不向调用方返回 Core 分配的内存，因此不存在跨 ABI 的释放函数或分配器所有权问题。

首期错误码固定为：`OK`、`INVALID_ARGUMENT`、`INVALID_HANDLE`、`ALREADY_EXISTS`、`INVALID_STATE`、`NOT_IMPLEMENTED`、`OUT_OF_MEMORY`、`INTERNAL_ERROR`、`BUFFER_TOO_SMALL`、`WOULD_DEADLOCK`。枚举值在 ABI v1 内不得重排或复用。

当前实现状态：ABI v1 头文件、唯一实例生命周期、Playback Controller 命令入口、异步 open/seek 完成事件、同步完成型接口等待、线程安全一致快照、音量设置和线程局部错误读取已建立。Controller 通过独立 `io_context` 线程协调命令；同步接口检测并拒绝在事件线程自等待。当前尚未接入 FFmpeg/音频输出后端：open/seek 会被接受并以 `NOT_IMPLEMENTED` 完成事件结束；play/pause 在无有效播放状态时返回 `INVALID_STATE`，需要媒体后端的操作不会伪报成功。stop/close 可清理空闲或打开失败后的逻辑状态。当前 Controller 的媒体工作仍是后端接入前的控制面骨架，尚未向共享 `thread_pool` 分发实际解码或 I/O 任务。

### 4.7 MediaBackend 与 PlaybackSession

Core 内部媒体会话采用两层结构：

- `MediaBackend` 是媒体处理后端接口，负责媒体源打开、播放控制、定位、停止和关闭。接口只暴露 Core 自己定义的 `MediaInfo` 与结果类型，不暴露 FFmpeg 类型。
- `PlaybackSession` 管理一次媒体会话及其后端实例，维护会话状态、媒体代次、时长和音视频流存在标记。
- `PlaybackSession` 不负责线程调度。Controller 将其操作提交到共享线程池，并在任务完成后把结果投递回 `io_context`。
- 同一个 Session 对同一个 Backend 的调用必须串行化；不能并发操作同一个 FFmpeg `AVFormatContext` 或解码器上下文。不同媒体阶段可在明确的资源边界上并行。
- 打开新媒体时递增 generation。旧 generation 的迟到结果不得更新当前 Player 状态。
- `MediaBackend` 的同步接口允许后端在调用线程执行阻塞式 FFmpeg API；因此调用必须发生在线程池，而不是 `io_context` worker 或 UI 线程。
- `MediaBackendFactory` 用于创建具体后端。FFmpeg 版本、依赖获取和链接方式确定后，再提供 FFmpeg 实现；平台音频/视频输出不属于 MediaBackend 的职责。

当前代码已建立 `core/media_backend.h`、`core/playback_session.h/.cpp` 的接口和会话状态管理。具体 FFmpeg 后端及 Controller 到 Session 的任务调度接入仍待实现。

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

### 5.3 Windows 首期工程实现

Windows 应用使用单一 `LNPlayer.exe`，通过启动参数选择音频或视频界面（`--audio` / `--video`）。每次进程启动只创建一个主窗口和一个 Core Runtime；不在同一进程内创建第二个 Player。

当前工程包含 Qt 6 Widgets 音频/视频窗口、Core C ABI、FFmpeg 音频解码、WASAPI 输出、SQLite 音乐库及 Windows D3D11 视频输出基础实现。音视频使用同一个 FFmpeg AVFormatContext，分别解码音频与视频流。

视频链路当前已实现：
- FFmpeg 音视频流探测、视频解码器初始化及同一解封装上下文的音视频 Packet 分流。
- 有界 Packet Queue；视频帧转换为 BGRA，支持软件解码。
- Windows 优先尝试 D3D11VA，硬件帧暂时下载到系统内存，再上传至 Renderer；硬件初始化失败时回退软件解码。
- Windows 原生 QWidget 子窗口 HWND、D3D11 Device/Swap Chain、纹理采样、原始比例、16:9、4:3、拉伸显示和窗口 resize；ResizeBuffers 失败及 DXGI device removed/reset 时尝试重建 Renderer。
- Windows 专用 `lnplayer_windows.h` 视频帧 Pull 接口；视频帧传输不加入通用 `lnplayer.h`。
- 统一按容器起始时间将音频/视频 PTS 归一化到从零开始的时间线；视频 PTS 等待、音频流主时钟的基础调度；音频位置在 PCM 写入后根据 WASAPI 当前待播放缓冲量回算，减少解码领先于扬声器实际播放造成的同步偏差；无音频视频使用独立时钟。
- 视频 Seek 后刷新解码器和 Packet Queue；暂停保留最后一帧。
- 全屏、快捷键、右键菜单、截图和逐帧操作的基础实现。

当前仍未完成：D3D11VA 与 Renderer 共享同一 Device 的零拷贝路径、精确 WASAPI 音频硬件时钟、独立 Core Video Frame Queue/显示 Scheduler、DXVA2 后备、多音轨选择、字幕解码/渲染、音频变速保音高、色彩空间/旋转元数据、滤镜和完整播放列表。当前 D3D11 Present 由 Windows 命令工作线程执行，窗口 resize 由 UI 线程执行并通过 Renderer 锁串行化；尚未验证实际设备和长时间播放稳定性。

## 6. Android 运行模型

建议 Android 首期采用单应用进程模型：

- Kotlin UI 管理 Activity、界面状态和 Android 生命周期。
- JNI Bridge 负责 Kotlin 与 Native Core 之间的调用。
- Core 管理 Player Instance 和媒体会话。
- 平台适配层负责 Android 音频输出、视频渲染和系统媒体集成。
- Android 后台播放、前台服务、MediaSession 和音频焦点策略仍需单独确认。

## 7. 异步服务、线程池与并发模型



### 7.1 总体模型

Core 使用 **Standalone Asio（不依赖 Boost）** 作为内部异步协调与并发执行基础。只使用 Asio 的独立发行版，代码命名空间为 `asio::`，不引入 Boost 库。每个进程只有一个 Player；该进程内的 Core Runtime 维护：

- 一个 `asio::io_context`，由独立单线程运行，负责命令接收、Player 命令协调、任务分发、结果回收、状态更新和事件分发。
- 一套共享的 `asio::thread_pool`，执行播放控制、媒体处理调用、文件 I/O、音乐库访问等实际业务任务。
- 一个 Player Instance 及其 Playback Controller、Playlist、Playback Session 和平台输出资源。

控制任务、媒体任务、文件 I/O、音乐库任务均使用同一套共享线程池，不按业务类型额外创建多个线程池。线程池中的工作线程可以并发执行不同任务；具体资源的线程安全和操作依赖由 Controller 与资源级同步机制保证。

C ABI 采用三种调用语义：

- 提交型（首期 `open/seek`）：将请求交给 Runtime 管理队列，确认接收后立即返回。
- 同步完成型（首期 `play/pause/stop/close`）：提交任务后由 C ABI 调用线程等待最终结果；业务任务仍由共享线程池执行，完成结果经 `io_context` 回传并唤醒调用线程。
- 快速查询型：直接读取线程安全状态快照，不等待 `io_context` 或线程池。

`io_context` worker 只负责协调，不得阻塞等待线程池业务任务。同步等待只发生在调用 C ABI 的外部线程；不得从 `io_context` worker 或事件回调线程调用同步完成型接口。

~~~mermaid
flowchart TD
    UI["Qt UI / Kotlin UI"] --> ABI["C ABI"]
    ABI --> KIND{"接口语义"}
    KIND -->|"快速查询"| SNAP["线程安全状态快照"]
    KIND -->|"提交型 open / seek"| IO["io_context"]
    KIND -->|"同步完成型 play / pause / stop / close"| WAIT["提交任务并由调用线程等待"]
    IO --> CTRL["唯一 Player Controller"]
    WAIT --> CTRL
    CTRL --> POOL["共享 Asio thread_pool"]
    POOL --> TASK["FFmpeg / 输出 / 文件 I/O / 音乐库任务"]
    TASK --> RESULT["执行结果"]
    RESULT --> IO
    IO --> UPDATE["更新状态 / 生成事件 / 唤醒等待者"]
    UPDATE --> EVENT["C ABI 事件回调"]
    SNAP --> ABI
~~~


### 7.2 Asio 与业务任务职责

| 组件 | 职责 |
|---|---|
| C ABI | 提供稳定跨平台边界；按接口语义提交任务、等待完成或读取快照 |
| `io_context` | 协调唯一 Player 的命令顺序、分发任务、回收结果、更新状态、生成事件并唤醒同步等待者 |
| 共享 `thread_pool` | 执行实际业务任务，包括播放控制、媒体处理调用、文件 I/O 和音乐库操作 |
| Player Controller | 管理唯一 Player 的命令顺序、媒体代次、状态转换和操作生命周期 |
| Event Manager | 分发提交型操作完成事件及独立状态/运行时事件 |
| FFmpeg | 执行媒体探测、解封装和解码，并管理其解码器内部线程 |

除快速查询型接口外，播放命令通过 Runtime 的 `io_context` 协调入口。快速查询直接读取线程安全快照，不访问非线程安全 FFmpeg 上下文，也不等待任务队列。

提交型接口在请求进入受管理队列后返回。同步完成型接口将任务提交给 Controller，并由调用 C ABI 的线程等待最终结果；业务完成后由工作线程将结果投递回 `io_context`，由其更新状态并唤醒等待者。不得让 `io_context` worker 或共享 `thread_pool` worker 阻塞等待同一 Runtime 内由其推进的任务。

共享线程池任务完成后，统一将结果投递回 `io_context`。由 `io_context` 更新 Player 状态、判定操作结果并安排事件分发。同步完成型接口的最终结果通过其函数返回值交付；提交型接口的最终结果通过带 `operation_id` 的操作完成事件交付。


### 7.3 共享线程池与任务并发

- 每个进程只有一个 Player；该进程内所有 Core 业务模块共用一套 Asio `thread_pool`。
- 同一个 Player 的命令顺序由唯一 Playback Controller 管理；不能依赖线程池任务的提交顺序、开始顺序或完成顺序来保证命令顺序。
- 同一 Session 可以存在多个异步任务在途。只有在任务访问同一非线程安全资源或存在数据依赖时，才对相应资源或操作阶段进行协调；不采用“整个 Session 全部任务串行”的模型。
- 线程池任务应尽量是有界工作单元。持续播放/解码不应由一个无限期运行的任务独占 worker；应按可调度的处理阶段提交任务，并在数据、队列和输出背压条件允许时继续推进。
- 线程池线程数量、任务优先级和背压策略仍需结合目标平台与实际性能确定。
- 多个播放器进程之间由操作系统隔离，各自拥有独立 Runtime 和线程池；不在进程之间共享线程池。

### 7.4 FFmpeg 内部多线程

FFmpeg 解码器本身支持内部多线程。Core 不再为每个 FFmpeg 解码线程创建对应的 Asio 任务，也不接管 FFmpeg 内部线程的生命周期。

- Core 的共享线程池负责调度对 FFmpeg 的业务调用。
- FFmpeg 根据解码器配置管理帧线程、切片线程等内部并行执行。
- 每个 Playback Session 拥有独立的媒体上下文和解码器上下文。
- 音频与视频通常使用独立的解码上下文，可在 Core 任务调度层并发推进，并分别使用 FFmpeg 内部解码线程。
- FFmpeg 的 `thread_count`、`thread_type` 等参数由媒体配置决定；多窗口/多进程同时播放时，需要评估总 CPU 并发，避免每个解码器都无约束地扩张线程数。
- FFmpeg 内部线程不属于 Core 的 Asio 共享线程池。

### 7.5 媒体资源的并发访问约束

采用异步并发服务不代表可以无保护地并发调用同一个 FFmpeg 上下文。并发约束应落在具体资源和操作上，而不是把整个 Session 串行化：

- 同一个 `AVCodecContext` 的发送 Packet、接收 Frame、刷新和重配置等操作，必须遵守 FFmpeg 对该上下文的线程安全要求；不得由多个 Core worker 无协调地同时调用。
- 同一个 `AVFormatContext` 的 demux 读取及 Seek/关闭等互斥操作，需要协调访问，避免读取与定位、关闭相互竞争。
- 音频与视频的独立解码上下文可以并行处理；解封装产生的 Packet 通过各自的数据队列交付给对应解码路径。
- 资源锁或操作协调只覆盖必要的临界区/操作阶段，不应在持锁状态下等待长时间任务、输出设备或其他异步任务。
- Seek、Close、Destroy 等生命周期操作需要与正在使用相关资源的任务建立取消、停止和完成同步关系，确保资源释放前不再有在途访问。

具体锁粒度、Packet/Frame 队列、解码推进方式和取消机制在媒体管线实现设计中确定。

### 7.5.1 硬件解码与软件回退

视频解码采用硬件解码优先（Hardware Decoding First），软件解码作为兼容性回退路径。该策略属于共享媒体管线的能力，不由 UI 直接选择或实现具体解码器。

- Media Pipeline 通过 FFmpeg 的硬件设备与硬件像素格式机制探测、初始化并使用可用的硬件解码路径。
- 解码器选择应结合平台、编解码格式、Profile/Level、分辨率、像素格式及设备能力；不能仅根据 GPU 厂商或设备型号推断支持情况。
- 硬件设备初始化失败、当前流不受支持或硬件路径运行失败时，允许切换到软件解码。切换需要协调解码器重建、Packet/Frame 队列和时钟状态，不能让两条路径同时无序写入同一输出队列。
- Core 管理统一的解码器选择策略和回退行为；平台适配层提供硬件设备创建、资源互操作和渲染能力。
- FFmpeg 的硬件解码能力取决于构建时启用的组件、运行时驱动和设备能力。具体 FFmpeg 构建选项与可用后端需要在依赖构建阶段验证。

Windows 首期优先评估 FFmpeg D3D11VA 路径，并将 DXVA2 作为兼容性候选。QSV、CUDA 等后端可按实际兼容性和维护成本作为可选能力，不要求首期为每种厂商接口建立独立解码管线。硬件帧应尽可能保留为 D3D11 GPU 资源交给渲染层，避免不必要的 GPU→CPU 下载和 CPU→GPU 重传。

Android 首期优先评估 FFmpeg MediaCodec 硬件解码集成。MediaCodec 的 Surface 输出与普通 CPU 可读 AVFrame 不同，具体输出模式取决于 FFmpeg 集成方式和 Android 渲染实现；需要由 Android 平台适配层管理 Surface/BufferQueue 生命周期及必要的 Native 资源交接，不能假设所有 MediaCodec 帧都能作为普通图像内存直接访问。

### 7.5.2 视频帧类型与渲染交接

Video Pipeline 与 Platform Video Output 之间采用有界帧队列交接，不把每一帧包装成普通 C ABI 命令或主动事件。Core 保留帧队列和帧调度状态；平台 Renderer 从可呈现队列获取帧，避免解码线程直接调用 GPU API。

Video Output 至少需要区分两类帧：

- **Software Frame**：位于 CPU 内存中的图像平面数据，包含像素格式、尺寸、平面指针、行跨度和时间戳等信息。平台渲染层可将其上传为 GPU 纹理并完成颜色空间转换。
- **Hardware Frame**：由 FFmpeg 硬件帧上下文持有或引用的平台硬件资源，例如 D3D11 纹理或 Android MediaCodec 输出缓冲区。其资源句柄、同步方式和释放操作由对应平台适配层处理。

帧交接必须明确引用计数/所有权、消费者完成使用的时机、跨线程释放规则、设备上下文归属及队列背压。Core 将帧交给 Renderer 时，Renderer 必须取得独立有效的帧引用；硬件帧在 GPU 最后一次使用完成且对应引用释放前不得归还解码器复用。不能仅传递一个裸指针后立即释放底层 AVFrame。

队列容量应有界。队列满时根据播放时钟、帧时间戳和实时性策略丢弃过期帧或施加解码侧背压；不能无限缓存以追求不丢帧。Seek、切换媒体或 Session 关闭时，应通过时间线代次/队列清理规则阻止旧帧进入新时间线。暂停时可保留最后一帧供静态显示。

Core 内部可以使用 FFmpeg AVFrame 和 AVBufferRef 管理帧生命周期，但这些 FFmpeg 类型不得出现在公开 C ABI 中。跨平台抽象应隐藏平台资源类型；若平台 UI/渲染器需要原生 Surface 或图形资源，应通过平台专用适配接口交接，而不是把 ID3D11Texture2D*、ANativeWindow* 等类型放入通用 lnplayer.h。

零拷贝是优先优化目标，不是所有设备和解码路径都能保证的契约。无法直接互操作时，允许执行必要的硬件帧传输/格式转换，或回退到软件解码；应避免无条件把硬件帧下载到 CPU 再重新上传 GPU。



### 7.6 平台 UI 与 C ABI 调用模型

平台 UI 不需要额外的 UI 侧 Asio 调度层。C ABI 函数本身是普通同步函数调用，但不同接口具有不同的完成语义。

- **Windows / Qt**：Qt GUI 主线程可直接调用提交型 `open/seek` 和快速查询接口。同步完成型 `play/pause/stop/close` 会等待 Core 操作完成；为避免界面冻结，应由平台后台线程调用这些接口，再通过 Qt queued connection 更新 UI。
- **Android / Kotlin**：Kotlin 通过 JNI 调用相同 C ABI。提交型接口和快速查询可直接调用；同步完成型接口应由后台 Dispatcher/线程调用，不得阻塞 Android Main Thread。
- **Core 内部**：命令由 Runtime 的 `io_context` 协调，实际业务由共享 `thread_pool` 执行。UI 不需要了解 Core 的线程池结构，也不负责调度 FFmpeg 任务。

快速查询只读取线程安全快照。同步完成型接口的等待发生在调用 C ABI 的线程，不阻塞 `io_context` worker。事件回调不得直接操作 QWidget、Android View 或其他 UI 对象；平台桥接层必须将事件投递到对应 UI 线程。

不得从 Core 的 `io_context` worker 或 Player 事件回调中调用同步完成型接口，以免发生自等待或死锁。

### 7.5.3 渲染任务调度与实时边界

- 视频帧 Pull 不采用独立忙轮询循环。帧就绪、Asio 定时器、平台 vsync/显示回调或其他呈现时机触发后，先进入 Runtime `io_context`，再由其协调并将实际帧获取/呈现工作提交到共享 `thread_pool`。
- 视频 GPU 资源的创建、绑定、提交和销毁必须遵循对应图形 API 的设备、上下文及线程约束。普通渲染任务由共享 `thread_pool` 执行；通过 Renderer 内部的同步/串行化机制保护非线程安全的图形上下文，不默认创建独立渲染线程或渲染线程池。
- 不得假设 `thread_pool` 的 strand 会固定到同一个操作系统线程。若目标平台 API 经验证确实要求线程亲和性，需单独定义满足该 API 约束的受控执行上下文；这属于平台 API 约束，不改变 Core 统一经 `io_context` 协调、共享线程池执行任务的原则。
- Qt GUI 线程负责窗口和 QWidget 生命周期、布局与用户交互，不负责 FFmpeg 解码或 GPU 呈现工作。Core worker 不得直接操作 QWidget；需要更新窗口状态时，通过平台 UI 线程调度机制完成。
- Android UI 管理 Surface 生命周期；Native 输出适配层响应 Surface 创建、变化和销毁，并确保 Surface 失效后不再提交帧。Android API 的线程要求由平台适配层遵守。
- GPU Present、vsync 等可能长时间阻塞的调用不得无约束地占用共享 `thread_pool` worker；需验证使用可等待对象、低延迟呈现或其他非阻塞机制。确实无法避免的阻塞调用应采用有界、受控策略。
- 音频设备实时回调属于平台驱动/系统回调上下文，不放入共享线程池等待；回调内只执行实时数据搬运和缓冲区填充。音频回调与普通渲染任务均不得执行阻塞文件 I/O、SQLite 操作或等待共享线程池任务。

### 7.5.4 Windows：Qt Widgets + Win32 + D3D11

Windows 视频 UI 采用 Qt 6 Widgets 管理播放器窗口和常规控件，视频画面区域使用独立的原生 Win32 子窗口承载 D3D11 Renderer。Qt 负责布局、菜单、控制栏、窗口模式及交互；D3D11 Renderer 只负责视频画面呈现。

- Video Container 由 Qt 管理尺寸和位置；其内部原生 HWND 作为视频显示目标。
- D3D11 Renderer 管理与该 HWND 关联的 Swap Chain；纹理采样、颜色转换和缩放等呈现任务由共享 `thread_pool` 执行，并通过 Renderer 内部同步机制保护 D3D11 Immediate Context 等共享图形上下文。Present 的阻塞特性及线程要求需在实现验证中确认。
- 首期 Windows 硬件解码优先使用 FFmpeg D3D11VA；DXVA2 保留为兼容性候选。
- 为避免解码纹理跨设备互操作，优先由 Windows Video Output 平台模块创建并管理 D3D11 Device，并将同一设备关联到 FFmpeg `AVHWDeviceContext` 与 Renderer。实际设备创建顺序和 FFmpeg 设备上下文接入方式以 D3D11VA 集成验证为准。
- D3D11VA 产生的硬件帧通过 FFmpeg 硬件帧引用进入 Core 帧队列；Renderer 的呈现任务从队列取得帧引用并访问对应 D3D11 资源，GPU 资源访问遵守统一的同步规则。
- 不能因 Renderer 已取得纹理接口就提前释放 AVFrame 或允许解码器复用表面。必须保证 GPU 最后一次读取完成后才释放对应帧引用；必要时使用 D3D11 同步机制或受控的渲染队列完成通知。
- 窗口 resize 时调整 Swap Chain 尺寸，不重建 Player 或媒体 Session。窗口隐藏/最小化时可暂停不必要的呈现，但不得破坏播放状态。
- 不将 D3D11VA 纹理强行接入 `QOpenGLWidget` 作为首期路径，避免引入 D3D11/OpenGL 互操作复杂度。

建议 Windows 视频显示关系：

~~~mermaid
flowchart TD
    subgraph Qt["Qt Widgets UI"]
        MW["VideoWindow"]
        VC["Video Container"]
        MW --> VC
    end
    VC --> HWND["Win32 Child HWND"]
    subgraph VideoOutput["Windows Video Output"]
        DEV["Shared D3D11 Device"]
        DEC["FFmpeg D3D11VA"]
        Q["Core Video Frame Queue"]
        R["D3D11 Renderer Tasks"]
        SWAP["Swap Chain"]
        DEV --> DEC
        DEV --> R
        DEC --> Q
        Q --> R
        R --> SWAP
    end
    HWND --> SWAP
    SWAP --> SCREEN["Video Display"]
~~~

### 7.5.5 Android：Kotlin + SurfaceView + MediaCodec

Android UI 使用 Kotlin 原生界面，视频显示区域首选评估 `SurfaceView`。Surface 创建、尺寸变化和销毁由 Android UI 生命周期通知 Native 平台输出模块；Surface 失效后必须停止向其提交帧。

FFmpeg MediaCodec 集成需要验证具体输出模式，不能预先假定所有 MediaCodec 解码帧都表现为可直接渲染的普通 AVFrame，也不能假定 FFmpeg 会自动完成任意 Surface 的绑定和呈现时机控制。

首期评估两条平台实现路径：

1. **MediaCodec 直接输出 Surface**：减少应用层帧搬运，适合直接显示；需要确认 FFmpeg MediaCodec API 对 Surface 绑定、输出缓冲区释放及呈现时间控制的支持程度。
2. **硬件帧交给 Native Renderer**：由 Native 渲染器从硬件帧获取可用图形资源并在 Surface 上绘制；便于统一 PTS 调度，但需要验证 MediaCodec 硬件帧与 Android 图形 API 的互操作方式。

两条路径不强制共用完全相同的内部帧实现。Android 平台适配层应封装 Surface 生命周期、MediaCodec 输出缓冲区和 Native 图形资源；Core 保持跨平台播放控制、时间戳和帧生命周期的抽象边界。最终采用哪条路径，以目标 Android 版本、FFmpeg 构建及设备验证结果决定。

~~~mermaid
flowchart TD
    UI["Kotlin UI"] --> SV["SurfaceView"]
    SV --> SURF["Android Surface"]
    subgraph Native["Native Platform Output"]
        MC["FFmpeg MediaCodec Integration"]
        MODE{"输出模式验证"}
        DIRECT["MediaCodec Surface Output"]
        FRAME["Hardware Frame / Native Renderer"]
        MC --> MODE
        MODE --> DIRECT
        MODE --> FRAME
    end
    DIRECT --> SURF
    FRAME --> SURF
    SURF --> COMP["Android Compositor"]
    COMP --> DISPLAY["屏幕"]
~~~

### 7.5.6 视频呈现调度与时钟

- 正常音视频播放时以音频时钟作为主时钟；视频 Scheduler 根据帧 PTS 与音频时钟决定帧的呈现、等待或丢弃。
- 无音频流或音频输出不可用时，使用独立视频时钟。
- Renderer 依据显示刷新节奏获取当前应呈现的最新帧，不要求每个解码帧都必须显示。
- Seek 会推进媒体时间线代次并清理旧帧；旧代次帧即使迟到也不得呈现。
- 暂停时冻结播放时钟并保留最后一帧；恢复播放后重新按时钟调度。
- 具体时钟漂移校正、显示刷新率适配和丢帧阈值在同步专题中进一步确定。


### 7.7 进程隔离边界

每个进程独立创建一个 Core Runtime，并且**一个进程只有一个 Player Instance**。该进程的 Runtime、`io_context`、共享 `thread_pool`、Player、Playback Session 和 FFmpeg 上下文均属于该进程。

Windows 每个播放器窗口运行在独立子进程中，因此每个窗口进程各自拥有一个 Player 和一套 Core 运行资源。Android 每个应用进程同样只创建一个 Player。

不同进程之间不共享 Player、线程池或可变媒体上下文。进程隔离用于窗口和资源生命周期隔离；每个进程内部仍按 Asio 共享线程池模型执行异步并发任务。

## 8. Core C ABI 接口与事件系统

Core 对外提供稳定的 C ABI，公共头文件位于 `core/include/lnplayer.h`。C ABI 是 Windows Qt UI 与 Android JNI Bridge 共同使用的边界；内部 C++ 类型、FFmpeg 上下文和线程对象均不得泄漏到该边界。


### 8.1 Runtime 与 Player 句柄

建议采用 Runtime 句柄和唯一 Player 句柄：

- **Runtime Handle（`ln_runtime_t`）**：不透明句柄，代表一个进程内的 Core 运行环境，负责唯一 Player、线程池、事件分发及模块级资源。
- **Player Handle（`ln_player_t`）**：不透明或整数型句柄，代表该 Runtime 唯一的 Player Instance。
- 一个 Runtime 只能创建一个 Player；重复创建应返回明确的已存在/状态错误，而不是创建第二个实例。
- Player 销毁后句柄失效；后续调用返回明确的无效句柄错误。
- Runtime 销毁必须等待唯一 Player 完成销毁，或按确定顺序执行 Player 销毁屏障后再释放 Runtime 资源。

不需要 Player Manager 或进程内 Player ID 分配器来管理多个实例。公开句柄不得暴露内部 C++ 对象指针的可解引用语义；具体句柄类型和 ABI 布局在头文件设计时确定。


### 8.2 API 分组

建议公开 API 按职责分组：

| API 分组 | 典型操作 | 语义 |
|---|---|---|
| Runtime | create、destroy、get_version | 初始化和关闭进程内 Core 运行环境 |
| Player | create、destroy | 创建和销毁进程内唯一 Player |
| Event | set_callback、clear_callback | 注册或注销事件接收端 |
| 提交型 Playback Command | open、seek | 接收请求后快速返回提交结果及 operation_id |
| 同步完成型 Playback Command | play、pause、stop、close | 等待实际操作完成并返回最终结果 |
| Playback Query | get_state、get_position、get_duration、get_media_info、get_volume | 快速读取线程安全状态快照或缓存 |
| Error | error_code_to_string、get_last_error（如确有需要） | 获取错误说明 |

API 命名统一使用 `ln_` 前缀。导出函数使用 C linkage，并明确导出宏、调用约定和固定宽度整数类型。公共头文件只包含 C ABI 所需的标准 C 头文件，保证可由 C 和 C++ 编译器包含。


### 8.3 请求提交、查询与异步执行

C ABI 函数由调用方以普通同步方式调用。不同接口采用提交、等待完成或快速查询语义。

**提交型接口（首期 open/seek）：**

1. 调用方传入唯一 Player 句柄和操作参数。
2. C ABI 检查参数及句柄有效性，分配 `operation_id`。
3. 请求进入 Runtime 管理的 Player 命令队列；成功入队是提交成功的线性化点，函数随即返回提交结果和 `operation_id`。
4. Controller 按实际入队顺序协调命令，将实际业务任务提交到共享 `thread_pool`。
5. 业务任务完成后将结果投递回 `io_context`，由其更新状态并发送携带相同 `operation_id` 的操作完成事件。

并发调用时，以请求被唯一 Player 命令队列实际接受的顺序作为命令顺序。不能仅因调用线程先进入 C ABI 就推断其先于另一个并发请求入队。

**同步完成型接口（首期 play/pause/stop/close）：**

1. 调用方线程调用 C ABI，入口完成参数和句柄验证。
2. 请求交由 Runtime 的 `io_context` 和唯一 Player Controller 协调。
3. Controller 按命令顺序将业务任务提交到共享 `thread_pool`。
4. 调用 C ABI 的线程等待该操作的完成结果；等待不得发生在 `io_context` worker 或负责完成该操作的线程池 worker 中。
5. 业务任务完成后将结果投递回 `io_context`，更新 Player 状态并唤醒等待中的调用线程。
6. C ABI 返回最终执行结果，而非仅返回请求接收结果。

**快速查询型接口：**

直接读取原子变量或锁保护的线程安全状态快照，不等待 `io_context`、`thread_pool`、FFmpeg 或文件 I/O。查询结果表示读取时刻可立即取得的状态；媒体信息尚未就绪时按接口约定返回未就绪状态。

提交结果与操作结果严格区分：

- **提交成功**：请求已进入唯一 Player 的受管理命令队列，不保证业务最终成功。
- **提交失败**：参数无效、句柄无效或命令队列无法接收请求等入口错误。
- **同步操作成功/失败**：由 `play/pause/stop/close` 函数返回最终执行结果。
- **异步操作成功/失败**：由 `OPEN_COMPLETED`、`SEEK_COMPLETED` 等携带 `operation_id` 的完成事件报告。

C ABI 不暴露 Future、Promise 或 C++ 异步类型。Core 内部可以使用 Promise/Future 或等价机制唤醒原调用线程，但 `io_context` worker 不得阻塞等待线程池任务。快速查询是绕过 `io_context` 的明确例外。

首期命令语义：

| 命令 | C ABI 返回语义 | 最终业务结果 |
|---|---|---|
| open(source) | 请求进入管理队列后立即返回提交结果及 operation_id | OPEN_COMPLETED 成功/失败事件 |
| play | 等待播放启动操作完成 | 函数返回最终结果 |
| pause | 等待暂停操作完成 | 函数返回最终结果 |
| stop | 等待停止操作完成 | 函数返回最终结果 |
| seek(position) | 请求进入管理队列后立即返回提交结果及 operation_id | SEEK_COMPLETED 成功/失败事件 |
| close | 等待媒体关闭及关联资源清理完成 | 函数返回最终结果 |

视频帧交付是独立实时数据通道，不属于普通控制请求响应或主动事件通知。帧由 Core Video Output 与平台 Renderer 之间的内部平台输出接口交接；通用播放控制 C ABI 不逐帧传递图像数据。

同一 Player 的命令顺序由唯一 Controller 管理；这不要求该 Player 的所有内部任务串行执行。每个进程只有一个 Player，不存在进程内不同 Player 之间的并发调度。


### 8.4 事件结构与类型

建议采用统一事件结构，至少包含：

| 字段 | 含义 |
|---|---|
| struct_size | 结构体大小，用于 ABI 版本扩展 |
| type | 事件类型 |
| operation_id | 关联的异步操作编号；非操作事件为 0 |
| sequence | 该 Player 内单调递增的事件序号 |
| result | 事件对应的结果或错误码 |
| data / data_size | 可选事件负载及其字节数 |

首期事件类型建议包括：

- `STATE_CHANGED`：Player 状态变化。
- `OPEN_COMPLETED`：打开操作完成，携带 operation_id、成功/失败结果及可选媒体信息摘要。
- `SEEK_COMPLETED`：定位操作完成，携带 operation_id 和成功/失败结果。
- `PLAYBACK_ENDED`：媒体自然播放结束。
- `ERROR`：无法归属于某个提交型请求的播放过程错误或异步运行时错误。
- `MEDIA_INFO_UPDATED`：媒体信息在播放期间发生可独立通知的更新（如确有需要）。
- `POSITION_UPDATED`：可选的位置更新事件，频率需要限制，避免高频事件挤占其他事件。

`OPEN_COMPLETED` 和 `SEEK_COMPLETED` 报告提交型操作的最终业务结果。同步完成型 `play/pause/stop/close` 的最终结果通过 C ABI 函数返回，不需要再通过操作完成事件重复报告；它们引起的独立状态变化仍可通过 `STATE_CHANGED` 通知。

事件负载使用有明确布局和版本规则的 C ABI 结构体，不传递 C++ 对象、STL 容器或 FFmpeg 类型。扩展结构体时保留 `struct_size`，新增字段只能追加，不能改变既有字段的含义和布局。


### 8.5 回调线程与事件顺序

- Core 不在 Qt GUI 线程或 Android UI 线程执行事件回调。
- 回调由 Core 自己的事件分发机制调用，平台层负责转发到 UI 线程。
- 每个进程只有一个 Player；该 Player 的事件由 Runtime 的 Asio 异步分发路径统一排序，同一 Player 的回调不并发执行。
- `sequence` 在该 Player 内单调递增，便于调用方识别事件顺序和排查遗漏。
- 回调应快速返回，不执行耗时工作，不等待同步完成型 C ABI 操作，也不直接操作 UI 控件。
- Qt 桥接层使用 queued connection 将事件转发到 GUI 线程；Android JNI Bridge 将事件转发到合适的 Kotlin 调度上下文，再由主线程更新 UI。
- 不得假定回调来自固定线程。

位置更新属于高频信息，建议通过可配置频率的事件或 UI 定时查询获取；状态变化和独立运行时错误等事件必须优先且不能被位置更新淹没。

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
- 请求提交失败或命令队列无法接收请求。
- 媒体输入、格式探测、解码和输出错误。
- 内存、内部资源和未知错误。

提交型 C ABI 返回码表示请求是否成功进入受管理命令队列，不代表实际业务执行成功。参数错误、无效句柄和请求入口拒绝等提交阶段错误由函数返回码报告；媒体探测、定位等异步业务失败通过携带 operation_id 的完成事件报告。

同步完成型 `play/pause/stop/close` 的返回码表示实际操作的最终结果。与具体请求无关的运行时错误通过主动错误事件通知。错误码应稳定且可枚举，不能直接把 FFmpeg 内部错误值作为公开 ABI 错误码。需要展示的错误文本应通过稳定的错误码转换接口获得，或由错误事件提供 UTF-8 描述。若提供 `get_last_error`，必须明确其线程归属，避免把不同调用线程的同步错误混淆。


### 8.8 回调注销与对象销毁

- 注册回调时允许调用方传入 `user_data`，Core 只保存指针，不取得其内存所有权。
- 注销回调后，不再启动新的回调；同步注销需要等待已开始的回调退出，之后调用方才可释放 `user_data`。
- 不允许在 Player 自身的回调中调用会等待该回调退出的同步注销或销毁操作，否则可能发生自等待死锁。
- 每个进程只有一个 Player。Player 销毁时停止接受新命令，取消或终止未完成工作，停止事件分发并等待在途回调退出，然后释放 Session、Controller 和其他 Player 资源。
- Runtime 销毁必须等待唯一 Player 销毁完成，并确保事件分发资源停止，不能遗留回调访问已释放的桥接对象。

回调是通知机制，不拥有 Player 生命周期。平台层应在自身退出流程中明确注销回调并执行销毁屏障。


### 8.9 平台桥接约束

Windows Qt 与 Android JNI 共用同一套 C ABI 语义：

- Windows Qt GUI 主线程可调用提交型 `open/seek` 和快速查询；同步完成型 `play/pause/stop/close` 应从后台线程调用，避免阻塞 GUI。
- Android Kotlin 通过 JNI 调用相同 C ABI；提交型接口和快速查询可直接调用，同步完成型接口应从后台 Dispatcher/线程调用，避免阻塞 Main Thread。
- JNI Bridge 负责句柄映射、字符串转换、事件转发和线程切换，不重新实现播放状态机、命令顺序或媒体生命周期。
- 每个进程只有一个 Player；Runtime 创建第二个 Player 必须失败。
- 两个平台对提交结果、同步操作最终结果、异步操作完成事件、主动事件、错误码和内存有效期的理解必须一致。

本节定义接口契约和建议模型，不代表具体函数签名、枚举数值和结构体布局已经冻结。正式实现前应据此设计并审查 `core/include/lnplayer.h`。

### 7.8 Standalone Asio 构建约束

- Core 使用 Standalone Asio 头文件库，不依赖 Boost，也不链接 Boost.System。
- 源码统一使用 `asio::` 命名空间及 `asio.hpp` 头文件。
- CMake 通过独立的 Asio include 路径配置依赖；不得隐式回退到 Boost.Asio。
- Asio 仅负责异步协调和线程池调度；FFmpeg 解码等实际业务仍由共享线程池中的有界任务执行。

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
3. C ABI 具体函数签名、枚举数值、结构体布局，以及请求响应、命令取消和回调注销契约。
4. Asio `io_context` 与共享 `thread_pool` 的线程数量、任务调度、背压和响应路由模型。
5. FFmpeg 上下文的并发访问边界、创建/销毁、Seek、Flush 和中断机制，以及解码器内部线程预算。
6. 音视频时钟、同步、缓冲、显示刷新率适配和丢帧策略。
7. Windows 与 Android 音频输出后端。
8. Windows D3D11VA/DXVA2 的实际支持范围、D3D11 Device 创建/共享方式、帧资源同步与 GPU 完成检测。
9. Android MediaCodec 的 Surface 绑定、输出缓冲区呈现时机、Surface 生命周期与 Native 渲染资源互操作方式。
10. 硬件帧队列容量、帧引用/释放契约及无法直接互操作时的传输策略。
11. SQLite 访问线程模型、事务边界和迁移策略。
12. Windows 多进程启动、文件打开分发和 IPC 方案。
13. Android 后台播放和系统媒体控制架构。

## 11. 后续设计顺序

当前采用 **Windows 完整版本优先** 的实施策略。Android UI、JNI 和 Android 平台输出暂不进入实现阶段；Core 继续保持平台无关，Windows 专有实现通过平台适配层隔离。

1. **Core Runtime 与命令生命周期**
   - 重构 PlaybackController，使 `io_context` 只负责命令排序、任务分发、结果回收和事件通知。
   - 实际媒体操作提交到共享 Standalone Asio `thread_pool`。
   - 将 PlaybackSession 接入 Controller，落实 operation_id、generation、命令顺序和取消语义。
   - 建立可靠的 in-flight task 生命周期管理和 destroy 屏障，确保没有后台任务或事件回调访问已释放对象。
2. **Windows 媒体依赖与 FFmpeg Backend**
   - 确定 Windows FFmpeg SDK 的版本、构建配置、动态链接及许可证分发策略。
   - 实现媒体探测、流选择、解封装、音视频解码、Seek、Flush 和中断关闭。
   - 通过明确的 CMake 配置定位依赖，不在源码中硬编码开发机路径。
3. **Windows 音频播放链路**
   - 实现音频解码、重采样/格式转换、音频缓冲和音频主时钟。
   - 实现 Windows Audio Output、设备枚举/切换、音量和静音。
   - 完成音频播放、暂停、停止、Seek、结束、错误恢复和长时间播放。
4. **Windows 音频 UI 与音乐库**
   - 完成音频 UI 和 Core C ABI 的完整连接。
   - 实现直接播放、音乐目录导入、扫描、文件变化监控和手动刷新。
   - 实现 SQLite 音乐库、元数据、文件位置关联、重复内容识别和最近播放记录。
   - 实现音乐浏览/搜索、播放列表、随机/循环、封面/歌词、均衡器和音频设备选择。
5. **Windows 视频解码与 D3D11 输出**
   - 实现有界视频帧队列、帧引用和时间线 generation。
   - 实现 D3D11 Renderer、Swap Chain、颜色转换、缩放和窗口尺寸变化。
   - 接入 FFmpeg D3D11VA 硬件解码，并实现软件解码回退。
   - 完成音视频时钟同步、Seek/暂停/恢复、丢帧和资源释放。
6. **Windows 视频 UI 与高级功能**
   - 完成 PotPlayer 风格的视频窗口、播放控制、播放列表和右键菜单。
   - 实现快捷键、鼠标操作、全屏、画面比例、播放速度和音量控制。
   - 实现多音轨、内嵌/外挂字幕、逐帧播放、截图。
   - 按需求优先级完成滤镜、字幕样式和高级解码/渲染设置。
7. **Windows 多进程与文件打开流程**
   - 完成一个窗口对应一个独立播放器子进程。
   - 完成音频/视频模式启动、文件管理器打开文件、连续打开新窗口和进程间请求分发。
   - 保证关闭单个窗口只销毁对应 Player 和进程资源。
8. **Windows 系统集成与交付**
   - 实现系统媒体按键、任务栏/系统媒体控制和后台音频播放。
   - 完成设置持久化、Qt/FFmpeg 运行库部署、安装包和卸载流程。
   - 完成 Windows 完整功能、稳定性、兼容性和多窗口资源使用审查。
9. **Android 平台实现（Windows 完整版本完成后）**
   - 实现 Kotlin UI、JNI Bridge、Android 音频输出和后台播放集成。
   - 实现 Android 视频 Surface 生命周期、MediaCodec 硬件解码及对应回退路径。
   - 复用已稳定的 Core 播放、音乐库和媒体处理逻辑。

每完成一个专题，应同步更新需求与架构文档，并明确区分已实现、已确认但未实现、待验证和不在当前范围的内容。不得为了赶进度将未实现能力标记为完成。
