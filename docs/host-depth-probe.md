# 离线宿主深度观察与投影换算

2026-10-08 阶段使用独立测试程序，未启动 CS2。新观察器已编译并通过合成
事件测试，在实际 CS2 中的回调覆盖、开销和资源对应关系仍待验证。
上一阶段 `d1be2d9` 验证的是 MC 诊断预览，不是 CS2 世界深度融合。

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

完成检查并批准临时离线测试后，安装同一候选目录。
`launch-cs2-lab.ps1 -StateFile <本次状态文件> -HostProbe` 默认只预览；明确
加 `-Launch` 才启动。`-HostProbe` 添加 `-countercraft-host-probe`，保留
`-insecure` 和实验室参数。结束后按状态文件恢复加载器。
完整安装步骤见 [原生实验说明](../cs2/native/README.md)。

## 只读观察器

使用 ReShade 6.8.0 的公开资源、绑定、绘制和清屏事件记录元数据。
不读取游戏内存、常量缓冲或深度像素，不创建宿主视图，不复制宿主纹理，
不调用 GPU 等待。设备/资源身份仅作为非拥有的数值键使用；报告输出独立
生命周期编号，不输出原生指针，也不持有 COM 引用。

固定容量为 128 个资源、64 个上下文绑定和 8 个设备，每设备最多报告
8 个候选。销毁、重新创建或描述变化会使旧绑定失效并产生新编号。
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

重装后的顺序：确认新 build 和回调覆盖；比较实际通道候选；确认同帧相机、
投影、视口和深度约定；验证一个世界坐标立方体的遮挡和相机移动。
实际 resize、世界切换和资源释放也仍需验证。在此之前不启用全屏世界融合。
