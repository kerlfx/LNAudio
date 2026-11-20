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

- [ ] 内嵌文本字幕、ASS、位图字幕格式支持范围确认和验证。
- [ ] 外挂 SRT/ASS/SSA 格式支持范围确认和验证。
- [ ] ASS Script Info、V4/V4+ Styles、Events、Dialogue 解析。
- [ ] 字体、字号、粗斜体、颜色、透明度、描边、阴影。
- [ ] 对齐、边距、定位、裁剪、换行和绘制区域。
- [ ] 常用 Override Tags、Transform、Move、Fade、Rotation。
- [ ] Karaoke 基础效果。
- [ ] 字体加载与回退策略。
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
