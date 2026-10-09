# CounterCraft 0.1.0-alpha.3

这是源代码辅助启动的 Windows 离线桥接初版包。CS2 显示完整 Minecraft
客户端画面并转发键鼠；真实 MC 负责世界、方块、生物、红石、合成和碰撞。
当前没有将 MC 方块放进 Dust2，也没有接通 CS2 人物和 MC 碰撞。
完整移植目标仍需后续世界融合工作；不能将这个包称为完成全部移植。
alpha.3 新增[实验世界融合管线](world-fusion.md)，但 CS2 实机尚未显示融合方块；
默认入口仍使用已有完整 MC 画面桥接。实验模式需要本机私人校准。

## 依赖与包内容

需要用户自己的 Steam CS2、Minecraft Java 1.20.1、Windows x64、PowerShell
7.4+、Python 3、JDK 21、Gradle 8.14。开发启动器会通过 Fabric Loom 下载/构建
独立客户端；首次准备需要联网。包内已有自有 native addon 和可玩的 mod jar，
不需要 MSVC 重新编译 addon。源码开发者构建 native 才需要 MSVC x64。

Fabric Loader 0.19.5、Loom 1.10.5、Yarn 1.20.1+build.10 已固定。ReShade
6.8.0 全 addon 运行库必须单独下载并通过固定 SHA256 检查。
包不包含 Minecraft/CS2 游戏资产、ReShade 运行库、私人捕获、存档或依赖缓存。
PCL/OptiFine 实例保持独立；此初版不支持 OptiFine/OptiFabric。

## 准备和启动

解压后，在包根目录用 PowerShell 7 执行：

```powershell
python scripts/package-alpha.py verify .
./scripts/fetch-reshade-runtime.ps1
./scripts/build-minecraft.ps1 -Gradle '<gradle.bat>' -JavaHome '<JDK 21 目录>'
./scripts/build-minecraft.ps1 -Gradle '<gradle.bat>' -JavaHome '<JDK 21 目录>' -Task runClient -Background
```

第一次在这个独立 MC 中创建单人测试世界（如 `CounterCraft Lab`），然后正常退出。
不要复制原有重要存档。若显卡/声卡环境出现持续 OpenAL 重置错误，可在测试命令中
加 `-NullAudio`；该选项关闭此客户端声音，默认仍启用原版声音。

CS2 关闭且 Steam 已运行时，用 universal-modder 建立安装前备份：

```powershell
um backup create '<CS2 目录>/game/bin/win64' --name countercraft-before-loader
Copy-Item game/config.example.json .local/countercraft-machine.json
```

在 JSON 中填入 CS2、Gradle、JavaHome、已完成的备份 ZIP 和独立世界名称。
`NullAudio` 默认 false。可选 `Loader`、`NativeBuild`、`SteamExecutable`
可指定外部依赖；打包版本默认使用 `native/CounterCraftProbe.addon64`。
所有机器路径、存档和日志只保存在本机，勿加入 Git。

```powershell
./game/start.ps1          # 预览，不启动或改游戏
./game/start.ps1 -Launch  # 自动启动已有独立世界，再经 Steam -insecure 启动 CS2
```

MC 由启动器开启时，在 CS2 退出后自动关闭；复用已有仓库 MC 时保留该进程。
仅符合 Java 路径、仓库路径、Fabric 开发入口和启用标志的进程可被复用；端口
37122 被其他进程占用会拒绝启动。

## 操作与恢复

WASD 移动；鼠标转向；左键挖矿/攻击，右键使用/放置，R 是世界视图中的替代键；
E 打开库存，Esc 关闭库存；T 聊天，/ 命令；数字键/滚轮选物；Space/Shift/Ctrl 跳跃/潜行/冲刺；
Q 丢弃，Ctrl+Q 丢整组，F 交换副手，中键选取方块。F8 返回 CS2 画面并释放 MC
输入，再按恢复。库存支持点击、右键分物、拖动和 Shift/Ctrl 修饰。

关闭 CS2 会恢复临时加载器；恢复记录在 `.local/sessions/<会话>/`。
若脚本/终端被强制关闭，先关闭 CS2，然后用**原来的会话目录**恢复：

```powershell
./game/play.ps1 -Mode Recover -Cs2Root '<CS2 目录>' -SessionDirectory '<原会话目录>'
```

恢复核验文件路径和哈希，修改过的文件不会被擅自删除。`session.json` 的 Mode
应为 Restored。普通使用 CS2 前确认 `game/bin/win64/dxgi.dll` 和临时
`ReShade.ini` 已恢复。卸载自有文件只需在成功恢复并退出独立 MC 后移除解压目录；
如需保留测试世界，先另行保存 `minecraft/run/saves`。

## 验证和已知限制

```powershell
python -m unittest discover -s bridge -p 'test_*.py'
python scripts/test-package.py
python scripts/test-game-start.py
# MC 运行、CS2 关闭时，在有作弊权限的可丢弃创造测试世界执行：
python -m bridge.verify_vanilla --allow-lab-commands
```

实际验收和未验收功能见根目录 README、MODLOG 和 gameplay-input 文档。
传输上限 20 FPS，CPU 拷贝且无 GPU 共享；短促按键可能被采样遗漏。
窗口保持 MC 原比例。支持普通文字和基本编辑键，尚不支持输入法组合、剪贴板和长按重复；MC 暂停需要在 MC 窗口本地
恢复。其他叠加层可能截取输入。退出时 D3D11 引用计数提示仍待归因。

原版验收命令会暂时创建两个方块和一只唯一标记的猪，测试红石、攻击和生存伤害；
最后恢复创造模式、清理其方块/实体并复原视角。角色会受伤，可能出现掉落物，
不能用于重要存档，也不声称回滚存档中的全部变化。结果只写入本地 `.local`。

## 开发打包

构建两端后，使用全新目录：

```powershell
python scripts/package-alpha.py build --destination .local/packages/CounterCraft-alpha
python scripts/package-alpha.py verify .local/packages/CounterCraft-alpha
```

清单含每个文件的 SHA256、大小、源码提交和 dirty 标记；ZIP 另有校验文件。
它是完整性校验，不是数字签名或信任来源。发布前对这个白名单目录运行
`um publish check`，不要扫描包含私人运行缓存的整个工作区。
自有代码 MIT，依赖声明见 THIRD_PARTY_NOTICES。代码由 Codex 协助开发。
