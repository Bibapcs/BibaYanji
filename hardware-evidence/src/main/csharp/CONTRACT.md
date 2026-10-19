# C#与Java参数交接契约｜demo-args-v1

日期：2026-10-13。适用语言：C#调用方、Java被调用方。
本文件是独立实现所需的完整输入，不需要任何学习工程源码或历史对话。只规定本次参数校验演示；总体硬件协议、宿主接口与JSON结果协议没有因此冻结。本版沿用已讨论行为，新增调用方分类/超时等为可修改的演示默认。

## 1. 场景与目标

用户发起一次演示。C#生成任务身份，创建独立空目录，再启动一个Java子进程。Java接收字符串参数，验证身份与目录，打印短日志，结束进程。C#同时读取两路日志、等待进程结束、读取退出码并分类显示。

成功表示“本次参数校验通过”，不能显示“硬件正常”“检测通过”或“报告已生成”。当前不采硬件、不创建result.json/result.tmp、不启动WPF、不接真实宿主、不增加网络通信。以逐次进程方式先验证连接；常驻服务或JAR打包需要新版本契约。

## 2. C#输入与工作目录

独立控制台调用方命令：

```text
Caller <javaExecutable> <classDirectory> <runsRoot>
```

三项均非空Windows完整绝对路径；不依赖当前工作目录。
- javaExecutable：已存在的java.exe文件，运行时能加载Java21编译产物。
- classDirectory：已存在的编译产物根目录，包含默认包入口DemoCommand.class。调用方不依赖Java内部record或嵌套类的具体文件名；其他依赖由Java启动器检查。
- runsRoot：任务父目录，允许C#按配置创建，只供受控演示使用；不得把个人文件目录作为清理对象。已有父目录可保留和复用；如果路径是文件或不可访问则本次调用失败。

C#只使用.NET标准库，首选net10.0，与当前演示基线一致。未安装可用SDK时交付源码和明确未构建结论，不静默下载SDK、不绕过审批。没有第三方NuGet依赖。

## 3. 任务身份与目录责任

C#在启动Java之前：
1. 校验配置路径。
2. 生成executionId：恰好32个ASCII小写十六进制字符（字符集合0-9和a-f），例如Guid的N格式。
3. 建立任务路径：runsRoot/run-<executionId>，使用完整绝对路径。
4. 确保本次路径原先不存在；若文件或目录已占用则拒绝，不复用，不清空，不覆盖旧文件。
5. 创建空目录后再启动Java；将实际同一identity/path传给Java并保留在本次调用结果中。

Java只检查已存在的目录，不负责创建或修复目录。Java也不凭目录名重新计算identity。

本次是受控本地串行演示；“检查路径不存在→创建目录”不构成跨进程原子独占。不得声称具备对抗并发/恶意目录置换能力。生产目录独占和权限强化后续验收。无论成功或失败，本次目录保留供核查，不做自动删除。

## 4. 进程级接口

逻辑启动形式（可读命令示意；真实实现必须逐项传参）：

```text
<javaExecutable> -cp <classDirectory> DemoCommand demo --execution-id <executionId> --out <taskDirectory>
```

C#将下列8项按顺序加入ProcessStartInfo.ArgumentList，禁止通过shell拼接一条带引号的命令：

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

实际Windows路径可以有中文和空格。ArgumentList里的路径项直接是路径文本，不额外包引号；命令显示时的引号只是可读表示。

Java main(String[] args)只收到类名之后5项：

```text
["demo", "--execution-id", "<executionId>", "--out", "<taskDirectory>"]
```

-cp、classDirectory和类名由Java启动器处理，不进入此args。

C#设置UseShellExecute=false、CreateNoWindow=true、RedirectStandardOutput=true、RedirectStandardError=true，并显式以UTF-8读取两路输出。

## 5. Java被调用方必须遵守的行为

Java21标准库程序，默认包主类名DemoCommand，入口public static void main(String[] args)。
- 只接受命令demo，参数总数恰好5项。
- --execution-id与--out各出现一次，两个选项可以交换顺序。
- 重复、未知选项、缺值、额外参数、空目录文本均拒绝。
- executionId必须匹配[0-9a-f]{32}，不自动转小写。
- --out必须为已存在且可作为目录访问的绝对路径；可做Path.normalize词法归一化，不等于解析符号链接或真实路径。
- 不接受文件冒充目录，目录内出现result.json或result.tmp则拒绝复用。
- 绝不因为输入目录不存在就创建它，不写入/覆盖任何任务文件。
- stdout和stderr都采用UTF-8。

成功日志，stdout中的前三行固定：

```text
REQUEST_VALIDATED
executionId=<本次身份>
outputDirectory=<Java词法归一化后的绝对路径文本>
```

可再追加短解释行。正常成功stderr为空，Java退出码为0。调用方不得依赖解释行的精确中文、CRLF/LF或stdout/stderr跨管道的交错顺序；不以日志字符串替代退出码判定。路径显示可归一化，不能单凭字符串大小写差异判失败。

