# CounterCraft
目标：在 **CS2 本机离线模式**中接入真实 Minecraft Java 模拟，包括方块、合成、生物等系统。

**当前状态：前期原型，不能在 CS2 中玩 Minecraft。** 已实现独立的 Python 协议诊断端点、坐标转换、环境检查和离线启动预览；Minecraft 模组、CS2 适配器和画面合成尚未实现。这里没有可安装的游戏模组包。

## 本地检查

无需第三方 Python 依赖。在仓库根目录执行：

```powershell
./game/preflight.ps1
./game/launch-offline.ps1  # 仅预览，不启动游戏
python -m unittest bridge.test_bridge -v
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

现有 `1.20.1-OptiFine_I6` 实例和存档未修改。计划用独立的 Fabric 1.20.1 实例开发；尚未建立实例，也未验证 OptiFine 混用。

详见 [MODDING_PLAN.md](MODDING_PLAN.md) 和 [MODLOG.md](MODLOG.md)。插件安装可审阅 `scripts/install-plugin.ps1` 后在普通 PowerShell 中运行。插件安装与游戏适配是两个独立步骤。
