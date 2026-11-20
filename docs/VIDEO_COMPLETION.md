# Windows 视频版交付清单

> 本文件是 Windows 视频播放器的执行清单。状态必须以代码审查和实际验证为依据；“代码存在”不等于“已验收”。
> 开发分支：`gptdev`。不直接提交 `main`，不自动运行 CI。

## 范围

本阶段完成 Windows 本地视频播放器。保留视频文件中音轨的枚举和切换能力，但不扩展通用音乐播放器、音乐库、Android UI/JNI、多端同步、P2P 或远程控制。GPU 拔出、驱动重置和多显卡异常恢复暂缓到 Windows 功能完成后评估。

## P0 — Core 正确性

- [ ] Player / Runtime / Controller / Session / Backend 创建、关闭、销毁顺序。
- [ ] 异步任务退出屏障：关闭后不得再访问 Controller、Player 或平台对象。
- [ ] open、seek 与 play、pause、stop、close 的顺序及交错语义。
- [ ] operation_id、generation、状态快照和事件回调一致性。
- [ ] Session 锁与 FFmpeg demux、decoder、packet queue、字幕队列的锁边界。
- [ ] 同步视频帧 Pull 不得阻塞命令执行器或造成关闭死锁。
- [ ] D3D11 device/frame owner 的引用计数和跨线程生命周期。
- [ ] C ABI 参数、结构体版本、错误码、回调字符串/帧数据生命周期。
- [ ] 视频模式启动不依赖 SQLite、音乐库目录或数据库可用性。

## P1 — 字幕

- [ ] 内嵌文本字幕、ASS/SSA、PGS/DVD/DVB 位图字幕的格式支持范围和样本验证。
- [ ] 外挂 SRT/ASS/SSA 格式支持范围和编码样本验证。
- [x] ASS/SSA 渲染采用 FFmpeg libavfilter 的 subtitles/libass filter，不重复实现 ASS Script Info、Styles、Events、Dialogue 自定义解析器。
- [ ] 验证 libass 字体、字号、粗斜体、颜色、透明度、描边、阴影和字体回退。
- [ ] 验证对齐、边距、定位、裁剪、换行、常用 Override Tags、Transform、Move、Fade、Rotation。
- [ ] 验证 Karaoke、容器内嵌字体附件和系统字体回退。
- [ ] 内嵌/外挂字幕统一轨道、时间轴、延迟、启用状态和渲染接口。
- [ ] 字幕在 Seek、切换媒体、切换轨道、逐帧和历史帧浏览时正确。

## P1 — 视频变换、色彩与 HDR

- [ ] Display Matrix 旋转在硬件/软件路径一致。
- [ ] SAR/DAR、旋转后尺寸和显示比例正确。
- [ ] BT.601/709/2020、Full/Limited Range 转换验证。
- [ ] PQ/HLG、HDR10 Mastering Display、MaxCLL/MaxFALL 元数据处理。
- [ ] HDR 到 SDR Tone Mapping 策略、参数和边界验证。
- [ ] SDR/HDR 输出路径和截图色彩一致性。
- [ ] 不支持的色彩/硬件路径有明确软件回退。

## P1 — 视频滤镜

- [ ] 统一滤镜参数模型和处理阶段。
- [ ] 定义滤镜执行顺序与组合语义。
- [ ] 亮度、对比度、饱和度、色调、锐化、降噪、灰度效果验证。
- [ ] 滤镜与 HDR、色彩转换、旋转、字幕叠加的组合验证。
- [ ] 实时参数调整、重置和设置持久化。
- [ ] 评估高级滤镜接口，避免把处理逻辑耦合进窗口类或 Renderer 菜单代码。

## P1 — 播放控制与 UI

