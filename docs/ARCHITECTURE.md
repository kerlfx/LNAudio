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
- Video Output：管理视频帧输出抽象、帧交付和平台渲染器之间的生命周期边界；Core 负责帧队列与播放时钟调度，平台 Renderer 负责 GPU 呈现。
- 视频解码采用硬件解码优先策略；硬件解码不可用或不支持当前媒体时允许回退软件解码。
- 渲染器按显示节奏从 Core 的可呈现帧队列获取帧（Pull），在自己的渲染线程执行图形 API 操作；解码线程不直接调用 UI 或 GPU 呈现接口。
- 输出实现通过平台输出抽象与共享播放逻辑隔离。平台图形设备、Surface 和渲染资源由对应平台适配层管理，不暴露为通用 C ABI 类型。

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
| Video Output | 视频帧队列交接、呈现调度及输出生命周期协调 | 播放列表业务、具体平台 GPU 绘制 |
| Platform Renderer | 在平台渲染线程获取帧并执行 GPU 呈现 | 媒体解封装、解码及播放状态机 |
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
6. 将打开操作的最终结果返回原请求响应通道；媒体信息通过响应数据或后续查询提供。独立发生的状态变化仍通过主动事件通知。

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

## 7. 异步服务、线程池与并发模型

### 7.1 总体模型

Core 使用 Boost.Asio 提供异步接口与并发任务执行能力。Asio 不承担播放器业务本身；实际业务任务通过 Asio 异步提交到 Runtime 共享的线程池执行。

每个 Runtime 维护：

- 一个 `boost::asio::io_context`，负责异步接口入口、异步操作协调、完成通知和事件分发。
- 一套共享的 `boost::asio::thread_pool`，作为 Runtime 内所有业务任务的统一执行池。
- Player Manager 及其他 Runtime 级资源。

控制任务、媒体任务、文件 I/O、音乐库任务均提交到同一套共享线程池，不额外创建按业务类型划分的控制池、媒体池或文件 I/O 池。线程池中的工作线程可以并发执行不同任务；任务类别不意味着独占线程或独立线程池。

~~~mermaid
flowchart TD
    A[平台 UI / JNI] --> B[C ABI]
    B --> C[Asio 异步接口层]
    C --> D[io_context]
    D --> E[统一任务提交]
    E --> F[Runtime 共享 Asio thread_pool]
    F --> G[播放控制任务]
    F --> H[媒体与 FFmpeg 调用任务]
    F --> I[文件 I/O 任务]
    F --> J[音乐库任务]
    G --> K[任务结果 / 完成通知]
    H --> K
    I --> K
    J --> K
    K --> D
    D --> L[事件分发]
    L --> B
~~~

### 7.2 Asio 与业务任务职责

| 组件 | 职责 |
|---|---|
| C ABI | 提供稳定的跨平台调用边界；校验参数并提交请求 |
| `io_context` | 异步接口协调、完成通知、事件分发及轻量调度 |
| 共享 `thread_pool` | 执行实际业务任务，包括播放控制、媒体处理调用、文件 I/O 和音乐库操作 |
| Player Controller | 处理播放命令、状态转换和操作生命周期 |
| FFmpeg | 执行媒体探测、解封装和解码，并管理其解码器内部线程 |

C ABI 命令采用同步提交、异步执行。C ABI 请求首先进入 Runtime 的 `io_context`，由 `io_context` 协调请求并将实际业务任务提交到共享线程池；业务任务完成后再投递回 `io_context`。操作完成结果通过该请求对应的响应通道返回，不转化为主动事件回调。不得绕过 `io_context` 从 C ABI 直接提交线程池，也不得在 C ABI 调用栈中执行耗时解码或阻塞式媒体操作。

Asio 的 `io_context` worker 不应被当作长期业务工作线程。业务任务统一提交到共享 `thread_pool`；完成通知通过 `post` 等方式返回 `io_context`。

### 7.3 共享线程池与任务并发

