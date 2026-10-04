; 笔吧验机 安装向导脚本（Inno Setup 6，中文界面）
; 构建："%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" installer\setup.iss
; 源 = publish\ 整目录（self-contained 便携包）；产物 = installer\笔吧验机Setup-v0.0.4.exe
; 注意：OutputBaseFilename 不能含多余的点（"Setup-0.0.2" 的 ".0" 会被 Inno 当扩展名剥掉），用 v0.0.2 形式
;
; 插件化结构：主程序 = 验机软件.exe + 运行时 + YanJi.PluginSdk.dll（组件 main，固定必装）；
; 每个验机模块 = publish\plugins\<id>\ 目录（一个可选组件，默认全勾，自定义安装可取消）；
; Tools\（prime95 + FurMark 自包含 + PawnIO，约 130MB）只有散热测试用，随 stress 组件安装。
; 0.0.2：新增 disk 组件（硬盘跑分，开源）；附加任务新增「安装 PawnIO 驱动」（默认勾选，
; 安装末尾静默执行 Tools\pawnio\PawnIO_setup.exe -install -silent（用户零点击，无需重启；
; 未装散热组件或系统已装 PawnIO 时自动跳过）。
; 0.0.3：配置核对新增「电池」分组（设计/完全充电容量、损耗、循环次数），无电池设备显示提示。
; 0.0.4：配置核对硬盘部分新增「介质与数据完整性错误」（NVMe SMART，即 CrystalDiskInfo 的 0E 项）。

; LZMA2 块级并行线程数：自动取本机逻辑处理器线程数（NUMBER_OF_PROCESSORS），取不到回落 8
#define BlockThreads GetEnv("NUMBER_OF_PROCESSORS")
#if BlockThreads == ""
  #undef BlockThreads
  #define BlockThreads 8
#endif

