# AGENTS-AI.md — 面向 AI 助手的协作约定

> 本文件面向参与本项目的 AI 助手（agent）。人类开发者请先读根目录 `AGENTS.md`（项目工程手册），本文件是其补充，不替代。

## 项目是什么

笔吧验机：Windows 桌面验机工具（WPF），二手/新机到手后一键采集硬件信息、逐项人工核对、标记通过。主程序是插件外壳，每个验机功能模块是独立插件（class library）。**全部硬件采集基于开源组件与 Windows 自带接口，禁止集成闭源商业软件。**

上游已发布版本：v0.0.4；本轮本地验收包：v0.0.5-preview.1，未公开发布。目标用户：二手交易买家、新机验机用户。

## 当前开发主线

2026-10-15 用户确认：暂停教学与评分，改为简单 C# 整合 LHM 已有能力。
配置核对直接复用公开 SMBios 对象，A～E（BIOS、整机、主板、CPU、内存补充）全选；
保留现有报告模型、WPF、插件、主题、安装发布和有效 Windows 补充，来源与失败明示。
配置/散热的 Computer 生命周期由 `Plugins/LhmComputerAccess.cs` 共享源码互斥管理，
不扩充 SDK、不新增采集框架；原生调用不能强制取消，切页后等待返回再释放。
用户已授权助手将既有未提交成果和本阶段确认的改动逐项核对、提交、推送 fork dev 并更新 PR；
不得混入未知文件，不含合并、部署、强推、删除或历史改写。
旧 Java/Caller/契约保留作历史，不沿用为新主线，不继续建设独立证据引擎。

## 本地预览封装（2026-10-15）

- 用户确认第1项真实监控与第3项本地安装/打包验收，完成后提交推送并更新 PR；第2项电池异常由用户处理。满载烤机、驱动卸载重装、公开发布、合并均不在本轮范围。
- PawnIO已运行时先查状态，避免每次进监控页重复请求启动；查询失败则沿用不可用降级，未借助替代命令绕过服务启动限制。
- 预览版独立 AppId/安装目录；负载 `preview-mode` 使设置与插件待处理队列使用 `YanJiPreview`。硬盘成绩仍沿用共享目录，卸载预览始终保留用户数据。
- 封装入口与真实验收工程在 `installer/build-release.ps1`、`VerifyRelease.csproj`；具体运行限制和证据见 PR。产物不进 Git。验收入口禁用 `AllowAutomaticDriverStart`，可用 `--soak-seconds 10800` 持续低负载监控并记录原始样本/进程资源；短跑不算三小时通过。

## 历史两种语言的开发分工（已停止推进）

本项目涉及两种语言，分工边界必须分清：

| 语言 | 职责 | 在本项目中的位置 | 谁来写 |
|---|---|---|---|
| **C#** | 宿主外壳、6 个插件、SDK、烤机工具及 Java 调用/结果接收 | `验机软件/`、`Plugins/*/`、`PluginSdk/`、`FurMark/`、`hardware-evidence/src/main/csharp/` | AI 助手可写，人类质检 |
| **Java** | hardware-evidence 实际引擎；当前参数演示与结构化人工结果，后续采集/分析按新契约接入 | `hardware-evidence/src/main/java/`，Java 21 / Maven | 按用户明确需求进行实际开发，不恢复教学或要求用户手写 |

**当前约定（2026-10-14）**：用户已选择 F 盘真实工程并停止教学，随后选择推进结构化结果接口。两侧只依赖外部契约，内部类不构成接口。原 `demo-args-v1` 不变；新接口见 `hardware-evidence/contracts/result-demo-v1.md`，构建和测试见该目录 README。旧交接的教学规则、E 盘源码位置及“Java 不在仓库”条款不再适用。

## 全套技术栈

### 平台与框架

- **.NET 10**（目标框架 `net10.0-windows10.0.19041.0`），SDK 10.0.400
- **WPF**（Windows Presentation Foundation）桌面 UI，无 MVVM 框架，UserControl + code-behind
- **x64 平台**：`LibreHardwareMonitorLib` 的 CsWin32 源生成器要求固定 x64，不可改回 AnyCPU

### 硬件采集（核心）

| 组件 | 用途 | 协议 |
|---|---|---|
| LibreHardwareMonitorLib（源码引用） | CPU/GPU/内存/硬盘/传感器采集 | MPL-2.0 |
| System.Management (WMI) | 网卡、电池、部分硬件信息 | — |
| 注册表 EDID + Win32 显示 API | 屏幕信息、色域估算 | — |
| PawnIO 内核驱动（官方签名安装器） | CPU/主板传感器 Ring0 访问 | GPL-2.0（带例外） |

### 功能组件

| 组件 | 用途 | 协议 |
|---|---|---|
| NAudio 2.2.1 | 麦克风/扬声器（WASAPI） | Ms-PL |
| CrystalDiskMark 9.0.3（源码编译为 CdmHost） | 硬盘跑分引擎 | MIT |
| diskspd 2.0.20a（微软，CDM 定制 fork） | 硬盘测速子进程 | MIT |
| prime95（GIMPS 源码编译） | CPU 烤机 | GIMPS freeware |
| FurMark（C#/OpenTK 重实现） | GPU 烤机 | MIT（原型） |
| OpenTK 4.x | FurMark 的 OpenGL 绑定 | MIT |
| StbImageSharp | FurMark 贴图解码 | 公有领域 |

