; 笔吧验机 安装向导脚本（Inno Setup 6，中文界面）
; 构建："%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" installer\setup.iss
; 源 = publish\ 整目录（self-contained 便携包）；产物 = installer\笔吧验机Setup-v1.0.3.exe
; 注意：OutputBaseFilename 不能含多余的点（"Setup-1.0.0" 的 ".0" 会被 Inno 当扩展名剥掉），用 v1.0.3 形式
;
; 插件化结构：主程序 = 验机软件.exe + 运行时 + YanJi.PluginSdk.dll（组件 main，固定必装）；
; 每个验机模块 = publish\plugins\<id>\ 目录（一个可选组件，默认全勾，自定义安装可取消）；
; Tools\（prime95 + FurMark 自包含 + PawnIO，约 130MB）只有散热测试用，随 stress 组件安装。

; LZMA2 块级并行线程数：自动取本机逻辑处理器线程数（NUMBER_OF_PROCESSORS），取不到回落 8
#define BlockThreads GetEnv("NUMBER_OF_PROCESSORS")
#if BlockThreads == ""
  #undef BlockThreads
  #define BlockThreads 8
#endif

[Setup]
AppName=笔吧验机
AppVersion=1.0.3
AppVerName=笔吧验机 1.0.3
AppPublisher=笔吧验机
; 只面向 x64 Windows（self-contained win-x64），且装到 C:\Program Files（而非 (x86)）
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; 默认目录用纯英文 BibaInspection：规避外部组件/CLI 对中文路径的兼容问题
DefaultDirName={autopf}\BibaInspection
DefaultGroupName=笔吧验机
PrivilegesRequired=admin
OutputDir=.
OutputBaseFilename=笔吧验机Setup-v1.0.3
Compression=lzma2/max
SolidCompression=yes
; 块级并行压缩：多线程各压一块（压缩率略降）；独立 64 位压缩进程，规避 32 位编译器内存上限
LZMANumBlockThreads={#BlockThreads}
LZMAUseSeparateProcess=yes
VersionInfoVersion=1.0.3.0
VersionInfoCompany=笔吧验机
VersionInfoDescription=笔吧验机 安装向导
VersionInfoProductName=笔吧验机
UninstallDisplayName=笔吧验机
; 安装/卸载程序 exe 的图标 = 主程序同款 app.ico
SetupIconFile=..\验机软件\app.ico
WizardStyle=modern

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

; 安装类型：完整安装（全部模块）/ 自定义（逐项勾选模块）
[Types]
Name: "full"; Description: "完整安装"
Name: "custom"; Description: "自定义安装"; Flags: iscustom

; 可选组件：主程序固定必装；六个验机模块插件逐项可选（默认全勾）
[Components]
Name: "main"; Description: "主程序（必需）"; Types: full custom; Flags: fixed
Name: "config"; Description: "配置核对（CPU/显卡/内存/硬盘/网卡/屏幕采集核对）"; Types: full
Name: "keyboard"; Description: "键盘测试（104 键可视化）"; Types: full
Name: "screen"; Description: "屏幕坏点（全屏纯色检测）"; Types: full
Name: "av"; Description: "影音会议（摄像头/麦克风/扬声器）"; Types: full
Name: "stress"; Description: "散热测试（CPU/GPU/双烤 + 传感器监控，含 prime95/FurMark 工具）"; Types: full

; 两个附加任务默认都勾选（用户可取消）：桌面快捷方式 / 开始菜单项
[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务："
Name: "startmenu"; Description: "创建开始菜单项"; GroupDescription: "附加任务："

[Files]
; 主程序：除 plugins\ 与 Tools\ 外的全部内容（exe + 运行时 + PluginSdk + 依赖）。
; 注意不能带 createallsubdirs：它会把被 Excludes 排除的目录也空建出来（Tools\ 空目录就是这么来的）
Source: "..\publish\*"; DestDir: "{app}"; Excludes: "plugins\*,Tools\*"; Components: main; Flags: ignoreversion recursesubdirs
; 模块插件：每插件一个目录一个组件
Source: "..\publish\plugins\config\*"; DestDir: "{app}\plugins\config"; Components: config; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\publish\plugins\keyboard\*"; DestDir: "{app}\plugins\keyboard"; Components: keyboard; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\publish\plugins\screen\*"; DestDir: "{app}\plugins\screen"; Components: screen; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\publish\plugins\av\*"; DestDir: "{app}\plugins\av"; Components: av; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\publish\plugins\stress\*"; DestDir: "{app}\plugins\stress"; Components: stress; Flags: ignoreversion recursesubdirs createallsubdirs
; 烤机工具负载只有散热测试用：随 stress 组件安装（不勾散热可省约 130MB）
Source: "..\publish\Tools\*"; DestDir: "{app}\Tools"; Components: stress; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\笔吧验机"; Filename: "{app}\验机软件.exe"; Check: WizardIsTaskSelected('startmenu')
Name: "{group}\卸载 笔吧验机"; Filename: "{uninstallexe}"; Check: WizardIsTaskSelected('startmenu')
Name: "{commondesktop}\笔吧验机"; Filename: "{app}\验机软件.exe"; Check: WizardIsTaskSelected('desktopicon')

; 卸载 = Inno 默认行为：删除 {app} 内已安装文件与快捷方式；
; 不碰 %APPDATA%\YanJi（用户设置）与 %TEMP% 日志（不在安装目录，天然保留）