[Setup]
AppName=笔吧验机
AppVersion=0.0.4
AppVerName=笔吧验机 0.0.4
AppPublisher=笔吧验机
; 只面向 x64 Windows（self-contained win-x64），且装到 C:\Program Files（而非 (x86)）
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; 默认目录用纯英文 BibaInspection：规避外部组件/CLI 对中文路径的兼容问题
DefaultDirName={autopf}\BibaInspection
DefaultGroupName=笔吧验机
PrivilegesRequired=admin
OutputDir=.
OutputBaseFilename=笔吧验机Setup-v0.0.4
Compression=lzma2/max
SolidCompression=yes
; 块级并行压缩：多线程各压一块（压缩率略降）；独立 64 位压缩进程，规避 32 位编译器内存上限
LZMANumBlockThreads={#BlockThreads}
LZMAUseSeparateProcess=yes
VersionInfoVersion=0.0.4.0
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
Name: "config"; Description: "配置核对（CPU/显卡/内存/硬盘/网卡/屏幕/电池采集核对）"; Types: full
Name: "keyboard"; Description: "键盘测试（104 键可视化）"; Types: full
Name: "screen"; Description: "屏幕坏点（全屏纯色检测）"; Types: full
Name: "av"; Description: "影音会议（摄像头/麦克风/扬声器）"; Types: full
Name: "stress"; Description: "散热测试（CPU/GPU/双烤 + 传感器监控，含 prime95/FurMark 工具）"; Types: full
Name: "disk"; Description: "硬盘跑分（CrystalDiskMark 源码集成，SEQ1M/RND4K 四项）"; Types: full
Name: "report"; Description: "验机报告（硬件配置/确认记录/性能摘要，导出 PDF）"; Types: full

; 附加任务默认都勾选（用户可取消）：桌面快捷方式 / 开始菜单项 / PawnIO 驱动
[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务："
Name: "startmenu"; Description: "创建开始菜单项"; GroupDescription: "附加任务："
Name: "pawnio"; Description: "安装 PawnIO 内核驱动（CPU/主板传感器需要；官方签名安装器，静默安装无需重启）"; GroupDescription: "附加任务："

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
Source: "..\publish\plugins\disk\*"; DestDir: "{app}\plugins\disk"; Components: disk; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\publish\plugins\report\*"; DestDir: "{app}\plugins\report"; Components: report; Flags: ignoreversion recursesubdirs createallsubdirs
; 烤机工具负载只有散热测试用：随 stress 组件安装（不勾散热可省约 130MB）
Source: "..\publish\Tools\*"; DestDir: "{app}\Tools"; Components: stress; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\笔吧验机"; Filename: "{app}\验机软件.exe"; Check: WizardIsTaskSelected('startmenu')
Name: "{group}\卸载 笔吧验机"; Filename: "{uninstallexe}"; Check: WizardIsTaskSelected('startmenu')
Name: "{commondesktop}\笔吧验机"; Filename: "{app}\验机软件.exe"; Check: WizardIsTaskSelected('desktopicon')

; 安装末尾自动安装 PawnIO 内核驱动（默认勾选的附加任务）。
; 必须 -install -silent：只传 -install 时其安装器会弹版权确认窗（已实测）——
; 零点击的唯一路径就是全静默（与程序内「安装 PawnIO 驱动」按钮同款命令）。
; 两道 Check 防呆：① PawnIO 安装器随 stress 组件的 Tools\ 安装——未选散热组件时文件不存在自动跳过；
; ② 系统已装 PawnIO 时也跳过（其安装器对已装状态会弹「请先卸载」的阻塞对话框，违背零点击原则）
[Run]
Filename: "{app}\Tools\pawnio\PawnIO_setup.exe"; Parameters: "-install -silent"; StatusMsg: "正在安装 PawnIO 内核驱动…"; Flags: runhidden waituntilterminated; Check: WizardIsTaskSelected('pawnio') and FileExists(ExpandConstant('{app}\Tools\pawnio\PawnIO_setup.exe')) and (not PawnIOInstalled())

; 卸载 = Inno 默认行为：删除 {app} 内已安装文件与快捷方式；%TEMP% 日志不在安装目录，天然保留。
; %APPDATA%\YanJi（用户设置/跑分结果）由卸载向导的「删除用户数据」勾选决定（见末尾 [Code]，默认勾选删除）
;
; 0.0.2 补充：默认卸载只删安装时登记的文件——安装后经「插件管理」导入（或手工拷贝）的
; 插件目录会残留。这里把 plugins\ 下全部内容列入卸载删除，保证完全卸载
[UninstallDelete]
Type: filesandordirs; Name: "{app}\plugins\*"

[Code]
{ PawnIO 是否已安装（注册表卸载项，与程序内 SensorMonitorService 的判据一致） }
function PawnIOInstalled(): Boolean;
begin
  Result := RegKeyExists(HKLM, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\PawnIO');
end;

{ 卸载时的「删除用户数据」勾选框（0.0.2 新增，默认勾选）：
  %APPDATA%\YanJi（软件设置、跑分结果、插件待处理队列等）默认随卸载删除，用户可取消勾选保留。
  注意：① Inno 不允许卸载期 CreateCustomPage（报 Cannot call ... during Uninstall），
  只能用 CreateCustomForm 自建模态对话框——它在 Inno 自带确认框之前弹出；
  ② 只清当前卸载发起用户的 Roaming，多用户机器上其它用户的不动；
  ③ /VERYSILENT 静默卸载不弹该对话框，DeleteUserData 保持默认 True（即会删除） }
var
  DeleteUserData: Boolean;

function InitializeUninstall(): Boolean;
var
  Form: TSetupForm;
  Check: TNewCheckBox;
  BtnOk, BtnCancel: TNewButton;
  Lbl: TNewStaticText;
begin
  DeleteUserData := True; { 静默卸载/异常路径下的默认值 }
  Form := CreateCustomForm(ScaleX(420), ScaleY(150), False, True);
  Form.Caption := '卸载 笔吧验机';
  Form.Position := poScreenCenter;

  Lbl := TNewStaticText.Create(Form);
  Lbl.Parent := Form;
  Lbl.Caption := '即将卸载 笔吧验机。是否同时删除本软件保存的用户数据？';
  Lbl.Left := ScaleX(16);
  Lbl.Top := ScaleY(12);
  Lbl.Width := Form.ClientWidth - ScaleX(32);
  Lbl.AutoSize := False;
  Lbl.WordWrap := True;

  Check := TNewCheckBox.Create(Form);
  Check.Parent := Form;
  Check.Caption := '删除用户数据（软件设置、跑分结果等）';
  Check.Checked := True;
  Check.Left := ScaleX(16);
  Check.Top := ScaleY(52);
  Check.Width := Form.ClientWidth - ScaleX(32);

  BtnOk := TNewButton.Create(Form);
  BtnOk.Parent := Form;
  BtnOk.Caption := '卸载(&U)';
  BtnOk.ModalResult := mrOk;
  BtnOk.Default := True;
  BtnOk.Left := Form.ClientWidth - ScaleX(192);
  BtnOk.Top := Form.ClientHeight - ScaleY(36);
  BtnOk.Width := ScaleX(88);

  BtnCancel := TNewButton.Create(Form);
  BtnCancel.Parent := Form;
  BtnCancel.Caption := '取消(&C)';
  BtnCancel.ModalResult := mrCancel;
  BtnCancel.Cancel := True;
  BtnCancel.Left := Form.ClientWidth - ScaleX(96);
  BtnCancel.Top := BtnOk.Top;
  BtnCancel.Width := ScaleX(88);

  Result := Form.ShowModal() = mrOk;
  if Result then
    DeleteUserData := Check.Checked;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    if DeleteUserData then
      DelTree(ExpandConstant('{userappdata}\YanJi'), True, True, True);
end;
