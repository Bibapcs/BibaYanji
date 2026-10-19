# 结构化结果接口｜result-demo-v1

2026-10-14。该契约落实 Java → C# 的结果文件交接，独立于原文保留的 `demo-args-v1`。当前仅允许人工数据；未来 collect、真实字段、证据定位、HTML、JAR 启动与 WPF 展示另行演进。

## 请求与文件责任

调用方：`Caller --result-demo <javaExecutable> <classDirectory> <runsRoot>`。
Windows、Java 21；三项路径为完整绝对路径。class 根包含 `cn/bibapcs/evidence/cli/ResultCommand.class`，依赖位于其 `lib/`。使用 .NET/JDK 标准接口与 Jackson 2.22.3（Apache-2.0），不使用 shell 拼接。

C# 原子创建 `runsRoot/run-<executionId>`（Windows CreateDirectoryW，已有同名项即失败），记录身份后才启动：

```text
java.exe -cp <classDirectory>;<classDirectory>/lib/* cn.bibapcs.evidence.cli.ResultCommand result-demo --execution-id <id> --out <taskDirectory>
```

Java main 收到后五项；选项可交换但各一次，身份精确匹配 `[0-9a-f]{32}`，目录须已存在、可访问、为空且自身不是符号链接。Java 不创建目录；先以 CREATE_NEW 写 `result.tmp`，关闭后同目录 `Files.move` 到 `result.json`（不使用 REPLACE_EXISTING）。拒绝旧临时或最终文件，提交失败保留现场；不自动重试、清理或读取旧任务。此策略已在 Windows 本机验证，不声称跨文件系统原子事务、断电持久性或目录权限沙箱。C# 接收时拒绝结果文件及任务目录自身的重解析点。

## 响应

UTF-8 无 BOM，严格 JSON、禁止任意层级重复键；最大 16 MiB、嵌套深度 32。根必填下列 11 键，类型严格，集合不为 null；字符串按 UTF-16 计最多 4096 单元。无害未知键允许但同样校验重复键、深度；未知版本和枚举拒绝。

| 键 | 类型 / 约束 |
| --- | --- |
| schemaVersion | JSON 整数 1，不能是字符串、布尔或小数 |
| engineVersion | 非空字符串，当前写入 `0.1.0` |
| ruleSetVersion | 精确 `demo-only-v1` |
| mode | 精确 `result-demo` |
| executionId | 与本次请求精确一致 |
| captureId | 精确 `demo-cpu-v1`，仅人工演示标记 |
| executionStatus | `completed` 或 `failed` |
| sources | 恰好一个人工来源，见下文 |
| fields | 空数组；不接受任何伪硬件字段 |
| checks | completed 恰好一个 demoOnly；failed 可为空或同样一个 |
| diagnostics | 1–128 条；必须包含 ARTIFICIAL_DEMO |

人工来源：sourceId=`demo`、kind=`synthetic`、required=false、status=`captured`（completed）或 `failed`（failed）、evidencePaths=[]。

demoOnly 规则：ruleId=`demoOnly`、status=`insufficientEvidence`、fieldIds=[]、reason 为非空解释。禁止人工演示输出“硬件通过”。

诊断：code 非空；severity 为 info/warning/error；stage 为 request/collect/parse/check/output/demo；sourceId 为 null 或 `demo`；message 非空。ARTIFICIAL_DEMO 必须为 info/demo/demo，明确人工来源；failed 至少一条 error。未知 code 可以显示，不能据 message 自动改变结论。

完成响应的共享样例为 `fixtures/result-demo-success.json`；其中固定 executionId 仅测试使用。Java 序列化类型与 C# 接收模型由该契约约束，两侧不依赖彼此内部类。

## 退出、日志与结果分类

- Java 0 + completed 且完整通过校验 → `result_ready`，文案“人工演示结果已接收，无硬件核验结论”。
- Java 2：受控请求错误，可无结果；有响应时必须 failed 且合法 → `request_invalid`。
- Java 0 无最终文件、任意坏响应、身份不一致或状态/退出码矛盾 → `protocol_failure`。保留实际自然退出码与现场。
- Java 其他退出码 → `java_failure`，不接受结果，即使文件存在；Java 写入/内部失败退出 1，stderr 保留原因。
- 配置、创建、启动、等待或管道错误 → `caller_failure`；不将失败转换成硬件结论。
- 10 秒自然等待超时 → `timeout`；外部取消令牌生效 → `cancelled`；均不接收结果，javaExitCode=null，仅停止持有的进程。停止确认和两路日志收尾各有共同 3 秒上限；不保证所有后代退出。

stdout/stderr 应用日志 UTF-8，仅诊断。每路最多保存 1 MiB 原始字节，超出后继续消费，设置对应 truncated 标记；禁止先无限读取再截断。日志可截断在 UTF-8 字符中间，尾部替换字符只影响诊断。JVM 入口前的系统编码错误仍可能乱码，当前不添加 JVM 编码属性。

Caller 自身退出码：result_ready=0、request_invalid=2，其余=1。原 demo 仍是其原有七字段输出；新结果使用可选 response、stdoutTruncated、stderrTruncated 字段。自然退出后、读文件前后都检查取消，调用 UI 还须在展示时复核自身活动任务；当前不宣称实现了切页、关闭、崩溃保护或随包运行时。
