# LNPlayer 性能审查

审查分支：`gptdev`

审查日期：2026-11-21

审查范围：Windows Qt UI、UI Asio 调度、Core C ABI / PlaybackSession、FFmpeg 视频帧读取、D3D11 呈现与帧历史。

本审查基于当前分支源码静态检查，不代表 Windows 性能基准测试。未执行 MSVC 构建、视频播放压测或 GPUView/ETW/PIX 分析。

## 结论摘要

| 优先级 | 问题 | 状态 |
|---|---|---|
| P0 | 软件帧最终呈现使用 command Strand，可能让垂直同步等待阻塞播放控制 | 已修复，待运行验证 |
| P0 | 硬件解码每帧无条件分配、清空并上传全尺寸透明字幕图层 | 已修复，待运行验证 |
| P0 | 软件解码帧历史固定保留 120 张完整 BGRA 图像，4K 理论上约 3.71 GiB | 待修复 |
| P1 | 25ms 快照轮询运行在 command Strand，Session 快照锁与完整视频帧读取锁竞争 | 待设计和修复 |
| P1 | 视频帧读取在 Core Session mutex 内执行完整 FFmpeg read，可能让同步控制操作等待解码/解封装 | 架构约束，待优化 |
| P1 | D3D11 硬件路径每帧调用 Flush，且每帧创建多个 D3D 对象 | 待实测评估 |
| P2 | 播放列表重建在 GUI 线程逐项创建 QListWidgetItem 并查询 QFileInfo | 待优化 |
| P2 | Renderer 资源释放轮询 GPU event query 并 Sleep(0)，可能在关闭/设备恢复时持续占用 CPU | 待优化 |
| P2 | 软件解码每帧执行完整 BGRA copy、字幕合成和 GPU 上传 | 待基准测试 |
| P2 | FFmpeg decoder 未显式配置 thread_count，自动线程数可能与 UI/Core worker 叠加 | 待实测评估 |

## 已修复

### P0 — 软件帧呈现占用 command Strand

原实现：软件帧完成字幕合成后，使用 `postCommand()` 执行 `lnplayer_get_snapshot()`、D3D11 上传和 `Present(1, 0)`。

影响：
- `Present(1, 0)` 使用同步间隔 1，可能等待显示刷新。
- command Strand 同时承载播放/暂停/停止/Seek 等控制调用。
- 当呈现等待垂直同步时，后续控制任务会排在同一 Strand 后面。

修复：软件帧最终呈现改为提交到 `frame_executor_`（`postFrame()`），与命令控制调度隔离。帧读取和呈现仍保持单一有界帧流水线，不增加并发帧数。

提交：`3f8a21be8c7cc86e8ec170c2a4c36471b6df23ab`。

### P0 — 硬件路径无字幕时仍创建全屏透明图层

原实现：每帧无条件创建 `QImage(overlay_size, Format_ARGB32)` 并清空透明，然后传入 D3D11 硬件呈现。Renderer 会为非空 overlay 执行动态纹理 Map、整屏逐行 memcpy、Shader 合成。

影响：
- 无字幕时仍产生整张显示区域大小的 CPU 图像分配和清零。
- 仍执行全尺寸 GPU 动态纹理上传及额外合成 Draw。
- 4K BGRA overlay 单帧约 31.6 MiB；60 FPS 时仅 overlay 数据就约 1.85 GiB/s（未计额外内存读写）。

修复：仅在字幕实际启用且当前帧存在文本或位图字幕时创建 overlay；无字幕时传递空 QImage，使 Renderer 跳过 overlay 上传和合成。

提交：`3f8a21be8c7cc86e8ec170c2a4c36471b6df23ab`。

## 待处理问题

### P0 — 软件帧历史存在极高内存上限

位置：`windows/main_window.cpp` 的 `presentPendingVideoFrame()`。