- [ ] 连续 Seek、Seek 期间命令、EOF 和旧帧丢弃语义。
- [ ] 播放/暂停/停止/关闭及失败状态恢复。
- [ ] 播放速度变化和时钟重新锚定。
- [ ] 逐帧历史缓存、历史帧 PTS、字幕和进度显示一致。
- [ ] 截图格式、路径、分辨率和色彩。
- [ ] 播放队列持久化、恢复、随机和循环行为。
- [ ] 多文件打开、拖放、队列编辑和媒体切换。
- [ ] 菜单、快捷键、鼠标操作及状态同步。
- [ ] MainWindow 仅保留窗口/UI 协调；评估队列和帧历史协调是否值得独立组件化。

## P2 — Windows 交付

- [ ] 一个窗口一个独立进程、每进程一个 Player。
- [ ] 多窗口并行播放和独立退出。
- [ ] 命令行多媒体路径与文件管理器批量打开。
- [ ] Qt、FFmpeg DLL 和其他运行时依赖部署。
- [ ] 安装包、卸载、文件关联和默认播放器设置。
- [ ] 发布目录在干净 Windows 环境运行。

## P2 — 验收

- [ ] Core 状态机与命令顺序测试。
- [ ] 生命周期、关闭屏障和回调竞态测试。
- [ ] FFmpeg 探测、解码、Seek、EOF 和错误样本测试。
- [ ] 字幕解析、时间轴和渲染测试。
- [ ] 色彩转换、HDR 和滤镜测试。
- [ ] D3D11 硬件帧与软件回退测试。
- [ ] 多窗口、长时间播放、资源占用和安装环境测试。
- [ ] Windows 完整功能清单逐项验收。

## 当前状态记录

- 已存在基础 FFmpeg 视频解码、D3D11 呈现及软件回退。
- 已存在视频多音轨、内嵌字幕基础解码、外挂字幕基础解析。
- 已存在旋转、基础色彩矩阵/范围、基础 PQ/HLG Tone Mapping 和基础 D3D11 滤镜。
- 已存在视频播放队列 QSettings 持久化、逐帧和截图基础能力。
- 上述“已存在”只表示代码路径存在；除非补充验证记录，不视为验收通过。
- GPU 设备拔出、驱动重置、多显卡切换恢复暂不纳入本阶段。

## 接管开发记录

### 已修正

- [x] 将 Windows D3D11 Device 变更操作从直接调用 PlaybackSession 改为通过 PlaybackController 的同步命令入口，并在其 worker strand 上执行。此前该平台操作绕过命令执行器，可能与帧读取或其他媒体操作竞争；现已统一串行化。

提交：`addbfb0b3293711c8d6c79b817aa6454b875126b`。

### 仍需重点审查

- Controller 当前使用 `std::promise/std::future` 适配同步 C ABI；需检查异常路径是否总能完成 promise，避免调用线程永久等待。
- `is_current_player()` 仅在全局锁内验证句柄，锁释放后调用者继续使用裸指针。需要明确并实现 API 调用与 `lnplayer_destroy()` 并发时的生命周期契约。
- 音轨/字幕枚举、快照读取和音量/速度设置直接访问 Session；虽然 Session mutex 提供数据互斥，仍需确认与 Controller 命令顺序的语义要求。
- `PlaybackSession::set_video_device()` 在持有 Session mutex 时执行 backend close/open/seek/play，操作耗时；需评估是否阻塞其他查询或回调，以及是否存在回调重入。
- `Runtime::shutdown()` 当前先 join thread_pool，再释放 work guard 并 join io thread；需通过生命周期测试证明 worker completion handler 能被完整 drain，且不会等待已停止的执行器。
- 当前代码审查未发现上述项目均已解决；不得将它们标记为完成。


### 2026-11-21 — C ABI Player 句柄并发销毁