- 同一个 Runtime 内的所有 Player 和模块共用一套 Asio `thread_pool`。
- 多个任务可以同时执行；不同 Player、不同 Playback Session 之间允许并发。
- 同一个 Player 的命令提交、状态变更和操作结果需要遵守明确的顺序与状态机规则，但不因此要求该 Player 的所有工作任务都在单一队列中串行执行。
- 同一个 Session 可以存在多个异步任务在途。只有在任务会访问同一非线程安全资源或存在数据依赖时，才对相应资源或操作阶段进行协调；不采用“整个 Session 全部任务串行”的模型。
- 线程池任务应尽量是有界的工作单元。持续播放/解码不应由一个无限期运行的任务独占线程池 worker；应按可调度的处理阶段提交任务，并在数据、队列和输出背压条件允许时继续推进。
- 线程池线程数量、任务优先级和背压策略仍需结合目标平台与实际性能确定。

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

### 7.5.3 渲染线程与实时边界

- 视频 GPU 资源的创建、绑定、提交和销毁必须遵循对应图形 API 的设备/上下文线程约束；由平台 Renderer 专属渲染线程或明确拥有该图形上下文的线程执行。
- Qt GUI 线程负责窗口和 QWidget 生命周期、布局与用户交互，不负责 FFmpeg 解码或 GPU 呈现循环。渲染线程不得直接操作 QWidget。
- Android UI 管理 Surface 生命周期；Native 输出适配层响应 Surface 创建、变化和销毁，并确保 Surface 失效后不再提交帧。
- Asio 共享 thread_pool 负责媒体业务任务和解码调用调度，不承担持续的 GPU 呈现循环，也不应长期阻塞等待垂直同步或显示完成。
- 音频设备实时回调与视频渲染线程均不得执行阻塞文件 I/O、SQLite 操作或等待共享线程池任务。

### 7.5.4 Windows：Qt Widgets + Win32 + D3D11

Windows 视频 UI 采用 Qt 6 Widgets 管理播放器窗口和常规控件，视频画面区域使用独立的原生 Win32 子窗口承载 D3D11 Renderer。Qt 负责布局、菜单、控制栏、窗口模式及交互；D3D11 Renderer 只负责视频画面呈现。

- Video Container 由 Qt 管理尺寸和位置；其内部原生 HWND 作为视频显示目标。
- D3D11 Renderer 在该 HWND 对应的渲染线程创建并管理 Swap Chain，执行纹理采样、颜色转换、缩放和 Present。
- 首期 Windows 硬件解码优先使用 FFmpeg D3D11VA；DXVA2 保留为兼容性候选。
- 为避免解码纹理跨设备互操作，优先由 Windows Video Output 平台模块创建并管理 D3D11 Device，并将同一设备关联到 FFmpeg `AVHWDeviceContext` 与 Renderer。实际设备创建顺序和 FFmpeg 设备上下文接入方式以 D3D11VA 集成验证为准。
- D3D11VA 产生的硬件帧通过 FFmpeg 硬件帧引用进入 Core 帧队列；Renderer 在其渲染线程取得帧引用并访问对应 D3D11 资源。
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
        R["D3D11 Renderer Thread"]
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

### 7.7 Windows 多进程边界

Windows 每个播放器窗口运行在独立子进程中。每个子进程独立创建 Core Runtime，并拥有自己的 `io_context`、共享 `thread_pool`、Player Instance、Playback Session 和 FFmpeg 上下文。

不同子进程之间不共享线程池或可变媒体上下文。进程隔离用于窗口和资源生命周期隔离；每个进程内部仍按上述 Asio 共享线程池模型执行异步并发任务。

Android 首期采用单应用进程模型时，同样由该进程内的 Runtime 管理共享线程池和 Player 实例。

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

### 8.3 请求响应与异步执行

播放控制命令采用异步请求/响应模型：