软件路径每帧把 BGRA 图像加入 `video_frame_history_`，最多保留 120 帧。帧通过 `QImage::copy()` 从 Core 回调缓冲区复制，因此历史中的每一帧都是不同的完整图像缓冲区；QImage 隐式共享不能消除不同帧之间的内存占用。

理论内存（仅历史图像像素，不含解码器、临时帧和容器开销）：

| 分辨率 | 单帧 BGRA | 120 帧 |
|---|---:|---:|
| 1920×1080 | 7.91 MiB | 949 MiB |
| 2560×1440 | 14.06 MiB | 1.65 GiB |
| 3840×2160 | 31.64 MiB | 3.71 GiB |
| 7680×4320 | 126.56 MiB | 14.83 GiB |

硬件路径虽然不保存 CPU BGRA，但历史项保留 AVFrame owner，可能长期占用 D3D11 解码表面，影响解码器表面复用。

建议：
1. 将历史容量从固定帧数改为明确的内存预算，同时保留合理的最大帧数上限。
2. 软件帧按 `QImage::sizeInBytes()` 累计；达到预算后从最旧帧开始淘汰。
3. 硬件帧历史另设较小上限，避免长期持有过多解码表面。
4. 明确逐帧后退的历史范围；超出缓存范围时再考虑重新 Seek/解码，不应靠无限增加内存解决。

### P1 — 快照轮询与播放控制争用

位置：
- `windows/main_window.cpp`：视频模式打开成功后将 `snapshot_timer_` 设置为 25ms，并调用 `refreshSnapshot()`。
- `refreshSnapshot()`：使用 `postCommand()` 执行 `lnplayer_get_snapshot()`。
- `core/lnplayer.cpp`：Controller 的 `get_snapshot()` 调用 `session_.snapshot()`。
- `core/playback_session.cpp`：`snapshot()` 与 `read_video_frame()` 共用 `mutex_`；`read_video_frame()` 持锁执行整个 backend frame pull。

影响：
- 视频播放期间最多每秒触发约 40 次快照查询。
- 快照可能等待当前完整帧读取结束。
- 查询任务在 command Strand 上等待时，后续播放控制任务被排在它后面。
- 即使快照最终只读取少量字段，锁等待时间仍由 FFmpeg 解码/解封装路径决定。

建议：
1. 首选 Core 将可查询快照与 FFmpeg backend 生命周期锁分离；快照只读原子/独立锁保护的缓存状态，不进入解码锁。
2. UI 位置显示使用本地媒体时钟插值，状态变化使用 Core 事件更新；减少周期性 C ABI 查询。
3. A-B 循环边界必须独立于低频 UI 快照轮询，使用媒体时钟/帧 PTS 定时触发定位。
4. 不应简单将 25ms 改为更大的固定间隔作为最终修复；这会降低锁竞争频率，但可能加重 A-B 循环越界和 UI 进度显示延迟。

### P1 — Session mutex 覆盖整个视频帧读取

`PlaybackSession::read_video_frame()` 在持有 Session mutex 时调用 `backend_->read_video_frame()`。该 backend 路径会解码、必要时解封装读取数据，并可能执行像素格式转换和字幕数据更新。

这是为了保护 FFmpeg context 生命周期和 Seek/Close 并发安全而采取的粗粒度串行化；不能直接移除该锁。

优化方向：
- 保留 FFmpeg context 单一所有者/串行操作规则。
- 把可独立读取的快照状态从 backend 生命周期锁拆出。
- 对网络/损坏媒体的阻塞读取建立可取消的 interrupt callback 和明确的关闭/Seek 中断语义。
- 评估解码结果发布与 Session 状态锁的边界，避免持有状态锁执行像素转换等较长操作。

### P1 — D3D11 硬件呈现每帧 Flush 和对象创建

