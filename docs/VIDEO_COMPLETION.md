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
