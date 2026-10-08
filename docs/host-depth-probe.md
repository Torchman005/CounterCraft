# 离线宿主深度观察与投影换算

2026-10-08 已通过 Steam 启动真实离线 Dust2，验证只读资源、绘制、清屏和
效果边界回调能产生候选报告。完整回调覆盖、开销和资源对应关系仍待验证。
上一阶段 `d1be2d9` 验证的是 MC 诊断预览，不是 CS2 世界深度融合。

## 重装后的实际观察

Steam build 25738536，D3D11、1920×1080、RTX 4060 Laptop。通过运行中的
Steam 使用 `-applaunch 730`，实际子进程保留 `-insecure`、实验室标记和
`+sv_lan 1 +map de_dust2`。直接启动 `cs2.exe` 曾出现 Launcher Error #720；
单独加 `-steam` 不能解决，启动脚本现已改为 Steam 路径。

143 份后台报告中，137 份包含有效资源候选。观察到屏幕尺寸、四倍采样的
D24S8（候选 1），其有绘制的区间记录 13～1297 条直接/间接绘制；还有屏幕
尺寸的单采样 D24S8、4352×5248 的 D16、960×540 和 480×270 的 D24S8。
这些编号只属于本次资源生命周期。较多绘制、尺寸匹配或清屏值 1 均不足以
确定世界深度、普通 Z 或 reversed-Z。

结束时 `missedEvents=20`、`deferredEvents=0`、`overflow=0`，因此
`knownLossFree=false`。未观察到延迟事件不等于完整覆盖。该次旧版本的基础
视图判断没有规范化 D3D11 的默认层数/mip 数和多采样视图，
`nonBaseViewDraws` 不能据此证明游戏使用了非基础子资源。
`cameraDepthVerified=false`、`autoSelected=false` 始终保留。

本次没有启动 MC，接收端的 `Socket deadline exceeded` 与宿主观察器独立。
无 guest 纹理上传，`resourceFailures=0`；这不是再次验证 MC 预览。没有进行
性能对照，也没有验证深度像素、相机矩阵或同帧融合。

测试正常退出，runtime 归零并注销 add-on。D3D11 引用计数提示再次出现，
原因仍未定位。临时加载器和 BasePath 配置均恢复，游戏目录与新备份完全
一致，原有 `steam_appid.txt` 和崩溃记录保留。

## 深度视图归一化（2026-10-08）

已核对 ReShade v6.8.0 的公开 D3D11 视图转换代码：DSV 只选择一个 mip，
多采样视图类型是 `texture_2d_multisample` / `texture_2d_multisample_array`。
非数组 DSV 的 `first_layer/layers` 没有原生对应字段，转换后可为 0；
多采样 DSV 没有 mip 选择字段。不能将这些忽略字段当成有效的零层范围。
API 中 `UINT32_MAX` 的活动范围按资源剩余 mip/层数解释；活动 mip/数组
范围为 0 不视为默认值。未知资源层数/mip 数、错误采样形状、不支持的视图
类型、错误格式、非零 mip/数组起始层、部分数组和多 mip 范围均不分类为
规范基础 DSV。非数组和多采样的忽略字段则按原生视图语义归一化。

新增每候选 `views`，一个完成区间最多保留 4 种不同描述。每种记录
`type/typeValue`、typed DSV `format`、原始和归一化的
`firstLevel/levels/firstLayer/layers`、`formatCompatible`、
`canonicalBaseDsv`、绑定/直接及间接绘制/清屏统计和最后清屏值。
格式须匹配资源的 typed/typeless 深度族；SRV 解释或未知格式不作为 DSV。
规范基础 DSV 定义为相符采样形状、mip 0 的单 mip、从层 0 覆盖资源所有层。
超过 4 种描述增加 `overflow`；总绘制统计仍计数，不虚构丢弃描述的细分统计。
这些槽在效果边界重置，并随资源生命周期/设备销毁失效。

`nonBaseViewDraws` 现在统计未满足上述完整条件的绘制，因此也包括未知或
不兼容描述，仍不能单独解释为“使用了非零子资源”。`canonicalBaseDsv=true`
也只是元数据分类，不证明是相机世界深度，不选择资源或读取 GPU 像素。
多采样 DSV 可以是规范基础视图，但 `samplingShapeOnly` 仍为 false；现有
独立单采样合成器未新增 MSAA 深度读取/归约能力。

7 组原生 CTest 通过；深度 inventory 从 9 扩展至 16 类合成检查，覆盖真实
D3D11 转换形状、默认范围、mip/切片、格式族及有界细分统计。这些测试不
依赖游戏。先在 `.local` 准备候选；检测到用户 CS2 在
`-steam -perfectworld` 对局中时未安装或发送输入。该会话自行退出后才
备份并进入已授权的离线测试。