- [x] 所有接收 `lnplayer_t*` 的 Player API 入口通过 `PlayerAccess` 获取活动调用计数。
- [x] `lnplayer_destroy()` 在全局锁下停止新调用进入，等待已经进入的 API 调用退出，再关闭 Controller 并销毁 Player。
- [x] 通过线程局部活动调用计数，禁止在帧回调/库回调等同步重入路径中销毁同一个 Player，避免等待自身造成死锁。
- [x] 从 Event Thread 调用 destroy 仍返回 `LNPLAYER_RESULT_WOULD_DEADLOCK`。
- [ ] 需要在 Windows 上补充并发压力测试：多个查询/控制线程与 destroy 竞争、回调重入、重复 destroy、销毁期间创建新 Player。
- [ ] 需要检查所有 C ABI 入口是否都受同一生命周期保护，并检查回调期间的字符串/帧引用有效期。

提交：
- `e639ccfaf96eaaf0b5a16ff903f112b57643d32a` — Player API 活动调用保护与销毁等待屏障。
- `6e4850e5a2fb3cf70b2041687628b867d8a3238d` — 阻止从活动 API 回调中重入 destroy。

本次只完成代码修改和静态审查，没有运行构建或测试；因此并发屏障仍需 Windows 实际验证后才能标记验收通过。

### 2026-11-21 — C ABI 同步完成与回调边界

- [x] `invoke_sync()` 在 worker 已完成操作但无法向 io_context 投递完成处理器时，直接完成 promise，避免同步调用线程永久等待。
- [x] Windows 视频帧 Pull 在完成结果无法投递时也会完成等待者，并返回明确错误。
- [x] 事件、视频帧、音乐库和音轨/字幕枚举回调统一通过异常隔离入口调用，避免 C++ 异常穿越 C ABI 或逃出 Asio handler。
- [ ] 仍需验证所有异步投递失败路径，尤其 open/seek 的异步完成事件投递。
- [ ] 仍需在 Windows 构建和并发测试中验证这些修改。

提交：
- `4188c8d74b78df7a84b2067b7c00b0ae155af1d3` — 同步请求完成投递失败时释放等待者。
- `1a7154b6677545d55c89b0c547deeaf4b076b2b2` — 隔离外部 C ABI 回调异常。

- [x] 异步 open/seek 投递到 worker strand 失败时，恢复对应状态并报告操作完成错误，避免 io_context handler 因投递异常直接退出。
- [x] 将 open 的媒体路径复制和同步命令 promise 分配纳入异常处理范围，避免分配失败穿越 C ABI。
- [ ] worker 已完成但无法将 open/seek 完成事件重新投递到 io_context 的极端内存分配失败路径，仍需后续统一设计可观测的故障通道。

相关提交：
- `f24ea9cdfd6d2f0e3695f8bd255f308923aadcd7` — 异步 open/seek worker 投递失败处理。
- `69a5c94b029391a3417f6903a31c432740300be0` — open 路径和同步 promise 分配异常边界。

### 2026-11-21 — 关闭屏障失败时的析构顺序

- [x] Controller 正常关闭后使用栈上 promise 建立 io_context drain barrier，避免为屏障本身额外分配共享状态。
- [x] Controller 关闭未能建立屏障时，先关闭 Runtime：停止 thread_pool 生产者并排空 io_context，再销毁 Controller。
- [ ] 需要覆盖 close 失败、投递失败和 Runtime drain 的故障注入测试。

提交：`25f7df3267b923ae181e770a69314ec60ec9688d`。

验证说明：已通过 GitHub 文件回读确认修改位于 `gptdev`。此前一次更新因遗漏 branch 参数短暂创建在 `main` 的文件提交，已立即通过带 expected SHA 的 ref 回退恢复 `main` 原 HEAD；该修改没有保留在 main。今后所有写操作均显式指定 `branch: "gptdev"`。

### 2026-11-21 — 旋转后显示比例

- [x] 软件解码路径执行 90°/270° BGRA 旋转时同步取显示宽高比的倒数，避免像素已旋转但 UI 仍按原始横屏 DAR 缩放。
- [ ] 需要使用带 SAR 的 90°/270° 旋转样本验证显示尺寸和截图。

