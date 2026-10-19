import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;

public class DemoCommand {

    // 保存校验后的任务身份和目录。
    // record 自动提供 executionId()、outputDirectory() 等方法。
    private record DemoRequest(
            String executionId,
            Path outputDirectory
    ) {}

    // 程序入口：准备日志编码，调用 run，设置进程退出码。
    public static void main(String[] args) {
        System.setOut(
                new PrintStream(System.out, true, StandardCharsets.UTF_8)
        );
        System.setErr(
                new PrintStream(System.err, true, StandardCharsets.UTF_8)
        );

        int exitCode = run(args);
        System.exit(exitCode);
    }

    // 组织校验：成功返回 0，预期输入错误返回 2。
    private static int run(String[] args) {
        try {
            DemoRequest request = parseArguments(args);

            System.out.println("REQUEST_VALIDATED");
            System.out.println("executionId=" + request.executionId());
            System.out.println(
                    "outputDirectory=" + request.outputDirectory()
            );

            return 0;
        } catch (InvalidPathException ex) {
            System.err.println("DEMO_REQUEST_INVALID: 路径格式无效。");
            return 2;
        } catch (IllegalArgumentException ex) {
            System.err.println("DEMO_REQUEST_INVALID: " + ex.getMessage());
            return 2;
        } catch (SecurityException ex) {
            System.err.println(
                    "DEMO_REQUEST_INVALID: 没有访问任务目录的权限。"
            );
            return 2;
        }
    }

    // 解析并检查参数：成功返回请求，失败抛出异常。
    private static DemoRequest parseArguments(String[] args) {
        // || 是短路运算：数量不对时，不再访问 args[0]。
        if (args.length != 5 || !args[0].equals("demo")) {
            throw new IllegalArgumentException(
                    "用法：demo --execution-id <身份> --out <任务目录>"
            );
        }

        String executionId = null;
        String outputDirectoryText = null;

        // 从第 1 项开始，每次读取一组选项和对应的值。
        for (int index = 1; index < args.length; index += 2) {
            String option = args[index];
            String value = args[index + 1];

            if (option.equals("--execution-id") && executionId == null) {
                executionId = value;
            } else if (option.equals("--out")
                    && outputDirectoryText == null) {
                outputDirectoryText = value;
            } else {
                throw new IllegalArgumentException(
                        "未知或重复参数：" + option
                );
            }
        }

        if (executionId == null
                || !executionId.matches("[0-9a-f]{32}")) {
            throw new IllegalArgumentException(
                    "executionId 必须是32位小写十六进制文本。"
            );
        }

        if (outputDirectoryText == null
                || outputDirectoryText.isBlank()) {
            throw new IllegalArgumentException("必须提供任务目录。");
        }

        Path outputDirectory = Path.of(outputDirectoryText);

        if (!outputDirectory.isAbsolute()) {
            throw new IllegalArgumentException(
                    "任务目录必须是绝对路径。"
            );
        }

        outputDirectory = outputDirectory.normalize();

        if (!Files.isDirectory(outputDirectory)) {
            throw new IllegalArgumentException(
                    "任务目录必须已经存在且可作为目录访问。"
            );
        }

        if (Files.exists(outputDirectory.resolve("result.json"))
                || Files.exists(outputDirectory.resolve("result.tmp"))) {
            throw new IllegalArgumentException(
                    "任务目录已有结果文件，拒绝复用。"
            );
        }

        return new DemoRequest(executionId, outputDirectory);
    }
}