# 独立 C# 调用方：demo-args-v1

> 2026-10-14 更新：下文保留初次独立交付的历史状态，不能代表当前 SDK/构建情况。
> 当前实际构建、原接口兼容调用、新结构化结果接口与验证入口见 [hardware-evidence README](../../../README.md) 和 [result-demo-v1](../../../contracts/result-demo-v1.md)。

唯一业务依据是本目录未修改的 `CONTRACT.md`。本项目使用 .NET 10 标准库，无第三方 NuGet 依赖，不包含 Java 实现。本次交付是源码及静态审阅；尚未编译、运行或与真实 Java 联调。

## 本次验证状态

2026-10-13，仅在本目录执行了一次 `dotnet --list-sdks`。Git Bash 返回 127，错误为 `dotnet: command not found`。这只能说明当前命令环境找不到 dotnet，不能证明机器其他位置不存在 SDK。未再次探测，未下载或安装，未修改环境变量或全局配置。

因此没有构建产物，没有运行过 Caller，也没有建立或运行子进程测试替身。详细命令证据、实际文件清单、C1–C6 覆盖与剩余风险见 `validation.json`。静态审阅不代表编译通过。

## 文件分工

| 文件 | 功能、背景与方法 |
|---|---|
| Caller.csproj | net10.0 控制台项目，启用可空检查与警告作为错误，不声明 PackageReference。 |
| Program.cs | 接收三个位置参数，调用流程，输出中文分类说明及内存结果 JSON，以 Caller 自身约定退出。 |
| CallerConfiguration.cs | 校验 Windows 完整绝对路径、启动器和入口文件类型；创建不复用的任务目录。 |
| CallerResult.cs | 保存身份、目录、分类、Java 原退出码及两路日志，区分 Caller 自身退出码。 |
| JavaCaller.cs | 逐项启动，双管道 UTF-8 异步读取，实际退出码分类，十秒自然等待及三秒收尾。 |

## 构建与运行命令

以下为未来在 **Windows 且已经具有可用 .NET 10 SDK** 的环境执行的命令，本次没有执行。路径示例全部位于本独立目录内；其中 Java 启动器及 class 目录只是配置示例，目前不存在，必须由用户提供已有文件。不要把示例目录存在或 Java 21 兼容性视为已验证。

`dotnet run` 默认先构建指定项目，再运行调用方；采用这一构建加运行命令，以确保构建也显式使用 `--project`，不从默认 cwd 搜索工程：

```powershell
dotnet run --project "C:/Users/Administrator/WorkBuddy/biba-csharp-contract-demo-20261013/Caller.csproj" --configuration Release --no-launch-profile -- "C:/Users/Administrator/WorkBuddy/biba-csharp-contract-demo-20261013/Java 21/bin/java.exe" "C:/Users/Administrator/WorkBuddy/biba-csharp-contract-demo-20261013/编译产物 class" "C:/Users/Administrator/WorkBuddy/biba-csharp-contract-demo-20261013/演示任务 runs"
```

已成功构建后，只运行、不再次构建：

```powershell
dotnet run --project "C:/Users/Administrator/WorkBuddy/biba-csharp-contract-demo-20261013/Caller.csproj" --configuration Release --no-build --no-launch-profile -- "C:/Users/Administrator/WorkBuddy/biba-csharp-contract-demo-20261013/Java 21/bin/java.exe" "C:/Users/Administrator/WorkBuddy/biba-csharp-contract-demo-20261013/编译产物 class" "C:/Users/Administrator/WorkBuddy/biba-csharp-contract-demo-20261013/演示任务 runs"
```

在 PowerShell 中，命令后可读取 `$LASTEXITCODE`。不指定 `--project` 的构建/运行命令不属于本交付的执行方式。Release 的预计产物位置是本目录下 `bin/Release/net10.0/Caller.exe`；当前没有这个产物。

Java 配置要求：启动器是已存在的 `java.exe`，能加载 Java 21 编译产物；class 根目录包含默认包入口 `DemoCommand.class`。调用方仅确认入口文件存在且不是目录，不能静态证明字节码、JVM版本、其他依赖可用。JVM启动和依赖错误由启动器退出码及日志表达。

## 调用与输出语义

调用方按以下顺序把八项文本直接加入 `ProcessStartInfo.ArgumentList`：

```text
-cp
<classDirectory>
DemoCommand
demo
--execution-id
<executionId>
--out
<taskDirectory>
```

路径项不额外包引号，不经 shell 拼接。Java 启动器处理前三项后，Java main 只会得到后面的五项。两路输出分别指定 UTF-8，并在等待进程前开始异步读取。结果中的 stdout/stderr 是各自管道的文本，JSON 中的换行转义是正常表示，不承诺跨管道的交错顺序。

