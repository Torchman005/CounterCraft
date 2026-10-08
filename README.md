# CounterCraft

目标：在 **CS2 本机离线模式**中接入真实 Minecraft Java 模拟，包括方块、合成、生物等系统。

**当前状态：已在真实离线 CS2 中显示 MC 诊断预览，尚不能游玩完整 Minecraft。** 已实现 Python 协议诊断端点、坐标转换、环境检查，以及 Fabric 1.20.1 相机接收、世界颜色/深度导出、GPU 异步读回和有界本机帧流。CS2 的真实相机、世界深度融合和玩法输入尚未接通。

插件 `universal-modder 0.2.0` 已核验安装启用。完整 Fabric 构建成功，已生成模组 jar。在独立单人测试世界中，相机请求、实际渲染参数、真实地形深度和释放后的恢复通过。新增帧流用三槽 PBO/fence 异步读回，通过本机 TCP 发送 RGBA、深度和同帧矩阵；慢消费只保留最新帧。这仍是 CPU 拷贝原型，不是 GPU 共享纹理。

当前验证：18 项 Python、8 组原生 CTest 和 26 项安装/恢复/Steam 启动检查通过；未改动的 Java 代码在上一阶段通过 16 项 Gradle 测试。真实 1280×720 帧流测试中，20 FPS 上限下实收约 18.4 FPS，延迟估计 P95 约 43 毫秒；相机、投影、深度、释放、慢接收端超时及重连通过。真实暂停后的帧流关闭和预览消失已验证；缩放和切换世界仍待检查。详见 [帧流协议](docs/frame-stream.md) 与 [MODLOG.md](MODLOG.md)。

Windows x64 接收端和独立 D3D11 合成实验已验证。真实 MC 原生合成测试中，10 秒接收/上传 180 帧，延迟估计 P95 约 112 毫秒。离线 ReShade 6.8.0 实测确认 CS2 的 D3D11 回调、纹理上传和 FX 编译；Dust2 上出现 MC 地形/天空诊断预览，MC 暂停后预览消失。一轮会话接收 6660 帧、上传 5367 帧、资源创建失败为零；这些总数包含启动阶段，不能作为 FPS 基准。诊断预览仍不代表世界深度融合。

2026-10-07 测试正常退出，临时 `dxgi.dll` 已移除，当时 CS2 `win64` 目录与安装前备份完全一致。仅加载 ReShade 的对照会话也有退出时的 D3D11 引用计数提示，资源泄漏仍不能排除。构建、安装和恢复步骤见 [原生实验说明](cs2/native/README.md)。

2026-10-08 重装后已通过 Steam 进入离线 Dust2，并拿到只读宿主深度候选统计。143 份报告中 137 份包含资源候选，结束时丢失事件 20 次、延迟事件和容量溢出均为零；仍不代表完整覆盖或已选定世界深度。相机和世界融合尚未接通。临时加载器与配置已恢复，游戏目录与本轮新备份一致。显式投影/单位换算及 48 种独立硬件 GPU 遮挡检查通过，见 [宿主深度实验说明](docs/host-depth-probe.md)。

深度视图归一化已补上单采样、多采样、数组和默认子资源范围，新增有界视图细分统计；其中 inventory 覆盖 16 类检查。修复本机 MSVC/Ninja 头文件依赖缓存导致的混合旧对象后，干净候选在 Steam 离线 Dust2 产生 107 份报告，真实 2DMS/单采样视图分类与计数一致；该阶段最后丢失事件 159 次。

后续已用有界无锁事件队列替换回调与报告的锁争用，新增并发/快照测试。真实离线 Dust2 产生 165 份报告（139 份有候选），最后排空 21,227,415 个事件，已定义的丢失/争用/异常/延迟/溢出均为零。快照构建 P95 为 59 微秒，单批后台排空最高 2388 微秒；这不是游戏帧率或渲染回调成本对照。正常退出后的 runtime、设备和积压均归零，临时文件已恢复、备份差异为空。世界深度身份、MSAA 读取和同帧相机仍待验证；退出引用计数警告仍未归因。