参数/路径格式/目录可访问性等受控校验失败：stderr含以DEMO_REQUEST_INVALID:开头的一行原因，stdout不打印成功标记，Java退出码2。Java内部意外异常不伪装成2；JVM/入口/依赖加载错误可能给出其他退出码。

## 6. 方法返回值与进程退出码

Java内部run(String[] args)返回int，是Java调用栈中的方法返回值。方法返回本身不终止Java进程。
main收到此整数后，使用Java进程结束机制把它变成操作系统可读取的进程退出码。操作系统记录它，C#等待进程退出后读取Process.ExitCode。

具体Java方法体留给Java实现者；C#无需知道其实现、方法排列或内部record。

## 7. C#结果与错误分类

建议最小结果对象，字段如下（C#类型/类名自行选择）：

| 字段 | 意义 |
|---|---|
| executionId | 已创建任务的身份；创建前失败可无 |
| taskDirectory | 实际创建的绝对任务路径；创建前失败可无 |
| status | 下列分类之一 |
| javaExitCode | 仅自然观察到Java进程退出时有值；启动失败/超时分类为无 |
| stdout | 已读取的UTF-8标准输出；超时时可为空或注明不完整 |
| stderr | 已读取的UTF-8错误输出；超时时可为空或注明不完整 |
| error | 调用方配置、启动、等待或收尾错误说明 |

分类：
- validated：Java自然退出0，只表示参数校验成功。
- request_invalid：Java自然退出2。
- java_failure：Java自然退出其他值，保留原退出码和日志。
- caller_failure：配置、目录创建、启动、日志读取等调用方失败。
- timeout：Java未在演示期限内结束，保留任务身份/目录，不填造javaExitCode=2。

独立Caller自身进程退出码：validated为0；request_invalid为2；其余为1。这是调用方演示的本地约定，javaExitCode字段必须保留原值，不能把Caller自身的1误称Java返回1。

本版日志是有限短文本，可并发ReadToEndAsync；必须在等待进程结束之前启动stdout和stderr的异步读取，避免堵住管道。调用方分类依据实际退出码，日志供诊断。生产无限日志另行规定存储上限。

## 8. 演示超时与收尾默认

自然退出等待上限10秒，从Java启动后计时。超时后：
- 仅对本次调用自己持有的Process请求Kill(entireProcessTree:true)，不按名字扫杀其他java.exe。
- 最多再等待3秒确认退出；尽量收完有限日志，日志收尾也有3秒上限。
- 停止失败或收尾超时都记明事实，返回timeout，不声称已成功杀死进程。
- 观察/等待stdout与stderr任务异常，防止未观察异常；释放Process和取消源。
- 不自动重试，不删除任务目录。

自然结束也要有日志读取完成上限。调用方不把用户取消、宿主崩溃或完整生产生命周期描述为已经验证；这些不在本版实现范围。

## 9. 双方验收清单

Java方向（由Java实现者完成，C#不代写）：
J1 合法身份/中文空格绝对目录→stdout三行、退出0、目录不被写入。
J2 选项交换顺序→退出0。
J3 无参数/未知命令/缺值/重复/未知选项→退出2。
J4 非法或大写identity、相对/空/缺失目录、文件冒充目录→退出2；缺失目录仍不存在。
J5 目录含旧result.json/result.tmp→退出2；旧文件字节不变。

C#方向：
C1 两次调用任务身份/目录不同；创建完目录后才启动Java。
C2 配置和任务路径含中文/空格时逐项传参不损坏。
C3 同时读取stdout/stderr，并按0/2/其他退出码分别分类。
C4 配置/启动失败与Java返回2区分；保留上下文及目录。
C5 超时只停止自有进程、收尾有界、无自动重试/清理。
C6 实际Java class目录在独立实现阶段可以缺失；使用明确标注的子进程替身测试参数/管道/退出/超时，但不能算C#→真实Java联调。

必须逐项标注：已实跑/静态审阅/未验证。构建成功和实际联调是两项不同证据。不能用静态审阅替代编译通过。

## 10. 独立C#委派边界与交付

C#作者只依本文件，在本文件所在独立目录创建C#源码/工程文件、必要自测及验证记录，不读取原学习工程、原C#或原Java，不复制既有实现，不编写目标Java。

可验证SDK可用性；若审批拒绝立即记录并停止相关动作，若缺SDK明确记录，不静默安装或变换命令绕过。不得修改真实业务仓库、用户文件、环境变量、自动化任务或当前学习工程。

交付至少：可构建C#项目源码、使用命令、分类与失败语义、契约覆盖表、实际执行证据与未验证项。README可作为本次接口说明的配套使用材料。无构建工具时源码交付可完成，但运行验收必须保留未完成状态。

## 11. Java保留与后续教学

此前助手编写的Java参数接收器原样保留，身份是“已展示参考实现”，场景就是第1节演示；它不是本人新写的代码，也不表示真实硬件/C#联调已完成。

新的Java教学先说明当前目标、本人用中文说逻辑、助手订正和补必要语句、本人写代码编译运行。一次一个函数。代码效率与编译流程效率分别根据本人实际代码/命令/输出审查；本契约不提供新的完整Java目标实现。