第一候选暴露构建问题：本机 CMake 误解码中文 MSVC `/showIncludes`
前缀，Ninja 的 `addon.cpp` 头文件依赖计数为 0。修改 `HostProbe` 布局后，
该对象仍是旧版而 `host_probe.cpp` 已重编，造成 add-on 启动时访问异常。
已恢复临时文件，保留崩溃证据。构建脚本现在在本次进程中使用英语编译器
输出和 UTF-8 代码页；检测旧的本地化前缀时重新配置并干净重编，还核验
inventory 和两个 add-on 对象的头文件依赖。头文件时间戳变更的 dry-run
确实安排重编全部三个依赖对象。干净构建后 7 组 CTest 再次通过。

Steam 实际附加了 `-perfectworld` 地区后缀。启动脚本仅允许这一明确后缀，
仍须匹配完整 `-insecure` 实验参数；`+connect` 或其他多余参数仍拒绝。
未修改 Steam 地区/启动设置。新候选由 Steam PID 18224 创建 CS2 PID
47100，确认本机 Dust2 与机器人场景。输出尺寸为 1680×1050，D3D11、
RTX 4060 Laptop；未通过脚本修改图形设置。

干净候选产生 107 份报告，103 份含候选。主 4× D24S8 的 DSV 类型为
`texture_2d_multisample`，视图格式 45 匹配资源 typeless 格式 44；原始
范围 `{firstLevel:0, levels:1, firstLayer:0, layers:0}` 归一化为
`{firstLevel:0, levels:1, firstLayer:0, layers:1}`。
有绘制区间记录 10～2111 条直接/间接命令，`nonBaseViewDraws=0`。
屏幕尺寸单采样 D24S8、4352×5248 D16 及较小 D24S8 也表现为规范基础
DSV。所有已报告视图的细分绘制/清屏计数之和与候选总数相符。
数组、非零子资源和默认全范围目前只经过合成验证，本次游戏未观察到。

最后 `missedEvents/deferredEvents/overflow=159/0/0`，因此
`knownLossFree=false`。没有进行性能对照，不能将它与上一会话的 20 次
直接比较，也未判定争用发生在哪类事件。世界深度和相机身份仍未验证，
`autoSelected/cameraDepthVerified=false`。MC 未运行，接收超时及零上传为
预期；本轮不是 MC 预览或世界融合复验。

确切离线 PID 47100 正常关闭，ReShade 记录 runtime 销毁、add-on 注销并
结束退出。最后一份定时报告在销毁前，runtime 数为 1；未单独采到归零
报告。退出引用计数提示仍在（1353），原因待查。加载器和 bootstrap 已
恢复，游戏目录与本轮干净候选的新备份无新增/删除/修改；首次失败创建的
崩溃文件保留在此备份中，未发布。

## CS2 未安装时的准备

在仓库根目录构建，然后只在 `.local` 中准备候选包：

```powershell
./scripts/build-native.ps1 -VcVars '<Visual Studio 的 vcvars64.bat 绝对路径>'
./scripts/prepare-cs2-lab.ps1 -AllowMissingGame -Destination .local/cs2-host-probe-candidate
Get-Content .local/cs2-host-probe-candidate/install-plan.json
```

`-AllowMissingGame` 只解除准备阶段对 `cs2.exe` 存在性的要求，计划记录
`GameExecutablePresent`。不创建游戏目录、不安装加载器、不启动进程。
安装器仍要求真实游戏文件、备份和匹配的候选哈希。重装完成后重新核实位置
和 Steam build，生成最新计划并创建新备份；旧快照不代表重装后的当前状态。

完成检查并备份当前游戏目录后，使用
`manage-cs2-loader.ps1 -Mode Install -SteamLaunch` 安装同一候选目录。
`launch-cs2-lab.ps1 -StateFile <本次状态文件> -HostProbe` 默认只预览；明确
加 `-Launch` 才启动。`-HostProbe` 添加 `-countercraft-host-probe`，保留
`-insecure` 和实验室参数。通过已运行的 Steam 启动，而非直接运行游戏 exe。
临时 `ReShade.ini` 只包含官方 `[INSTALL] BasePath`，完整配置、日志、效果和
缓存留在候选目录。结束后按状态文件恢复加载器和这份配置。
完整安装步骤见 [原生实验说明](../cs2/native/README.md)。

## 只读观察器