提交：`17944b0df908ca0ff3a689f32032d67f93f5218d`。

### 2026-11-21 — Windows 构建、安装与生命周期测试

- [x] Windows 安装树部署 LNPlayerApp、Core DLL 和 FFmpeg DLL。
- [x] 使用 Qt 的部署脚本收集 Qt runtime 与 plugins。
- [x] 增加 MSVC runtime 安装规则。
- [x] 增加 NSIS CPack 配置、开始菜单视频模式快捷方式、视频文件 Open With / RegisteredApplications 注册。
- [x] 卸载时只清理 LNPlayer 自身注册项，不覆盖用户默认应用选择，也不删除 Windows 共享注册表根键。
- [x] 新增可选的 C ABI 生命周期测试：回调内重入 destroy、并发 API 调用与 destroy、销毁后重新创建。
- [ ] 仍需在 Windows MSVC/Qt 环境配置、编译和运行测试，并实际生成 NSIS 安装包验证注册表及卸载行为。

构建测试：
- 配置时启用 `-DLNPLAYER_BUILD_TESTS=ON`。
- 构建 `LNPlayerLifecycleTests` 后手动运行 CTest；不会自动触发 CI。

相关提交：
- `4530386c0aa8c46761f63b13ff2b277615779607` — Windows CPack/NSIS 初始配置。
- `5d0cbcf60210fa3e26d9005fe8e9a3ebbcaf6269` — Qt 与 FFmpeg runtime 部署。
- `a368f4e8a8b39a41187082f83a0ea440bf06cbe4` — 可选测试构建入口。
- `95eb54e1e6e3d3a15bcabf609dd1a25c0f1b46d7` — C ABI 生命周期测试。
- `f5e4c84fdf7d2b4fb159ab948352b1d5418ea4f5` — HDR peak luminance 传入 shader 并用于基础 tone mapping。

### 2026-11-21 — HDR tone mapping 参数链

- [x] 修复 MainWindow 已传递三参数但 VideoSurface / D3D11VideoRenderer 仍只有双参数声明的接口不一致。
- [x] 将 MaxCLL / Mastering peak 传递到 D3D11 pixel shader。
- [x] PQ EOTF 使用 10000 nit 标准量纲。
- [x] 使用 peak-aware extended Reinhard 基础曲线处理 HDR 到 SDR。
- [ ] PQ/HLG 色彩测试图、不同 MaxCLL/MaxFALL 内容和实际显示器输出仍需验证；当前属于基础映射，不等于完整 HDR10+ 动态元数据或显示器 HDR 输出支持。

### 2026-11-21 — Windows 视频偏好设置

- [x] 视频滤镜参数（亮度、对比度、饱和度、色调、锐化、降噪、灰度）使用 QSettings 持久化并在启动时恢复。
- [x] 播放速度持久化，并在创建 Player 后恢复。
- [x] 画面比例覆盖、拉伸模式持久化并恢复。
- [x] 截图记住上次保存目录，支持 PNG/JPEG/BMP，并反馈保存成功或失败。
- [x] 文件打开过滤器和 NSIS Open With 注册扩展常见视频容器后缀。
- [ ] 上述偏好设置需要在 Windows UI 手动验收，尤其检查多窗口共享设置是否符合预期（当前 QSettings 按应用/用户共享）。

相关提交：
- `481c10b0ac1a36953b3de88b5de149c20b0491d4` — 视频滤镜参数持久化。
- `dea4b2fd8ba90d67e0920baf4551d3a4167f3be9` — 截图目录与保存结果反馈。
- `3aa86d85072f3f41aab0600bd62c44d65b819444` — 播放速度持久化。
- `4cf417d1cc37119e32459f0f2b49550e3e2ec544` — 画面比例与拉伸模式持久化。
- `aea8e1147339a5e1fc7409949008e14bac1bf0dd` — 视频文件选择过滤器扩展。
- `940718b61a5c6cc3b1e65740eec88a97ff90b8a3` — 安装包文件关联扩展。

