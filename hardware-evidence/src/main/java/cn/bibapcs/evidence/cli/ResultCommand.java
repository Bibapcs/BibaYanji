package cn.bibapcs.evidence.cli;

import cn.bibapcs.evidence.model.DemoResponse;
import cn.bibapcs.evidence.report.JsonReportWriter;
import java.io.IOException;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.LinkOption;
import java.nio.file.Path;

public final class ResultCommand {
    private record Request(String executionId, Path directory) {}

    private ResultCommand() {}

    public static void main(String[] args) {
        System.setOut(new PrintStream(System.out, true, StandardCharsets.UTF_8));
        System.setErr(new PrintStream(System.err, true, StandardCharsets.UTF_8));
        System.exit(run(args));
    }

    static int run(String[] args) {
        Request request;
        try {
            request = parse(args);
        } catch (InvalidPathException exception) {
            System.err.println("RESULT_REQUEST_INVALID: 路径格式无效。");
            return 2;
        } catch (IllegalArgumentException | SecurityException | IOException exception) {
            System.err.println("RESULT_REQUEST_INVALID: " + exception.getMessage());
            return 2;
        }
        try {
            JsonReportWriter.write(request.directory(), DemoResponse.completed(request.executionId()));
            System.out.println("人工演示结果已提交，无硬件核验结论。");
            return 0;
        } catch (IOException | SecurityException exception) {
            System.err.println("RESULT_WRITE_FAILED: " + exception.getMessage());
            return 1;
        }
    }

    private static Request parse(String[] args) throws IOException {
        if (args.length != 5 || !args[0].equals("result-demo")) {
            throw new IllegalArgumentException("用法：result-demo --execution-id <身份> --out <目录>");
        }
        String executionId = null;
        String directoryText = null;
        for (int index = 1; index < args.length; index += 2) {
            if (args[index].equals("--execution-id") && executionId == null) {
                executionId = args[index + 1];
            } else if (args[index].equals("--out") && directoryText == null) {
                directoryText = args[index + 1];
            } else {
                throw new IllegalArgumentException("未知或重复参数：" + args[index]);
            }
        }
        if (executionId == null || !executionId.matches("[0-9a-f]{32}")) {
            throw new IllegalArgumentException("executionId 必须是 32 位小写十六进制文本。");
        }
        if (directoryText == null || directoryText.isBlank()) {
            throw new IllegalArgumentException("必须提供任务目录。");
        }
        Path directory = Path.of(directoryText).normalize();
        if (!directory.isAbsolute() || !Files.isDirectory(directory, LinkOption.NOFOLLOW_LINKS)
                || Files.isSymbolicLink(directory)) {
            throw new IllegalArgumentException("任务目录必须是已存在的绝对目录，且不是符号链接。");
        }
        try (DirectoryStream<Path> contents = Files.newDirectoryStream(directory)) {
            if (contents.iterator().hasNext()) {
                throw new IllegalArgumentException("任务目录非空，拒绝复用。");
            }
        }
        return new Request(executionId, directory);
    }
}
