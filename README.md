# CounterCraft

目标：在 **CS2 本机离线模式**中接入真实 Minecraft Java 模拟，包括建造、挖矿、合成、生物和红石。

**当前是可操作的离线桥接原型，尚未完成两个世界的融合。** Minecraft 1.20.1 Fabric 在独立进程运行，CS2 的 ReShade 插件显示完整 MC 画面、手部、HUD 和库存，并传递键鼠输入。移动由 MC 原版碰撞和重力处理；这不是 MC 方块已经进入 Dust2，也不支持官方匹配。

已实测：CS2 中的移动、鼠标转向、创造库存取物、左键挖除、右键和 R 键放置；MC 动作接口的放置、挖掘、2×2 原版合成；持续拉弓和释放。新加入滚轮、丢弃、副手交换、库存拖动与修饰键，未逐项完成 CS2 场景验收。暂停、断流和换世界会释放输入；玩法接收端重新握手，不复用旧 epoch。实际暂停/恢复及 MC 窗口 960×540 → 1280×720 调整已通过。

2026-10-09 补充：CS2 聊天文字与回车已进入 MC 原版日志；普通字符、编辑键经独立有界队列转发。原版客体测试已确认红石灯通断、猪实体受击、生存伤害和死亡界面复活。冲刺遵循 MC 原版规则。输入法组合、剪贴板、按键重复和完整跨游戏世界融合仍未实现。

现有 PCL/OptiFine 实例和存档未修改。使用隔离的 Fabric 1.20.1 开发实例，不能与 OptiFine 混用。帧传输是有界 CPU 拷贝原型，20 FPS 上限，不承诺 60 FPS。CS2 世界深度、相机读回和 GPU 遮挡实验仍是单独诊断能力，尚未接入玩法融合。退出时的 D3D11 引用计数提示仍未归因。

## 使用和检查

依赖 Windows x64、PowerShell 7.4+、Python 3、JDK 17+、Gradle 8.14、MSVC x64，及用户自己的 Minecraft/CS2。构建和独立客户端启动见 [Minecraft 说明](minecraft/README.md)；本机 ReShade 准备、Steam 启动和恢复见 [原生说明](cs2/native/README.md)。

进入独立单人测试世界并关闭菜单后：

```powershell
python -m bridge.health
python -m bridge.verify_input  # 会移动玩家并开关库存，仅在测试存档执行
python -m unittest discover -s bridge -p 'test_*.py' -v
```

推荐使用配置启动器（先构建，创建独立世界并完成安装前备份）：

```powershell
New-Item -ItemType Directory -Force .local
Copy-Item game/config.example.json .local/countercraft-machine.json
# 编辑该 JSON，填入本机路径；不要提交机器配置。
./scripts/fetch-reshade-runtime.ps1
./game/start.ps1           # 默认预览
./game/start.ps1 -Launch   # 启动独立 MC 和离线 CS2，监督退出恢复
```

启动器只复用已核验的仓库开发客户端。它自动关闭自己启动的 MC，保留预先运行的客户端。每次启动生成新的恢复记录；MC 世界需预先存在，不自动创建或导入存档。

若 MC 已在运行，可用单独的受监督入口：

```powershell
./game/play.ps1 -Cs2Root '<CS2 目录>'  # 默认只预览
./game/play.ps1 -Mode Play -Cs2Root '<CS2 目录>' -BackupSnapshot '<安装前备份 ZIP>' -SessionDirectory '.local/my-session'
```

MC 客户端须先进入独立单人世界。启动必须走已运行的 Steam `-applaunch 730`，使用 `-insecure`；不能直接运行 cs2.exe。F8 切换 MC 画面与输入。关闭 CS2 后，监督脚本验证原状态并恢复临时加载器。若终端被强制关闭，关闭 CS2 后用相同 `SessionDirectory` 执行 `-Mode Recover`，再正常使用 CS2。每轮使用新的会话目录，保留恢复记录。

单独的 `scripts/launch-cs2-lab.ps1 -Gameplay -Launch` 仍保留供诊断使用；它需要已记录的加载器状态，退出后手动恢复。

- [连续输入和画面协议](docs/gameplay-input.md)
- [初版包、安装与恢复](docs/alpha-install.md)
- [动作和合成协议](docs/gameplay-actions.md)
- [帧流协议](docs/frame-stream.md)
- [相机桥接实验](docs/camera-relay.md)
- [实验世界融合](docs/world-fusion.md)
- [宿主深度实验](docs/host-depth-probe.md)
- [计划](MODDING_PLAN.md)与[验证记录](MODLOG.md)

插件 `universal-modder 0.2.0` 已安装启用。参考 [universal-modder](https://github.com/rehan-remade/universal-modder) 的 mashup-mods 工作流；Minecraft 使用 Fabric Loader、Loom 和 Yarn，宿主使用 ReShade。代码由 Codex 协助开发；只发布自有桥接代码，不提交游戏资产、私人捕获或存档。
