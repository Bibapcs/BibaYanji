# AGENTS.md — 笔吧验机

## 项目是什么

Windows 桌面验机工具（WPF，net10.0-windows10.0.19041.0，.NET SDK 10.0.400）。
**主程序 + 插件架构**：主程序只是外壳（顶栏 / 左侧导航 / 主题 / 插件加载与管理），
每个验机功能模块都是一个独立插件（class library），放在 exe 同级 `plugins/<id>/` 目录下，
用户可在安装向导勾选模块、也可在程序内「插件管理」里导入 zip 插件包或删除插件（重启生效）。

硬性约束：开源软件，**禁止集成闭源商业软件**（如 HWiNFO）。硬件采集只用
LibreHardwareMonitor 源码（MPL-2.0）+ Windows 自带接口（WMI / 注册表 / Win32 API）。
详细工程背景与硬件采集的坑见 `项目交接文档.md`（先通读再动采集相关代码）。

## 目录结构

```
【视频】验机软件/
├── 验机软件.slnx              # 解决方案
├── 验机软件/                   # ★ 宿主外壳（WinExe，AssemblyName=验机软件，RootNamespace=YanJi）
│   ├── App.xaml(.cs)          # 启动：加载主题 + 全局异常落盘 %TEMP%\YanJi-crash.log
│   ├── MainWindow.xaml(.cs)   # 外壳：顶栏（插件管理按钮+主题开关）+ 动态导航 + 内容区
│   ├── Services/
│   │   ├── PluginLoader.cs         # 插件扫描/加载 + 依赖解析兜底钩子
│   │   ├── PluginPackageService.cs # zip 插件包导入/删除
│   │   ├── ThemeManager.cs / TitleBarTheme.cs
│   ├── Views/PluginManagerWindow   # 插件管理窗口（列表/删除/导入 zip）
│   ├── Themes/                # Light.xaml / Dark.xaml（令牌）+ Controls.xaml（隐式样式）
│   └── Tools/                 # 烤机外部 exe 负载（prime95/furmark/pawnio，散热插件用）
├── PluginSdk/                 # ★ 插件契约（YanJi.PluginSdk.dll）：接口 + 共享服务
│   ├── Contracts.cs           # IYanJiPlugin / IModulePage / IKeyHandlerPage / IPluginShutdown / IHostContext
│   └── Services/              # DisplayInfoService + EdidParser（多个插件共用才放这里）
├── Plugins/                   # ★ 五个内置模块插件（每个 = 一个 class lib 工程 + plugin.json）
│   ├── ConfigCheck/           # 配置核对（LHM + WMI 采集）      id=config    order=10
│   ├── KeyboardTest/          # 键盘测试（104 键）             id=keyboard  order=20
│   ├── ScreenDeadPixel/       # 屏幕坏点（全屏纯色）           id=screen    order=30
│   ├── AvConference/          # 影音会议（摄像头/麦/扬声器）   id=av        order=40
│   └── StressTest/            # 散热测试（烤机+传感器监控）    id=stress    order=50
├── FurMark/                   # GPU 烤机工具工程（构建后拷到 验机软件/Tools/furmark/）
├── prime95-build/             # prime95 叠加构建工作区（不进 slnx）
├── 集成开源项目代码/           # 开源组件源码（LibreHardwareMonitorLib 被插件项目引用）
├── installer/setup.iss        # Inno Setup 安装向导（可选组件 = 逐模块勾选）
└── publish/                   # 发布产物（self-contained 便携包，plugins/ 在根下）
```

## 构建 / 运行 / 发布

```bash
dotnet build 验机软件.slnx          # 构建全部（宿主 BuildPlugins target 会先建插件再拷到输出）
dotnet run --project 验机软件       # 运行（exe 是 requireAdministrator，开发期弹 UAC 属正常）

# 注意：全新克隆首次构建后再执行一次（或先单独 dotnet build FurMark）——
# Tools/furmark 的 None glob 在评估期求值而 FurMark 负载是构建期生成的，第二次构建才进输出。

# 发布（两条命令顺序不可换；publish/ 为自包含便携包，目标机无需装 .NET）
dotnet publish FurMark/FurMark.csproj -p:PublishProfile=FolderProfile
dotnet publish 验机软件/验机软件.csproj -p:PublishProfile=FolderProfile

# 安装向导（Inno Setup 6，产物 installer/笔吧验机Setup-v1.0.3.exe）
"%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" //Q "D:\【视频】验机软件\installer\setup.iss"
```

