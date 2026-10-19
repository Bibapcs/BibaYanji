# 硬件证据引擎开发入口

当前有两个独立跨语言接口：原 `demo-args-v1` 只校验参数；新增 [result-demo-v1](contracts/result-demo-v1.md) 由 Java 提交结构化人工结果、C# 严格接收。人工结果不能表示硬件核验通过。真实硬件来源、HTML、WPF 插件和发布运行时尚未接入。

## 构建与最小调用

Windows、JDK 21、Maven、.NET SDK 10。从仓库根执行；`$javaHome` 替换为本机已有 JDK 路径，不写入发布配置。

```powershell
$javaHome = 'D:/Program Files/Java/jdk-21.0.12.1'
mvn -f hardware-evidence/pom.xml test
dotnet build hardware-evidence/src/main/csharp/Caller.csproj -c Release
$classes = (Resolve-Path hardware-evidence/target/classes).Path
$runs = [IO.Path]::GetFullPath('hardware-evidence/out/manual-runs')
& ./hardware-evidence/src/main/csharp/bin/Release/net10.0/Caller.exe --result-demo "$javaHome/bin/java.exe" $classes $runs
```

Maven 编译原默认包 DemoCommand 与新入口，并将锁定的运行时依赖复制到 `target/classes/lib/`。当前仍以 class 目录调用，不引入 JAR 启动协议。原演示去掉 `--result-demo` 即可运行，仍不写任务文件。

正常新调用输出 `result_ready`、原 Java 退出码 0 以及通过校验的 response；任务内仅最终 `result.json`。每次创建新身份/目录，不自动覆盖、删除或重试。输入三项配置 → C# 创建目录并逐项启动 → Java 写临时文件并提交 → C# 校验当前最终文件 → 内存响应和控制台 JSON。

## 实现与复用

| 位置 | 职责 |
| --- | --- |
| `contracts/result-demo-v1.md` 与 fixtures | 共享语义及人工成功样例 |
| `src/main/java/cn/bibapcs/evidence/cli/ResultCommand.java` | 新请求入口；保持原 DemoCommand 原文 |
| `model/DemoResponse.java`、`report/JsonReportWriter.java` | 人工响应与标准 JSON 写入/提交 |
| `src/main/csharp/JavaCaller.cs` | 复用进程控制，新增 RunResultAsync(args, CancellationToken)；每路保留 1 MiB 日志并持续消费 |
| `CallerConfiguration.cs` | 复用配置校验；新接口用 Windows 原生创建保证不复用同名任务目录 |
| `ResultResponseReader.cs`、`ResultResponse.cs` | 16 MiB 上限、严格 UTF-8/JSON、重复键/身份/类型/版本/状态/引用/人工语义校验 |
| `cpuid-probe/` | 已有独立探针；输出形状尚未并入正式 collect 契约 |

生产引擎只使用 JDK 标准库与 Jackson 2.22.3（databind/core=2.22.3，annotations=2.22，Apache-2.0）。Jackson 官方 Maven POM 已核许可证/版本，实际依赖已解析；ObjectMapper.writeValueAsBytes 经 JDK 21 javap 与实际编译/运行确认，无手拼业务 JSON。测试用 JUnit 5.11.4（EPL-2.0）；Maven 构建插件 Apache-2.0。C# 仍无第三方 NuGet。后续插件入口必须携带对应 Credits。

## 可复现验证

```powershell
& ./hardware-evidence/tests/verify.ps1 -JavaHome $javaHome
# 本机 Maven 缓存目录与默认值不符时，显式传 -MavenRepository <已有缓存目录>。
```

脚本运行 Java 单元测试，单独编译测试故障进程，将真实 class/依赖复制到中文空格目录，再构建并运行 C# 接收与联调检查。真实引擎调用、纯接收样例测试、故障替身分别标注。测试产物及原始记录保留在 `hardware-evidence/out/`，Maven `target/`、.NET bin/obj 与 out 均不入库。

当前仍未验证目录权限拒绝/重解析点实跑、全部后代进程退出、宿主崩溃、页面生命周期和无预装 Java 发布。旧 `src/main/csharp/README.md`、validation.json 及 CSharp 交接正文是历史快照；当前入口以本文、新契约及新验证记录为准。