1. UI 调用 C ABI，C ABI 将请求提交到 Runtime 的 `io_context`。
2. `io_context` 检查请求入口所需的参数和句柄，并协调该 Player 的命令顺序。
3. `io_context` 将实际业务任务提交到 Runtime 共享 `thread_pool`。
4. 业务任务完成后，将结果投递回 `io_context`。
5. `io_context` 将最终响应路由回发起该操作的原请求响应通道，并携带对应的 `operation_id`。

必须区分请求是否成功进入处理流程与业务执行的最终结果。请求响应属于原请求的响应通道，不属于 Event Manager 的主动事件流。C ABI 请求不得绕过 `io_context` 直接提交 `thread_pool`。

视频帧交付是独立的实时数据通道，不属于普通控制请求响应，也不属于主动事件通知。帧由 Core Video Output 与平台 Renderer 之间的内部平台输出接口交接；通用播放控制 C ABI 不逐帧传递图像数据。

同一 Player 的命令和状态变更遵守明确的提交顺序及状态机规则；这不要求该 Player 的所有异步任务串行执行。不同 Player 允许并发执行，同一 Player 内互不冲突的任务也允许并发。查询 API 返回调用时刻的一致状态快照，不应等待媒体解码或阻塞式文件读取完成。首期不要求所有查询都产生异步事件。

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
- `PLAYBACK_ENDED`：媒体自然播放结束。
- `ERROR`：非某个请求专属响应的播放过程错误或异步运行时错误。
- `MEDIA_INFO_UPDATED`：媒体信息在播放期间发生可独立通知的更新（如确有需要）。
- `POSITION_UPDATED`：可选的位置更新事件，频率需要限制，避免高频事件挤占其他主动事件。

`OPEN_COMPLETED`、`SEEK_COMPLETED` 等请求操作的完成结果通过原请求响应通道返回，不作为主动事件重复发送。若未来某项操作不由调用方请求触发，才根据其语义设计独立主动事件。

事件负载使用有明确布局和版本规则的 C ABI 结构体，不传递 C++ 对象、STL 容器或 FFmpeg 类型。扩展结构体时保留 `struct_size`，新增字段只能追加，不能改变既有字段的含义和布局。

### 8.5 回调线程与事件顺序

- Core 不在 Qt GUI 线程或 Android UI 线程执行事件回调。
- 回调由 Core 自己的事件分发机制调用，平台层负责转发到 UI 线程。
- 同一 Player 的事件按序分发，且同一 Player 的回调不并发执行；不同 Player 的回调允许并发执行。事件回调由 Runtime 的 Asio 异步分发路径协调，不在业务任务完成线程中直接调用平台 UI。
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

请求入口的同步返回仅表示请求是否成功提交；异步操作的最终成功或失败通过原请求响应通道返回。独立发生的运行时错误通过主动错误事件通知。错误码应稳定且可枚举，不能直接把 FFmpeg 内部错误值作为公开 ABI 错误码。需要展示的错误文本应通过稳定的错误码转换接口获得，或由错误响应/错误事件提供 UTF-8 描述。

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
- 两个平台对请求提交结果、原请求最终响应、主动事件、错误码和内存有效期的理解必须一致。

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

1. 复核并冻结 Player Core 对象模型与生命周期。
2. 根据 C ABI 契约设计并审查公共头文件 `core/include/lnplayer.h`。
3. 定义 Video Frame、Frame Queue、帧引用和时间线代次规则。
4. 实现平台 Video Output 抽象与 Renderer 帧获取接口。
5. Windows D3D11 Renderer：先显示测试纹理，再验证 D3D11VA 设备共享和硬件帧呈现。
6. PTS Scheduler、音频主时钟同步、Seek/Pause/Resize 和软件解码回退。
7. Android MediaCodec + SurfaceView 输出模式验证与平台实现。
8. 音频输出适配。
9. Asio 异步服务、共享线程池、任务并发与资源所有权。
10. 音乐库、SQLite 和文件扫描架构。
11. Windows 多进程启动与文件请求分发。
12. Windows/Android 平台 UI 与系统集成。
13. 工程目录、依赖管理和构建配置。

每完成一个专题，应同步更新本文和需求基线，并明确区分已确认决策与待验证方案。