### 2026-11-21 — Runtime 两阶段关闭排空

- [x] Runtime 关闭前先在 io_context 建立屏障，确保已排队的 io handler 不会在 thread_pool join 开始后继续提交新 worker 任务。
- [x] 屏障成功后等待 thread_pool 完成并投递完成回调，再释放 work guard，让 io_context 排空剩余完成回调。
- [x] 屏障投递失败时先停止并 join io worker，再 join thread_pool，避免 io handler 在 join 期间继续生产任务。
- [x] 生命周期测试增加“异步 open 尚未完成时 destroy，确保 OPEN_COMPLETED 回调在 Controller 销毁前排空”的用例。
- [ ] 上述关闭路径仍需 Windows Debug/Release 下运行压力测试与故障注入。

提交：
- `c4d99cd43c7bf99ff5e56d00e466d7b4c729b873` — Runtime 两阶段排空。
- `03897d786b29f076181c03cbd20a7021af616048` — 异步 open 与 destroy 竞态测试。

- [x] 修正 NSIS 注册的 ProgID 和 Applications Open 命令，双击视频文件或从“打开方式”启动时显式传入 `--video`，避免进入默认音频模式。

- [x] HDR mastering display 与 Content Light Level 元数据优先读取逐帧 side data；逐帧缺失时回退读取 AVStream side data，覆盖只在流级声明 HDR10 元数据的文件。
- [ ] 仍需用不同容器封装的 HDR10 样本验证 side data 的实际布局和单位。

提交：`1acb3a351159e975fe63305626ec3e38d048cd8a`。

字幕实现说明：当前代码通过 FFmpeg 的 `subtitles` libavfilter 调用 libass，覆盖 ASS/SSA 的样式和动画能力；不应再开发自定义 ASS parser/renderer。是否能在发布包启用该功能取决于 FFmpeg SDK 是否编入 libass filter，必须在 Windows 发布构建中确认。

- [x] 播放音量及静音前音量使用 QSettings 持久化；Player 创建后在 Core 命令线程恢复音量，UI 控件只在 GUI 线程读取。
提交：`992550cbffa056523d9003b612a78e5d807c19be`（后续线程边界修正：`501cfef55d1d93ea22ec07d27efb44c15c075dfe`）。

### 2026-11-21 — Windows 字幕单元测试

- [x] 新增 Qt Core 字幕测试目标，覆盖 SRT 时间区间、换行、结束边界、正向延迟、禁用状态。
- [x] 覆盖 UTF-16LE BOM 解码。
- [x] 覆盖 ASS fallback 对 Dialogue 字段、逗号、Override Tag 和换行的基础处理。
- [ ] 尚未在 Windows/Qt 环境编译或运行；libass 的完整 ASS 样式和特效仍需集成测试，不由 fallback parser 测试代替。

提交：`8154dfd93a18b4e5d4b91b262de95174c4ff7e04`、`ca1051775937547962d2acad2db24f4168858173`。

### 2026-11-21 — 高刷新率视频调度

- [x] 视频呈现 timer 使用 1ms PreciseTimer，避免固定 16ms 轮询把高帧率视频限制在约 60fps。
- [x] D3D11 Device 变更检测拆到独立 250ms timer，避免高频呈现循环每帧重复 COM AddRef/Release。
- [x] UI 以 QElapsedTimer 在 Core 快照之间插值媒体时钟，避免 250ms 快照周期造成帧呈现等待误差。
- [x] 播放速度改变时先锚定当前插值位置，再重启 UI 时钟，避免速度切换造成时钟跳变。
- [ ] 需要用 24/30/60/120/144/240fps 视频和 60/120/144/240Hz 显示器组合验证；Present/VSync 行为仍依赖显示设备。

