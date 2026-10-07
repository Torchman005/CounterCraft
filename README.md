# CounterCraft
目标：在 **CS2 本机离线模式**中接入真实 Minecraft Java 模拟，包括方块、合成、生物等系统。

**当前状态：前期原型，不能在 CS2 中玩 Minecraft。** 已实现 Python 协议诊断端点、坐标转换、环境检查和离线启动预览，以及 Fabric 1.20.1 相机接收、按需世界颜色/深度导出、GPU 异步读回和有界本机帧流。CS2 适配器和画面合成尚未实现。

插件 `universal-modder 0.2.0` 已核验安装启用。完整 Fabric 构建成功，已生成模组 jar。在独立单人测试世界中，相机请求、实际渲染参数、真实地形深度和释放后的恢复通过。新增帧流用三槽 PBO/fence 异步读回，通过本机 TCP 发送 RGBA、深度和同帧矩阵；慢消费只保留最新帧。这仍是 CPU 拷贝原型，不是 GPU 共享纹理。

当前验证：18 项 Python、16 项 Gradle Java 测试通过。真实 1280×720 帧流测试中，20 FPS 上限下实收约 18.4 FPS，延迟估计 P95 约 43 毫秒；相机、投影、深度、释放、慢接收端超时及重连通过。实际暂停/缩放仍待游戏内检查。详见 [帧流协议](docs/frame-stream.md) 与 [MODLOG.md](MODLOG.md)。

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

`./game/launch-offline.ps1 -Launch` 才会启动原版 CS2，参数为 `-insecure -console +sv_lan 1 +map de_dust2`。已有 CS2 进程时会拒绝启动。该脚本不安装 CounterCraft，启动参数也不构成网络防火墙；不要在开发会话中连接官方服务器。

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