## 本地检查

无需第三方 Python 依赖。在仓库根目录执行：

```powershell
./game/preflight.ps1
./game/launch-offline.ps1  # 仅预览，不启动游戏
python -m unittest discover -s bridge -p 'test_*.py' -v
python -m bridge.bridge_server
```

诊断服务器只绑定 `127.0.0.1:37121`，接收 JSON Lines。每个连接先发送：

```json
{"v":1,"type":"hello","role":"test"}
```

收到 `ready` 后可发送相机消息；回应 `ack` 仅表示协议校验成功，**不表示帧已渲染**。

```json
{"v":1,"type":"camera","frame":1,"position":[0,64,0],"rotation":[0,0,0],"fov":70}
```

坐标采用 MC 坐标，角度是 `[yaw,pitch,roll]`，`fov` 是垂直视场角。Source 的水平 FOV 必须由未来适配器按实际画面宽高转换。相机帧号在连接内单调递增；读取时连续 5 秒未收到数据会断开，重连须重新握手。单条消息最多 64 KiB。该端点仅用于协议诊断，不负责转发、共享纹理或启动游戏。

## 离线开发

`./game/launch-offline.ps1 -Cs2Root '<当前安装目录>' -Launch` 才会通过已运行的 Steam 启动原版 CS2，参数为 `-insecure -console +sv_lan 1 +map de_dust2`。已有 CS2 进程或临时 ReShade 文件时会拒绝启动。直接运行游戏 exe 曾出现 Launcher Error #720；脚本已改用 Steam `-applaunch 730`，并核实实际子进程和离线参数。该脚本不安装 CounterCraft，启动参数也不构成网络防火墙；不要在开发会话中连接官方服务器。

ReShade 诊断会话使用 `scripts/launch-cs2-lab.ps1`，需要 PowerShell 7.4+ 和 `manage-cs2-loader.ps1 -SteamLaunch` 记录的安装状态；默认也只预览。启动走 Steam，游戏目录的临时 `ReShade.ini` 仅将 BasePath 指向候选目录，完整配置、日志、效果和缓存留在仓库的忽略目录中。按状态文件恢复加载器和临时配置后再正常使用 CS2。

现有 `1.20.1-OptiFine_I6` 实例和存档未修改。相机模组使用独立的 Fabric 1.20.1 开发实例，不支持 OptiFine 混用。构建、启动和本机相机测试见 [Minecraft 相机实验说明](minecraft/README.md)。

进入开发客户端的单人测试世界后，可执行 `python -m bridge.minecraft_host --verify`，同时验证实际渲染相机、投影、世界深度与释放。`--capture` 仅导出一帧；产物保存在忽略的 `minecraft/run/countercraft/captures/` 中，不提交游戏画面或深度数据。

连续帧流测试：

```powershell
python -m bridge.stream_host --verify --seconds 10 --fps 20
python -m bridge.stream_host --seconds 10 --fps 30 --consumer-ms 200
```

接收端校验会话、世界、尺寸、帧序号及 CRC；请求的 FPS 是上限。测试输出实际接收数、替换数、旧帧数及延迟估计，不能据此承诺 60 FPS。

详见 [MODDING_PLAN.md](MODDING_PLAN.md) 和 [MODLOG.md](MODLOG.md)。插件安装可审阅 `scripts/install-plugin.ps1` 后在普通 PowerShell 中运行。插件安装与游戏适配是两个独立步骤。

开发参考 [universal-modder](https://github.com/rehan-remade/universal-modder) 的 mashup-mods 工作流；Minecraft 适配使用 Fabric Loader、Fabric Loom 和 Yarn。代码由 Codex 协助开发，仓库只发布自有桥接源码。