提交：`aede919c7d256750930c7926938c12261c44e9f7`、`7a9c9ec52fb52d2944b60c2307152eecde381fbe`。

- [x] Windows EXE 增加 VERSIONINFO 资源，包含产品名、文件描述和项目版本号，便于文件属性与安装程序识别。
- [ ] 最终发布前需确认版本号策略及正式图标/签名；当前版本仍为 0.1.0，未进行代码签名。

提交：`7dc009c1fe0376f0ae1b5b63ff524cfaf16326ce`。

- [x] D3D11 Device identity 的所有写入统一回到 GUI 线程；初始化时在 GUI 线程取得 COM 引用并转移给 Core，避免 UI timer 与命令 worker 并发读写普通指针。
提交：`1a9d2aca59a524d29901d366a1b3d200f33c1118`。

### 2026-11-21 — 视频帧读取与播放命令解耦

- [x] PlaybackController 增加独立 video_frame_strand，与播放命令 strand 分离；仍共享固定 2-worker thread_pool。
- [x] PlaybackSession backend 所有权改为 shared_ptr，帧读取只在锁内获取 backend 和 generation，不再持有 Session mutex 执行解码。
- [x] 媒体替换、Seek、Stop、Close、轨道/字幕切换在操作前推进 generation；解码结束后若 generation 已变化则丢弃旧帧并携带新 generation 通知 UI 重取。
- [x] MainWindow 增加专用 frame_worker QThread；同步视频帧 Pull 与硬件帧 fallback conversion 不再占用 command_worker。
- [x] 窗口销毁先停止 frame worker 并等待当前帧请求退出，再销毁 Player。
- [x] 新增 Session 并发测试：帧解码阻塞时 Pause/Seek 仍能完成；Seek 跨代帧必须被丢弃。
- [ ] 尚未在 Windows 下运行测试；需要高负载软解码时验证 Pause/Seek/Close 延迟与资源释放。

提交：
- `13b37314ebccfcc7f8b7beb75cce397bce3b62be` — 独立帧解码 strand。
- `a72ec890417942e26c62d1d85b33b1f163ac84d2`、`10edc88ed7719c2dca346341561083b67dc7da65` — UI 专用 frame worker。
- `6abe1e3088a546929a4e2ea98946959e251cd58c` — 字幕设置变更推进 generation。
- `41936aa343905ef3008d6392853a6ac9fc2a9217`、`0c5d171152868db32ea933fdb906e2f9f5dd20bc`、`6241cb248430c7ee3834aaac457f47330ed53bf8` — stale frame 通知与 UI 重取。
- `3c11bbb7c2df104868d827f3cc753307277e8189`、`d15398381fe2ba2734dea5e7ab19b3e01b3f6312` — Session 并发回归测试。


### 2026-11-21 — C ABI 错误通道不再分配内存

- [x] 将线程局部 last-error 从动态分配的 `std::string` 改为固定容量缓冲区，确保错误处理路径本身不会因内存分配失败再次抛出异常。
- [x] 对音乐库目录添加/移除入口补齐异常边界，分别映射内存不足和未知异常为 C ABI 结果码。
- [ ] 需要在 Windows MSVC 下构建，并补充错误缓冲区截断及 last-error 读取测试。

提交：`6efe29f84a94ca4eb4ae86158fa41d25696bb188`、`d21d2d0813e130b5d7762ba45d3ca8360196c987`。

- [x] 生命周期测试增加 last-error 两阶段读取回归用例，验证所需缓冲区长度和原始诊断内容。
提交：`dacf84168a12d913632c14ceb24c07361143668e`。


### 2026-11-21 — PotPlayer 风格全屏控制栏交互