每次先校验配置，再生成 `Guid.NewGuid().ToString("N")` 身份，创建 `runsRoot/run-<executionId>` 空目录，最后启动进程。已有同名文件或目录则拒绝，不复用、覆盖或清理。启动失败保留已经创建的身份与目录；创建前失败可以为 null。可创建缺失的 runsRoot，其已有父目录不清理。受控串行检查不具备跨进程原子独占或抗恶意目录置换能力。

控制台先打印分类说明，再打印以下字段组成的 JSON：

| 字段 | 语义 |
|---|---|
| executionId | 已创建任务的 32 位小写十六进制身份；创建前失败可为 null。 |
| taskDirectory | 实际创建的完整绝对目录；创建前失败可为 null。 |
| status | validated、request_invalid、java_failure、caller_failure 或 timeout。 |
| javaExitCode | 已自然观察到的 Java 原退出码；启动失败、超时为 null。 |
| stdout / stderr | 两路读取结果。读完的一路会保留；未完成或读取失败的一路返回空文本，并在 error 标注不完整。 |
| error | 调用方失败或超时收尾事实；正常结果通常为 null。 |

JSON 仅打印到控制台，不写 `result.json` 或 `result.tmp`，也不冻结后续业务 JSON 协议。未运行，本文没有伪造的成功输出样例。

| 自然退出或调用状态 | status | javaExitCode | Caller 自身退出码 |
|---|---|---|---|
| Java 自然退出 0，日志读取成功 | validated | 0 | 0 |
| Java 自然退出 2，日志读取成功 | request_invalid | 2 | 2 |
| Java 自然退出其他非零值，日志读取成功 | java_failure | 原值 | 1 |
| 配置、创建、启动、等待、日志读取失败 | caller_failure | 仅已自然观察到的退出码可保留，否则 null | 1 |
| 十秒内没有自然结束 | timeout | null | 1 |

分类依赖退出码，不解析 `REQUEST_VALIDATED` 或中文解释行作为成败依据。即使替身打印成功标记但退出 2，仍应是 request_invalid。Java 退出 0 但日志收尾失败时归 caller_failure，已观察到的原退出码 0 仍保留。

成功只显示“本次参数校验通过”。没有硬件检测、WPF、真实宿主或网络通信，不生成业务报告。

## 超时与资源收尾

从进程启动后开始十秒自然等待。超时后只对本次持有的 Process 调用 `Kill(entireProcessTree: true)`，不按名称扫描其他进程。停止后最多等待三秒确认本次进程退出，两路日志共同最多再等待三秒；自然结束同样有三秒日志读取上限。

停止请求异常、确认等待超时、读取异常或日志不完整都会写入 error。超时分类保持 timeout，不借用停止后退出码，也不声称一定停止成功。已退出的本次进程无需再 Kill。即使确认本次进程已退出，也不能据此证明全部后代已退出。

读取任务及合并任务都有故障观察；超限时取消读取并关闭读端，避免无限等待，随后释放 Process 与取消源。不自动重试、不删除任务目录。十秒、三秒、三秒限定的是异步等待与日志收尾期限，不能据此声称同步文件系统操作、Process.Start、Kill 和 Dispose 一定让全流程精确在十六秒内返回。

## 后续验收清单（本次全部未实跑）

| 契约 | 需要实跑的断言 | 本次证据 |
|---|---|---|
| C1 | 连续两次身份/目录不同；子进程启动瞬间目录已存在、为空；目录持续保留。 | 静态审阅创建顺序及 Guid；运行未验证。 |
| C2 | 启动器、class、runsRoot 含中文空格，替身收到恰好八项且路径文本完整。 | 静态审阅 ArgumentList；运行未验证。 |
| C3 | 并发双管道输出超过管道缓冲仍不死锁；自然退出 0、2、其他非零分别分类；日志内容不左右分类。 | 静态审阅并行读取及映射；运行未验证。 |
| C4 | 缺失启动器/入口、相对路径、文件冒充 runsRoot 为 caller_failure；存在但不可执行的 java.exe 启动失败仍保留任务；与退出 2 区分。 | 静态审阅配置/启动异常路径；运行未验证。 |
| C5 | 替身超过十秒，保留身份/目录、javaExitCode=null；自身进程收尾有界，其他无关进程不被停止，无重试/删除。 | 静态审阅 Kill 范围及期限；运行未验证。 |
| C6 | 将子进程替身的输出、退出码及实跑时间记录为“替身测试”；真实 Java 联调另列证据。 | 已明确证据边界；未建立或运行替身，真实 Java 联调未验证。 |

未来若建立替身，必须使用明确标注的独立 C# 测试子进程；仅为配置预检查准备的入口占位文件应标记为非 Java 字节码，不能称真实 Java 联调。本次没有这些文件。

没有需要回查原工程才能决定的业务歧义。合同对等待/日志错误的结果对象已足够明确；控制台 JSON 的显示格式属于本地说明形式。可能存在并发目录竞争、链接置换、无限日志及进程树权限限制，均不在本版已验证能力之内。合同保持原样，未提出或实施行为变更。
