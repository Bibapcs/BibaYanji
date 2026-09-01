# 笔吧验机

二手/新机到手后的硬件配置核对工具：一键采集 CPU、显卡、内存、硬盘、网卡、屏幕（含色域估算）信息，
人工与订单配置逐项核对，确认无误后标记通过（左侧导航圆点变绿）。

全部硬件采集基于**开源组件与 Windows 自带接口**，不含任何闭源商业软件。

## 功能

- **配置核对**：进入页面即自动后台采集（不卡界面，有分阶段进度），分组卡片展示，核对后点「确认无误，通过」：
  - **CPU**：型号、核心/线程数
  - **显卡**：型号、显存（NVAPI/ADL 或注册表 qwMemorySize 直读，避免 WMI 超过 4 GB 溢出的问题）、驱动版本
  - **内存**：总容量、实际运行频率（BIOS 当前设定值）；每根详情（插槽/标称与实际频率/厂商/料号）折叠在「详细信息」中
  - **硬盘**：容量、盘符（C:/D: 等，一块盘多个盘符会并列显示）、型号、健康度、通电时间（SMART 直读，需管理员权限；读不到时显示「未知」）
  - **网卡**：型号（全部物理网卡，与设备管理器显示一致）
  - **屏幕**：分辨率、刷新率、尺寸（对角线英寸）、面板名称、色域 —— 由 EDID 色度坐标估算的
    sRGB / DCI-P3 **容积比**（面板顶点可超出标准色域，比值可能 >100%，是容积比而非覆盖率）
- **键盘测试**：全尺寸 ANSI 104 键可视化键盘，按物理键实时反馈（按住高亮、松开变绿），
  显示「已测试 X / 104」进度，可重置；笔记本没有的键（如小键盘）保持灰色即可
- **屏幕坏点**：选显示器后全屏纯色检测（白→红→绿→蓝→黑），按任意键或单击鼠标切换、Esc 退出，
  多屏逐台可测，检查坏点/亮点/暗点
- **影音会议**：摄像头（设备可选 + 实时预览，离开页面自动关闭）、麦克风（电平条实时跳动 + 录制/回放）、
  扬声器（设备可选 + 播放试音曲），三项确认后一键通过
- **散热测试**：CPU 单烤（prime95，本项目按 GIMPS 源码编译）/ GPU 单烤（FurMark 毛球，C# / OpenTK 重实现）
  / 双烤三模式；FurMark 分辨率/全屏/抗锯齿与 prime95 线程数/FFT 模式可调，已烤时长实时显示；
  **数据监控**四路实时折线（CPU 温度/功耗、GPU 温度/功耗，LibreHardwareMonitor 2 秒轮询；
  CPU 两路需要 PawnIO 内核驱动，未安装时页面可一键静默安装官方签名安装器，装后立即可用无需重启）；「显示界面」可关（烤机进程完全无窗口后台跑，负载不变）；
  运行日志框记录起停/异常退出事件；停止/切页/退出多层兜底，不会留下后台烤机进程
- **硬盘跑分**：CrystalDiskMark 源码集成（引擎原封编译为 CdmHost 控制台宿主 + 定制 diskspd 子进程，全程无窗口），
  对目标盘跑 SEQ1M Q8T1 顺序读/写 + RND4K Q1T1 随机读/写四项（1 GiB 随机数据 × 5 轮）；
  成绩 JSON 与记录 CSV 保存在 `%APPDATA%\YanJi\disk_result`（页面上可一键打开成绩目录）