硬件路径每帧：
- 创建 event query。
- 创建 Video Processor input view。
- 获取 swap-chain back buffer。
- 创建 Video Processor output view。
- 执行 VideoProcessorBlt。
- 调用 `context_->End()` 后立即 `context_->Flush()`。
- 再进行字幕 overlay 上传/合成、CopyResource 和 Present。

风险：
- 每帧 COM/D3D 对象创建有 CPU 开销。
- 每帧 Flush 可能降低驱动批处理效率；Present 本身也会提交工作，但移除 Flush 前必须验证 query 与 AVFrame owner 的 GPU 完成语义。
- output view / input view 缓存需要严格处理纹理和设备生命周期。

建议通过 ETW/GPUView/PIX 比较每帧 CPU 时间、GPU 队列深度、Flush 时间和 query 完成延迟后再改；不能仅凭 API 名称断言 Flush 一定是瓶颈。

### P2 — 播放列表大队列重建阻塞 GUI

`rebuildPlaylistWidget()` 在 GUI 线程清空并逐项创建 `QListWidgetItem`，每项调用 `QFileInfo` 获取绝对路径/文件名，并调用元数据更新逻辑；元数据更新还会读取文件大小并构造 Tooltip。

大队列导入后可能出现明显 UI 卡顿。后续建议改为 `QListView + QAbstractListModel`，按可见行绘制，并将文件属性收集放到后台；避免每次队列小改动都完整清空重建。

### P2 — D3D11 资源释放轮询 GPU query

`release_resources()` 在持有 Renderer mutex 时循环调用 `GetData()`，返回 `S_FALSE` 时执行 `Sleep(0)`，直到所有 pending hardware frame 完成。

这不是播放常态路径，但设备重建、窗口关闭或渲染器释放时可能持续消耗 CPU，并阻止其他 capture/present 调用获取 mutex。后续应评估事件/有界等待策略，并确保不会提前释放 GPU 仍在使用的 AVFrame owner。

### P2 — 软件解码完整帧搬运

软件解码路径使用 `sws_scale` 输出完整 BGRA 缓冲区，Windows 回调再 `QImage::copy()`，后台字幕合成后通过 D3D11 dynamic texture Map 逐行上传。

这是当前软件渲染路径的主要带宽成本。先测量 1080p/4K/8K 下 CPU 转换、QImage copy、字幕合成和 GPU upload 分项耗时，再决定是否引入缓冲池或共享缓冲区；不能在未明确回调缓冲区生命周期前直接去掉 copy。

### P2 — FFmpeg decoder 线程预算

当前视频和音频 decoder 在 `avcodec_parameters_to_context()` 后直接 `avcodec_open2()`，源码未显式设置 `thread_count`。FFmpeg 默认自动线程数可能与 Core 2-worker、UI 3-worker、UI/Core io 线程及其他系统线程叠加。

这属于需要在目标机器实测的线程过量风险，不应在未测试不同 codec/profile 的情况下直接固定为 1 或 2。建议记录 decoder 实际 `thread_count`，比较自动、合理上限和 codec 默认策略下的 CPU 占用、帧解码耗时与丢帧。

## 验证计划

本地 Windows 实测时记录：

1. 1080p H.264、4K HEVC、4K AV1（软件/硬件能力允许时）、60/120/144/240 FPS。
2. 有字幕/无字幕、SRT/ASS、软件/硬件解码。
3. 播放时连续点击暂停/停止/Seek，记录控制命令提交到生效的延迟。
4. 观察 Private Bytes、Working Set、GPU Dedicated/Shared Memory，持续播放至少 30 分钟。
5. 逐帧前进/后退和长时间播放后观察帧历史内存。
6. 使用 ETW/GPUView/PIX 记录 Present 等待、GPU 队列、D3D11 Flush 和 query 完成情况。
7. 大型播放列表（1k/10k 项）导入、滚动、删除和排序时记录 GUI 线程响应时间。

禁止自动运行 CI。所有未实际运行的项目保持“待验证”。