使用 ReShade 6.8.0 的公开资源、绑定、绘制和清屏事件记录元数据。
不读取游戏内存、常量缓冲或深度像素，不创建宿主视图，不复制宿主纹理，
不调用 GPU 等待。设备/资源身份仅作为非拥有的数值键使用；报告输出独立
生命周期编号，不输出原生指针，也不持有 COM 引用。

固定容量为 128 个资源、64 个上下文绑定和 8 个设备，每设备最多报告
8 个候选，每候选每区间至多保留 4 种视图描述。销毁、重新创建或描述变化
会使旧绑定失效并产生新编号。
ReShade 效果阶段的绘制/清屏不计入宿主统计；效果结束后要求新的宿主绑定，
避免把内部状态恢复误认为游戏绑定。

渲染回调只做有限元数据操作，采用 `try_lock`。争用或异常记录
`missedEvents`，容量不足记录 `overflow`。D3D11 延迟命令录制/回放暂不
重建，记录 `deferredEvents`；间接绘制只记录命令数量，不猜测顶点数。
存在这些缺口时 `knownLossFree=false`。为真也仅表示未检测到这些缺口，
不证明覆盖了全部游戏绘制。编码、排序和写日志均在每秒一次的后台报告中。

`interval` 是设备经过效果边界的区间编号，不是 FPS；多个 runtime 可能
产生多个区间。报告只展示最近完成的区间，不导出完整逐绘制轨迹。

候选显示尺寸、格式族、采样数、层数、mip 数、shader-resource 使用标记、
绘制/清屏次数、最后清屏值和输出尺寸匹配情况。
`samplingShapeOnly` 仅表示单层、单 mip、单采样、已知格式且有 SRV 使用
标记的形状，仍须核实视图格式及生命周期；非基础子资源绘制另行计数。
按输出尺寸匹配、绘制次数排序，仅供调查，始终不自动选择或绑定深度。

屏幕大小的资源也可能是阴影、手部等通道；动态分辨率的世界深度可能与输出
不同尺寸。清屏 0/1 不能单独证明 reversed-Z。报告始终设置
`autoSelected=false`、`cameraDepthVerified=false`。

## 显式投影约定

`ProjectionDepth::from_d3d_column_major` 接受已确认是 D3D `[0,1]` 深度范围、
列主序的独立透视投影矩阵。它不搜索矩阵，也不能识别游戏来源。调用者必须
通过实际渲染证据确认布局、视口和同帧关系；OpenGL `[-1,1]` NDC 或
view-projection 矩阵不能直接冒充这个输入。

允许 x/y 镜头偏移，例如 TAA jitter；要求 z 不依赖 x/y，`clip_w=±eye_z`。
拒绝正交、斜切、镜像、非有限值和非规范布局。支持左右手眼空间、普通/反向
Z、有限/无限远平面，从深度端点推导近远平面。

对 `a=m[10]`、`b=m[14]`、`c=m[11]`、`d=m[15]`：

```text
clip_z = a * eye_z + b
clip_w = c * eye_z + d
window_depth = clip_z / clip_w
eye_z = (b - window_depth * d) / (window_depth * c - a)
```

右手眼空间的前方为负 Z，统一输出正眼空间距离。无限远端点保持无限远含义，
让较近 guest 几何可见。非法宿主深度保留宿主颜色；guest 天空或缺帧完全
保留宿主颜色。独立 GPU shader 使用同一显式约定。

距离仍使用宿主单位。独立 compositor 的 `host_units_per_guest` 默认 1，
先换算到 guest 单位再比较。协议目前约定 32 Source 单位对应 1 MC 方块，
仍须用实际 CS2 场景验证。正确单位不能替代相机位置、旋转、镜头和时序对齐。

## 验证范围与下一步

CPU 使用手算端点/中间距离验证 8 种透视约定、jitter、FP32、坏布局和非法
输入。事件重放验证销毁/重用、尺寸变化、效果排除、多设备、间接绘制、容量
和饱和计数。硬件 D3D11 用宿主平面距离 2/9、guest 平面距离 5 检查整个
读回画面的遮挡：8 种投影 × 2 个分辨率 × 单位比例 0.5/1/32，共 48 种
情况；也检查天空、非法深度、缺帧原色恢复和非法单位比例拒绝。
这些是合成纹理与真实 shader 运算，不是 CS2 捕获。

下一步：规范化并核对实际视图描述及事件缺口；比较实际通道候选；确认同帧相机、
投影、视口和深度约定；验证一个世界坐标立方体的遮挡和相机移动。
实际 resize、世界切换和资源释放也仍需验证。在此之前不启用全屏世界融合。
