# 笔吧验机

**免费开源的 Windows 硬件验机工具**——二手/新机到手，一键采集硬件信息、逐项人工核对、确认通过。

## 核心亮点：多源信息交叉验证

每个硬件字段都由**多个独立来源交叉验证**，单一来源拿不到或读不准时自动降级到下一精度层级，而不是简单显示"未知"：

- **显存三级精度**：LibreHardwareMonitor 的 NVAPI/ADL 直读 → 注册表 `qwMemorySize` → WMI `AdapterRAM`（后者 >4GB 溢出，仅兜底）——规避了 WMI 在大显存显卡上读数溢出的经典问题
- **CPU 型号**：LHM CPUID 直读优先，WMI 兜底，两源互证
- **电池**：电池类驱动 IOCTL 优先，`root\WMI` 电池类补充，与 `powercfg /batteryreport` 同源
- **硬盘**：SMART 直读（NVMe/SATA 分别走 DiskInfoToolkit），无管理员权限时 LHM 自动降级

这套**"开源组件 + Windows 自带接口"的多源降级采集**，实现了通常被认为"必须闭源库"（HWiNFO 等）的采集深度——这是本项目与主流验机工具的本质区别。

## 功能一览

| 模块 | 能力 |
|---|---|
| **配置核对** | CPU/显卡/内存/硬盘/网卡/屏幕/电池 自动采集，与订单逐项核对；含 SMART 健康度、EDID 色域容积比估算、电池损耗与循环次数 |
| **键盘测试** | 104 键可视化，按住高亮、松开变绿，进度实时显示 |
| **屏幕坏点** | 全屏纯色切换（白/红/绿/蓝/黑），多屏逐台检测 |
| **影音会议** | 摄像头预览、麦克风电平+录制回放、扬声器试音 |
| **散热测试** | CPU/GPU/双烤三模式，四路温度功耗实时折线；烤机进程无窗口后台运行，多层兜底不留残留 |
| **硬盘跑分** | CrystalDiskMark 引擎源码集成，SEQ1M + RND4K 四项，成绩 JSON/CSV 落盘 |

## 技术关键

- **插件化架构**：每个验机模块是独立 class library 插件，安装向导可按模块勾选，程序内支持 zip 插件包导入/删除（导入即加载免重启，删除自动重启完成），致谢信息随插件走
- **采集深度**：NVMe/SATA SMART 直读（健康度/通电时间/介质错误）；EDID 色度坐标估算 sRGB/DCI-P3 容积比；电池数据与 `powercfg /batteryreport` 同源；显存直读规避 WMI 4GB 溢出
- **烤机工具自研**：CPU 烤机按 GIMPS 公开源码编译 prime95；GPU 烤机为 FurMark 的 C#/OpenTK 重实现；传感器监控经 PawnIO 内核驱动（官方签名安装器，页面一键静默安装）
- **工程化**：自包含发布（目标机无需装 .NET）、Inno Setup 中文安装向导、深/浅主题、全局异常落盘

## 技术栈

.NET 10 · WPF · LibreHardwareMonitorLib(MPL-2.0) · NAudio · CrystalDiskMark 引擎 · diskspd · OpenTK · prime95(GIMPS) · PawnIO · Inno Setup

## 运行与构建

- **使用**：到 [Releases](https://github.com/Bibapcs/BibaYanji/releases) 下载安装包（约 116MB），Windows 10/11 x64，需管理员权限（SMART 与传感器读取需要）
- **构建**：需 .NET SDK 10，`dotnet build 验机软件.slnx`；开发细节见 `AGENTS.md`

## 开源协议

本项目开源，各集成组件协议详见 `AGENTS.md` 致谢表（MPL-2.0 / MIT / Ms-PL / GPL-2.0带例外 / GIMPS freeware）。试音曲使用已经著作权人同意，见 `AUDIO-LICENSE.md`。

---

_开发手册见 `AGENTS.md`（工程细节）与 `AGENTS-AI.md`（AI 协作约定）。_