- 深/浅主题切换（默认跟随系统），左侧导航显示各验机项完成状态（绿点 + ✓）
- 每个模块点「确认通过」后自动跳转到下一个未完成模块，全部完成时导航区给出提示
- 配置核对「重新检测」、键盘测试「重置」可随时复查（会清除通过状态重新核对）
- **插件化**：以上每个验机模块都是独立插件（程序目录 `plugins\` 下）；安装向导可按模块勾选安装，
  程序顶栏「插件管理」支持导入 zip 插件包（0.0.2 起可多选批量导入）/ 删除插件（导入即加载，删除/覆盖自动重启完成），
  并逐模块展示其**组件致谢**（致谢随插件走，删除模块其致谢也随之消失）。插件开发见根目录 `AGENTS.md`

## 运行环境

- Windows 10 2004 (19041) / 11 x64（摄像头用 WinRT MediaCapture；音频用 WASAPI；硬件信息依赖 WMI / 注册表 / Win32 显示 API）
- **需要管理员权限运行**（启动弹一次 UAC）：NVMe/SATA 硬盘 SMART（健康度/通电时间）普通权限读不到；
  个别传感器级数据（如内存 SPD 频率）还需 LibreHardwareMonitor 的 PawnIO 驱动，缺驱动时自动降级用 WMI 补充显示

## 构建

需要 .NET SDK 10（目标框架 net10.0-windows10.0.19041.0）：

```
dotnet build 验机软件.slnx
dotnet run --project 验机软件
```

> 说明：LibreHardwareMonitorLib 以其上游要求固定按 x64 平台构建（解决方案已配置平台映射，
> 请勿改回 AnyCPU，否则其 CsWin32 源生成器会跳过部分 API 导致编译失败）。

## 安装

**直接下载**：到 [Releases](https://github.com/Bibapcs/BibaYanji/releases) 下载最新安装包
（当前 v0.0.2：`BibaYanjiSetup-v0.0.2.exe`，约 116 MB，Inno Setup 中文向导）：

- 安装到 `C:\Program Files\BibaInspection`；「自定义安装」可**按模块勾选**要装的验机插件
  （不勾「散热测试」可省约 130 MB 烤机工具负载）
- 附加任务（默认都勾）：「创建桌面快捷方式」「创建开始菜单项」
  「安装 PawnIO 内核驱动」（0.0.2 起，安装末尾静默完成、无需额外点击；
  未装散热组件或系统已装 PawnIO 时自动跳过）
- 装完从桌面/开始菜单启动（**需管理员**：程序 manifest 会弹一次 UAC，硬盘 SMART 与部分传感器需要）
- 卸载走系统「应用与功能」或开始菜单卸载项：删安装目录与快捷方式
  （0.0.2 起连安装后导入的插件目录一并删除）；卸载时弹出「删除用户数据（软件设置、跑分结果等）」
  勾选框（默认勾选，可取消保留 %APPDATA%\YanJi）
- 目标机**无需安装 .NET 运行时**（self-contained win-x64）

**自行构建安装包**（先出 publish 再打 Inno 包，顺序不可换）：

```
dotnet publish FurMark/FurMark.csproj -p:PublishProfile=FolderProfile
dotnet publish 验机软件/验机软件.csproj -p:PublishProfile=FolderProfile
"%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" installer\setup.iss   # 产物 installer\笔吧验机Setup-v0.0.2.exe
```

安装包内容：`验机软件.exe` + 全套自包含运行时 + `YanJi.PluginSdk.dll`（插件契约）+
`plugins\`（六个验机模块插件，试音曲 mp3 在 `plugins\av\`）+ `Tools\prime95\`（CPU 烤机）+
`Tools\furmark\`（GPU 烤机）+ `Tools\pawnio\PawnIO_setup.exe`（PawnIO 驱动官方签名安装器——
0.0.2 起安装向导默认安装该驱动；不装也能跑，只是散热测试页 CPU 温度/功耗两路不可用，
页面也有一键安装按钮，装完无需重启）。

## 开源组件与协议（致谢）

程序内致谢随各模块插件展示（顶栏「插件管理」窗口，逐模块列出）；下表为汇总：

| 组件 | 用途 | 协议 |
| --- | --- | --- |
| [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor) | 硬件信息采集库（源码项目引用 `集成开源项目代码/LibreHardwareMonitor-master/LibreHardwareMonitorLib`） | MPL-2.0 |
| [CrystalDiskInfo](https://crystalmark.info/) | 硬盘信息采集的实现参考（C++/MFC 无法直接引用，其思路经由 LibreHardwareMonitorLib 的 DiskInfoToolkit Storage 支持落地） | MIT |
| [NAudio](https://github.com/naudio/NAudio) 2.2.1 | 麦克风/扬声器（WASAPI 设备枚举、电平监听、录制、播放） | Ms-PL |
| [Prime95](https://www.mersenne.org/download/)（GIMPS，George Woltman） | CPU 烤机（ torture test）。由本项目用 VS18/MSBuild 按其公开源码编译（`prime95-build/` 叠加工程，见 AGENTS.md），附带的 libgmp-gw1.dll / libcurl-x64.dll / libhwloc-15.dll 与其官方构建同源 | GIMPS freeware（随附 license.txt） |
| [FurMark（Python 版）](https://github.com/StanislavPetrovV/FurMark)（StanislavPetrovV） | GPU 烤机的渲染原型：本项目的 GPU 烤机为其 C# / OpenTK 重实现（`FurMark/` 工程），原 GLSL 着色器与贴图随附其许可文件（Assets/LICENSE.FurMark.txt） | MIT |
| [OpenTK](https://opentk.net/) 4.x | FurMark 重实现的 OpenGL 窗口（GLFW）与绑定 | MIT |
| [StbImageSharp](https://github.com/StbSharp/StbImageSharp) | FurMark 重实现的贴图解码（stb_image 的 C# 移植） | 公有领域 |
| [PawnIO](https://github.com/namazso/PawnIO)（namazso） | CPU/主板传感器的 Ring0 内核驱动（LibreHardwareMonitor 经设备 IOCTL 与其通信）。**分发的是官方签名安装器**（pawnio.eu → PawnIO.Setup releases，原样未修改，随附于 Tools/pawnio/，由用户点击后才安装） | GPL-2.0（带例外：独立模块经设备 IOCTL 接口通信不构成衍生作品） |
| [CrystalDiskMark](https://github.com/hiyohiyo/CrystalDiskMark) 9.0.3（hiyohiyo / Crystal Dew World） | 硬盘跑分引擎：源码原封编译为 CdmHost 控制台宿主（`Plugins/DiskMark/CdmHost/`，全程无窗口），引擎 DiskBench 直接驱动测速 | MIT |
| [diskspd](https://github.com/microsoft/diskspd) 2.0.20a（Microsoft，hiyohiyo 定制 fork） | 硬盘测速子进程（由 CDM 引擎拉起，吞吐经退出码回报；随插件分发于 `Plugins/DiskMark/CdmHost/CdmResource/diskspd/`） | MIT |

开发/打包工具致谢：[Inno Setup](https://jrsoftware.org/)（安装向导制作，仅打包用不进运行时，免费可商用）。

另使用 Windows 自带接口：WMI（System.Management）、注册表 EDID、Win32 显示 API、WinRT Windows.Media.Capture（摄像头预览）。

试音曲「不死のバイオレット」Copyright© 幻月遠征隊，使用已经著作权人同意，详见AUDIO-LICENSE.md。

感谢上述开源项目的作者与贡献者。