### 打包

- **Inno Setup 6**：安装向导（`installer/setup.iss`），产物约 116MB
- **dotnet publish**：self-contained win-x64，目标机无需装 .NET

## 开发现状

### 已可用（已发布 v0.0.4）

6 个插件全部功能完整：配置核对（含电池）、键盘测试、屏幕坏点、影音会议、散热测试（烤机+监控）、硬盘跑分。插件化架构完整（zip 导入/删除、免重启加载、致谢随插件走）。

### 开发环境现状（当前 fork 工作区）

- **工作目录**：`F:/BibaYanji-dev`（clone 自 `F:/BibaYanji`，origin=beifeng08 fork，upstream=Bibapcs 原版）
- **分支**：`dev` 上开发，`main` 保持与 `origin/main` 一致
- **已实测环境（2026-10-14）**：.NET SDK 10.0.401、JDK 21.0.12.1、Maven 3.9.16；Caller 与 Java 已构建和真实联调。先核实际环境，不再宣称 SDK 缺失
- **缺失 2**：上游 `AGENTS.md` 引用的 `项目交接文档.md` **不存在于本仓库**（全历史未提交）。涉及采集逻辑（LHM/EDID/SMART/色域）的改动**必须先读源码自行验证**，不能引用该文档中的结论
- **已发布成品**：`F:/BibaInspection/` 有 v0.0.4 自包含发布版（464MB），可直接运行查看产品现状，但无 SDK 组件不能用于编译

### 网络现状

2026-10-14 已成功 fetch/push 并创建 PR #2（fork dev → main）；Git 连接有过超时，重试后恢复。后续以实际命令结果为准，不将历史网络故障当当前阻断。

## 开发全流程与安排

### 单次改动流程（AI 助手必须遵守）

1. **说明背景**：改哪个文件、为什么、涉及什么机制、有什么风险
2. **按人类已明确的需求与授权范围实施**；业务范围或外部契约未明确时先商讨，不反复要求确认已经授权的开发
3. **实现**：最小改动，风格对齐周边文件（注释密度、命名、UserControl+code-behind）
4. **自检**：`git status` 看改动清单，`git diff` 看具体差异
5. **提交**：`git add <明确路径>`（不用 `git add .`），commit message 格式 `<类型>：<简述>`
6. **push**：**需人类单独授权**，不擅自执行

### 硬性约束（违反即返工）

- 禁闭源商业软件（HWiNFO 等）；采集只用 LHM 源码 + Windows 自带接口
- 插件引用 `YanJi.PluginSdk` 必须 `Private="false"`
- `LibreHardwareMonitorLib` 引用必须带 `SetPlatform="Platform=x64"`
- 宿主 csproj 里 `System.Management` 的 PackageReference **勿删**（插件运行时靠宿主解析真身）
- 试音曲署名（AvConference 页面）是用户给定原文，**不得改动**
- 新增内置插件需同步 3 处：slnx + 宿主 BuildPlugins/CopyPlugins 两个 target + `installer/setup.iss` 的 [Components]/[Files]
- 临时探针工程（`.*-probe/`）用完即删；LHM 上游源码不批量转码/改动

### UI 约定

- 只用语义画刷令牌（`WindowBgBrush`/`AccentBrush` 等），全部 `DynamicResource`，**不写字面颜色**；深/浅两个字典键集必须一致
- 「测试通过」语义色用 `ForestGreen`
- 资源还原用 `SetResourceReference` 恢复动态绑定（直接赋画刷会断主题切换）
- 日志区：只读 TextBox（Consolas 11）外套 `CornerRadius=6` 的 Border

### 证据标注（AI 助手必须遵守）

每个结论必须标注证据等级：
- **已实跑**：真的执行过，有输出
- **静态审阅**：读过代码但没运行
- **未验证**：推断或猜测

**未编译的改动必须显式标注「未编译验证」**。构建成功与实际运行是两项不同证据，不能互相替代。

## 项目开发方向与目标

### 产品定位

面向二手交易与新机验机场景的**免费开源**硬件核对工具。差异化：**不依赖任何闭源商业软件**（对照：图吧工具箱集成 HWiNFO 等），全部基于开源组件 + Windows 自带接口实现同等采集能力。

### 已确认的方向（来自上游 README/AGENTS）

- 插件化架构持续完善：第三方插件生态、更灵活的模块组合
- 采集能力深化：更多硬件细节（当前已有 CPU/显卡/内存/硬盘/网卡/屏幕/电池）
- 安装体验优化：模块化安装、PawnIO 驱动静默集成

### 本 fork 的工作目标（人类开发者设定，待补充）

> 此处由人类开发者填写本 fork 的具体目标。AI 助手开工前应确认当前迭代要解决什么问题。

**当前开发顺序**：
- 用户已全选公开 SMBIOS 可提供的 A～E 字段及生命周期修补。
- 配置采集、字段映射、切页取消、监控停止/重启和插件关闭收尾一并验证。
- 构建、实机、单项失败、切页和深浅主题分别标证据，不以编译成功代替运行。
- `项目交接文档.md` 缺失，继续以本地源码核对，不引用不存在的实测结论。
- 历史 .NET/Java/人工结果记录保留，不构成本主线验收。

---

_本文件随项目演进更新。机制变化（目录、契约、构建链路、协作约定）时同步修改。_