- [x] 将进度条、播放控制区和状态信息组合为独立 control panel，供全屏模式统一显隐。
- [x] 全屏进入时显示控制面板并启动 2 秒空闲计时。
- [x] 视频画布鼠标移动唤出控制面板；鼠标进入控制面板及其子控件时暂停隐藏计时，离开后重新计时。
- [x] 空闲超时后隐藏控制面板、菜单栏和播放队列侧栏；退出全屏恢复窗口模式 UI。
- [ ] 尚未进行 Windows 编译和实际鼠标交互验收；需验证多显示器、高 DPI、菜单弹出、触控板和控制栏边缘行为。
- [ ] 后续仍需加入底边唤出策略、独立队列显示状态持久化、双击/单击策略及鼠标手势配置。

实现提交：
- `2c3726b29c24bacb733d2785afdf4d44e209e64f` — MainWindow 全屏交互状态成员。
- `c2f09e7a89990db091637d28e459d67ac726e0d5` — 全屏控制面板自动隐藏和鼠标唤出。


### 2026-11-21 — 播放列表基础交互补齐

- [x] 增加播放列表显示/隐藏菜单入口，并持久化用户偏好。
- [x] 增加播放列表清空按钮和菜单命令。
- [x] 清空时清除当前媒体 UI 状态、帧历史和待处理帧，并异步关闭 Core 当前媒体。
- [x] 全屏控制栏自动隐藏时遵循用户保存的播放列表可见偏好；自动隐藏不覆盖用户偏好。
- [ ] 尚未完成 Windows 构建和队列交互运行验收。
- [ ] 仍需实现队列右键菜单、批量操作、文件信息列、独立队列窗口和拖动插入提示。

提交：
- `02e3b68513529e088720c8836abb23b20eb8525d`、`13f5aef83aede33242ac19715b62489928b392c5` — 队列显隐和清空入口。
- `5e4b74b5a86f94e2ff77d4d7f423d820c836605c` — 清空状态与全屏显隐修正。
- `248ace6ec9e91f3991a223de4d4bf78c33301219` — 初始化时先恢复队列显示偏好。


### 2026-11-21 — 视频快捷键扩展与焦点保护

- [x] 增加上一项/下一项（PageUp/PageDown）、30 秒定位（Ctrl+方向键）、打开文件（F3）、播放列表显隐（F6）、倍速减/增/复位（X/C/Z）、截图（K）。
- [x] 播放器快捷键在文本输入框和列表控件获得焦点时不抢占输入/列表导航。
- [ ] 当前仍是集中绑定在 MainWindow 的默认快捷键，不是可配置快捷键系统；快捷键映射编辑、冲突检测和恢复默认尚未实现。
- [ ] 尚未在 Windows 实机验证按键焦点和系统快捷键冲突。

提交：
- `677197f44ee320dfa9853864af071fa943f80587` — 播放列表 QAction 状态引用。
- `13e1511edbc53db6aa96abf00f1ea1e09491c883` — 快捷键扩展和焦点保护。


### 2026-11-21 — 视频设置窗口基础版

- [x] 增加独立视频设置对话框，提供界面、视频和快捷键说明页。
- [x] 界面页可设置全屏控制栏自动隐藏延迟（关闭或 1–10 秒）及播放列表侧栏默认显隐。
- [x] 视频页可设置原始比例、16:9、4:3、拉伸，以及亮度/对比度/饱和度。
- [x] 设置写入 QSettings；确认后应用，取消不写入；恢复默认可重置当前表单。
- [x] 增加 F5 设置入口。
- [ ] 快捷键页当前为默认映射说明，尚不支持编辑/冲突检测。
- [ ] 字幕样式、截图目录、播放行为、性能/解码配置页仍未完成。
- [ ] 尚未进行 Windows 构建或 UI 运行验收。

提交：
- `1578aee30e05a99ea90563284520f6515aa9f9ad` — 设置窗口入口与延迟配置状态。
- `3fee496b9199e695d817a7e2e9b7674748455fec` — 视频设置对话框和 F5 快捷键。
