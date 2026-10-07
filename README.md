# CounterCraft
目标：在 **CS2 本机离线模式**中接入真实 Minecraft Java 模拟，包括方块、合成、生物等系统。

**当前状态：前期原型，不能在 CS2 中玩 Minecraft。** 已实现 Python 协议诊断端点、坐标转换、环境检查和离线启动预览，并新增 Fabric 1.20.1 相机接收模组。CS2 适配器和画面合成尚未实现。

插件 `universal-modder 0.2.0` 已核验安装启用。Python 的 8 项测试、Gradle 中 Java 网络与状态核心的 5 项测试通过；完整 Fabric 构建成功，已生成模组 jar。开发客户端在独立单人测试世界中完成了 100 帧相机请求及释放握手；实际渲染效果仍待帧缓冲验证。

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

现有 `1.20.1-OptiFine_I6` 实例和存档未修改。相机模组使用独立的 Fabric 1.20.1 开发实例，不支持 OptiFine 混用。构建、启动和本机相机测试见 [Minecraft 相机实验说明](minecraft/README.md)。

详见 [MODDING_PLAN.md](MODDING_PLAN.md) 和 [MODLOG.md](MODLOG.md)。插件安装可审阅 `scripts/install-plugin.ps1` 后在普通 PowerShell 中运行。插件安装与游戏适配是两个独立步骤。
