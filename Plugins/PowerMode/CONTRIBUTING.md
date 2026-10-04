# 性能模式社区适配指南

PowerMode 是品牌中立的框架。`Core` 只管理身份、供电和 Provider 调度；`UI` 只调用契约。
Windows 是通用兜底，华硕是其中一个参考适配器。联想、戴尔、惠普骨架当前明确返回不支持；
宏碁、机械革命、神舟等品牌可按同样的接口新增适配器，不必修改宿主或页面。

## 用户界面文案

页面只呈现用户需要的模式、供电、切换结果与操作建议，不显示 ACPI、Provider、
调度、读回、社区插槽等开发术语，也不在页面提供社区适配入口。开发者说明集中在本文件。
`CurrentModeSource` 保留技术诊断信息；用户界面用“部分电脑无法确认原厂模式，请在电脑自带的控制中心核对”
说明读回限制。切换部分成功仍须明确告诉用户哪一步失败，不能简化成全部成功。

## 接入一个品牌

1. 在 `Providers` 下实现 `Core.IPowerModeProvider`，返回厂商匹配标识 `SupportedBrand`。
   核心对注册表 `SystemManufacturer` 做不区分大小写的包含匹配；`*` 表示全品牌。
   若品牌有多种厂商标识，可分别注册实例；设备型号/驱动版本限制应放在 `IsSupported` 内。
2. `IsSupported` 只读取能力，不能切档。必须确认目标驱动、机型和接口支持位；
   仅检测到品牌名或某个 WMI 类不足以宣称支持。探测异常/不支持时管理器继续选择下一 Provider。
3. 将静音、平衡、增强映射到官方预设策略。不要修改电压、风扇曲线或执行超频。
   不得集成闭源工具；可以通过 Windows 自带接口调用用户已安装的原厂驱动。
4. `GetCurrentMode` 应读取系统/固件状态。不能可靠归类时抛异常，页面显示未知。
   `CurrentModeSource` 必须明确区分实际读回、计划推断、Windows 状态和固件状态。
   不要把“上次发送的档位”持久化后当成当前实际档位。
5. `SetMode` 返回两层策略都完成的结果；`LastMessage` 说明拒绝、失败和部分成功。
   系统策略变更可恢复时恢复；未知原固件档位不能盲目回滚。增强操作由管理器在命令前检查交流供电，
   对有更严格供电要求的机型，Provider 还需自行检查。
6. 在 `PowerModePlugin.CreateManager` 注册适配器：`manager.Register(new ExampleProvider(), priority: 100)`。
   高优先级先尝试，同优先级保持注册顺序，Windows 通用 Provider 最后兜底。
   更改厂商适配代码不需要修改 `Core`、`UI`、PluginSdk 或其他插件。
7. 若用了第三方源码/协议参考，补充插件 `Credits` 和所需许可证；遵守原代码的许可证要求。

联想可调研 `root\wmi` 下的 `Lenovo_VpcWmi`，但不同产品线的方法、参数、返回值均需真机确认。
戴尔 AWCC、惠普 OMEN 等接口同样不得凭接口名称直接下发未验证命令。

## 当前实现边界

- Windows 优先使用平衡计划上的最佳能效 / 平衡 / 最佳性能模式，并做读回校验。
  若不可用，选择已经存在的省电 / 平衡 / 高性能或卓越性能计划。不创建重复计划。
  系统不存在所需预设、策略被组策略限制、或读回不一致时明确失败并尝试恢复原 Windows 策略。
  通用调度不能保证解锁原厂 EC 的功耗上限，也不保证所有硬件有性能收益。
- 华硕参考 ASUSMode 原型的协议，使用 ATKACPI 与原厂
  Silent / Performance / Turbo 计划。缺原厂计划时联动 Windows 通用策略。
  DSTS 用支持位探测，不把能力值当档位；技术状态来源保存在 CurrentModeSource，页面用易懂的提示引导用户核对原厂模式。
- Provider 由插件注册表动态注册/选择，不扫描额外 DLL，也不向 SDK 添加品牌代码。
  插件导入沿用宿主现有 zip 加载流程。
- 页面仅在可见时每 3 秒读取状态，切页停止轮询；句柄每次操作后释放。
  切档、刷新、供电变化或外部可读状态变化会撤回“已确认通过”。
  没有可读状态时不能确认通过。切档不自动通过，用户必须亲自确认。
- 切换后的系统/原厂策略会保留，切页或退出不会自动还原。可点击平衡或在原厂/Windows 设置中恢复。

## 构建与发布

本插件默认随主程序内置发布，仓库只保存源码、清单与文档，不提交编译输出或 zip。
解决方案已包含插件工程，宿主构建/发布会将文件复制到 `plugins/power/`，安装脚本也已包含对应组件。

在仓库根目录执行 `dotnet build 验机软件.slnx` 即可构建；完整发布流程见根目录 README。

### 单独打包（可选）

如需单独分发插件，软件的 zip 导入功能需要**编译后的插件文件**，不能直接压缩源码目录。
在仓库根目录执行（需要 .NET SDK 10）：

```powershell
dotnet build Plugins/PowerMode/YanJi.Plugin.PowerMode.csproj -c Release
New-Item -ItemType Directory -Path out -Force | Out-Null
Compress-Archive -Path 'Plugins/PowerMode/bin/Release/net10.0-windows10.0.19041.0/*' -DestinationPath 'out/PowerMode.zip' -Force
```

zip 根目录必须包含 `plugin.json`、`YanJi.Plugin.PowerMode.dll` 以及构建输出中的依赖文件。
`YanJi.PluginSdk.dll` 由宿主提供，不应放进插件包。软件不会将包内的 `.cs` / `.xaml` 源码自动编译。
打开已安装的软件，在「插件管理」中导入 `out/PowerMode.zip`；已有同名插件时按提示覆盖并重启。

## 真机验收

真机验收必须由用户完成：

- 无专属适配的设备进入页面应选择 Windows；查看静音/平衡/增强是否与系统设置一致。
- 插拔适配器，确认状态刷新，电池或供电未知时增强禁用，已有通过状态撤回。
- 华硕设备核对原厂控制中心三档、风扇/功耗行为及 Windows 策略，关注读回限制。
- 未安装原厂驱动/原厂计划，确认回退和失败原因明确；不把联想/戴尔/惠普骨架显示为已支持。
- 验证深浅主题、窄窗口、切页再回来、通过跳下一模块、重新检测撤回导航绿点。
- 通过“插件管理”导入 `power` zip；确认免重启加载，删除/覆盖按宿主原有重启流程执行。

公开接口参考：[Windows 电源方案 API](https://learn.microsoft.com/en-us/windows/win32/power/managing-power-schemes)、
[G-Helper 原厂协议实现](https://github.com/seerge/g-helper/blob/main/app/AsusACPI.cs)。