宿主构建链路：`BuildPlugins`（BeforeBuild，MSBuild 任务建 5 个插件工程）→ 插件构建 →
`CopyPluginsToOutput`（AfterTargets=Build，插件输出拷到 `$(OutDir)plugins/<id>/`）；
publish 时另有 `CopyPluginsToPublish`（AfterTargets=Publish）同步到 `publish\plugins\`。
**新增内置插件时，这两个 target 与 installer/setup.iss 的 [Components]/[Files] 都要加对应条目。**

## 插件机制

### 运行时布局与加载

```
验机软件.exe
├── YanJi.PluginSdk.dll        # 契约（插件与宿主共享同一份类型）
└── plugins/
    └── <id>/
        ├── plugin.json        # 清单（必需）
        ├── <入口>.dll         # 插件程序集
        └── *.dll / 其它资源    # 插件依赖（NAudio 等）与附带文件（如试音曲 mp3）
```

- 宿主启动时 `PluginLoader` 扫 `plugins/*/plugin.json`，校验后用
  `AssemblyLoadContext.Default.LoadFromAssemblyPath` 加载入口程序集并实例化 `entryType`。
- 插件引用 `YanJi.PluginSdk` 必须 `Private=false`（不拷进插件目录，运行时与宿主共享同一类型，
  否则接口类型不识别）。插件工程需设 `CopyLocalLockFileAssemblies=true`（类库默认不拷 NuGet 依赖到输出）。
- 插件的依赖 dll 放插件目录；宿主挂了 `AssemblyLoadContext.Default.Resolving` 兜底钩子，
  默认解析失败时到各插件目录找同名 dll（先加载者胜出，同名同版本天然去重），
  **并会下钻 `runtimes/win-x64/lib/**` 找 RID 专属托管资产**（如 LHM 的传递依赖
  Mono.Posix.NETStandard——缺它 LHM 的 OpCode.Open 在 JIT 时抛异常，采集功能全灭）。
- **增删插件 = 增删 `plugins/<id>/` 目录**。生效时机：**新插件导入后宿主帅即加载（免重启）**；
  删除/覆盖安装正在运行的插件时其 dll 被默认加载上下文锁定（运行时不卸载程序集），
  写入 `%APPDATA%\YanJi\plugin-ops.json` 待处理队列，提示后**自动重启**——下次启动在插件
  加载前统一执行（此时无文件锁）。「插件管理」窗口（顶栏按钮）封装了这套流程。
- ⚠️ 插件与宿主同进程、同**管理员权限**（宿主 manifest 是 requireAdministrator）——只安装可信来源插件。

### plugin.json 字段

```json
{
  "id": "screen",              // 目录名/唯一标识：小写字母数字 - _ ，≤32 字符
  "name": "屏幕坏点",           // 导航显示名
  "version": "1.0.0",
  "order": 30,                 // 导航排序，小的在前；内置插件按 10/20/.../90 留间隔便于插入
  "terminal": false,           // true = 终点页：无「确认通过」、不配状态圆点、不参与通过状态流
  "entryAssembly": "YanJi.Plugin.ScreenDeadPixel.dll",
  "entryType": "YanJi.Plugin.ScreenDeadPixel.ScreenDeadPixelPlugin"  // 全限定名，公共无参类，实现 IYanJiPlugin
}
```

### 契约（PluginSdk/Contracts.cs）

```csharp
public interface IYanJiPlugin                       // 入口，必需
{
    PluginInfo Info { get; }                        // 与 plugin.json 一致
    IReadOnlyList<PluginCredit> Credits => [];      // 本模块的致谢（开源组件/专有软件/素材版权；展示在插件管理窗口）
    UserControl CreatePage(IHostContext host);      // 宿主调用一次，页面实例常驻（切导航不丢状态）
}

public record PluginCredit(string Name, string Usage, string License);  // 致谢条目：名称/用途/协议

public interface IModulePage                        // 页面实现：「可通过」模块
{
    event Action<bool>? PassChanged;                // 确认通过=true，重新检测/重置=false
}

public interface IKeyHandlerPage                    // 页面实现：需要物理按键（键盘测试）
{
    bool HandleKey(Key key, bool down);             // 返回 true = 吞掉该按键
}

public interface IPluginShutdown                    // 插件实现：关程序时要清理（散热测试停烤机）
{
    void Shutdown();
}

public interface IHostContext                       // 宿主注入 CreatePage
{
    IReadOnlyList<PluginInfo> Modules { get; }      // 已加载全部模块（需要全模块清单的插件用，如汇总类终点页）
    bool IsDone(string moduleId);
    event Action? DoneChanged;                      // 任一模块通过状态变化（UI 线程）
}
```

宿主行为约定：
- 页面订阅 `IModulePage.PassChanged` 后统一处理：圆点变绿/还原 + 通过时自动跳下一个未完成模块；
  全部非终点模块完成 → 跳终点页 + 导航底部「🎉 全部验机项目已完成」。
- `PassChanged` 回退（false）要能还原圆点并撤掉全部完成提示。
- 物理按键由宿主 PreviewKeyDown/Up 隧道统一路由给当前页的 `IKeyHandlerPage`（已过滤 IsRepeat；
  Space/Enter 等已 Handled 的也能收到）。

### 开发一个新插件（完整步骤）

1. 建工程：仿照 `Plugins/ScreenDeadPixel/` 建 class lib（`Microsoft.NET.Sdk`，
   `TargetFramework=net10.0-windows10.0.19041.0`、`UseWPF=true`、`Nullable/ImplicitUsings=enable`、
   `CopyLocalLockFileAssemblies=true`），`ProjectReference` 引 `..\..\PluginSdk\YanJi.PluginSdk.csproj`
   并加 `Private="false"`。
2. 写页面：一个 `UserControl`（XAML + code-behind，无 MVVM 框架）。需要「确认通过」就实现
   `IModulePage`；需要按键实现 `IKeyHandlerPage`；关程序要清理实现 `IPluginShutdown`（在入口类上）。
3. 写入口类：公共无参类实现 `IYanJiPlugin`，`CreatePage` 返回页面实例；**模块用到的第三方组件/素材
   必须填 `Credits`**（名称/用途/协议或许可，展示在插件管理窗口——致谢随插件走，插件删了致谢也消失；
   专有软件也可列，注明许可即可，不要叫「开源致谢」）。
4. 写 `plugin.json`（`None Update` + `CopyToOutputDirectory=PreserveNewest` 随输出拷贝）。
5. 本地调试：把插件输出目录（含 plugin.json 与依赖）整体拷到宿主输出
   `验机软件/bin/<cfg>/net10.0-windows10.0.19041.0/plugins/<id>/`，启动宿主即可；
   要内置随包就把工程加进 slnx + 宿主 csproj 的 BuildPlugins/CopyPlugins 两个 target +
   installer/setup.iss 的 [Components]/[Files]。
6. 打包分发：把插件目录（plugin.json 在 zip 根或一层包裹目录下）压成 zip，
   用户经「插件管理 → 导入插件包 (.zip)…」导入，重启生效。示例：
   `powershell Compress-Archive -Path "plugins\myplugin\*" -DestinationPath myplugin.zip`
   （plugin.json 直接在 zip 根；或 `Compress-Archive -Path "plugins\myplugin" ...` 形成一层包裹目录，两种都支持）。

### UI 约定（与全局风格一致，验收会看）

- 主题：只用语义画刷令牌（`WindowBgBrush`/`SurfaceBgBrush`/`SubtleBgBrush`/`TextBrush`/
  `TextSecondaryBrush`/`BorderBrush`/`AccentBrush`/`ErrorBrush` 等，键集见 Themes/Light.xaml，
  深/浅两个字典键集必须一致），**全部 DynamicResource 引用，不写字面颜色**；
  控件样式复用宿主 Controls.xaml 的隐式样式（Button/ComboBox/CheckBox/TextBox 等已扁平化）。
- 「测试通过」语义色用 `ForestGreen`（导航圆点/状态行/清单 ✓）。
- 状态行/圆点等资源还原用 `SetResourceReference` 恢复动态绑定，直接赋画刷会断主题切换。
- 日志区惯例：只读 TextBox（Consolas 11）外套 `CornerRadius=6` 的 Border（LogBox 风格）；
  详情折叠区用 Controls.xaml 的 `DetailsExpander` 样式。
- 页面实例常驻：`Loaded` 会随切回重发（自动初始化要防重入），`Unloaded` 用来释放设备/停后台线程
  （摄像头灯、传感器轮询、烤机进程都靠它兜底）。

### 已知坑（插件化相关）

- **System.Management 双份实现**：NuGet 的 lib/ 下是 74KB PNSE 桩，真身在 runtimes/win（314KB）。
  插件目录里拷到的是桩，运行时靠**宿主 deps.json/TPA** 解析到真身——所以宿主 csproj 保留着
  System.Management 的 PackageReference（宿主代码并不直接用 WMI），**勿删**。
- **LibreHardwareMonitorLib 必须按 x64 构建**：插件引用它时 ProjectReference 必须带
  `SetPlatform="Platform=x64"`（CsWin32 部分 API 只在指定架构生成，AnyCPU 会编译失败）。
- 发布时插件工程不能继承宿主的 RuntimeIdentifier/SelfContained——宿主 BuildPlugins 的
  MSBuild 任务已 `RemoveProperties="RuntimeIdentifier;SelfContained"`，仿建即可。
- 安装向导的「可选组件」与 `plugins/<id>/` 目录一一对应；Tools/ 随散热组件安装。

## 修改守则

- 最小改动；新代码风格对齐周边（注释密度、命名、UserControl+code-behind、无第三方 UI 库）。
- 改采集逻辑前先读 `项目交接文档.md` 第三节的坑（EDID/SMART/色域/LHM 降级矩阵都有实测结论）。
- 试音曲署名（AvConference 页面）是用户给定原文，不得改动。
- 临时探针工程（.ui-probe 等）用完即删；LHM 上游源码不要批量转码/改动。
- 改了本文件描述的机制（目录、契约、构建链路、安装组件）时同步更新本文件。
