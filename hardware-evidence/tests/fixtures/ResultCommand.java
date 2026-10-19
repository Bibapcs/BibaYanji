package cn.bibapcs.evidence.cli;

// 仅测试用的故障进程：单独编译到测试输出，不进入生产 Maven 源码或产物。
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

public class ResultCommand {
    public static void main(String[] args) throws Exception {
        System.setOut(new PrintStream(System.out, true, StandardCharsets.UTF_8));
        System.setErr(new PrintStream(System.err, true, StandardCharsets.UTF_8));
        String id = args[2];
        Path directory = Path.of(args[4]);
        String scenario = directory.getParent().getFileName().toString();
        System.out.println("fixturePid=" + ProcessHandle.current().pid());
        if (scenario.equals("timeout") || scenario.equals("sentinel")) {
            Thread.sleep(30000);
            return;
        }
        if (scenario.equals("missing")) {
            return;
        }
        String classes = System.getProperty("java.class.path").split(java.io.File.pathSeparator)[0];
        String json = Files.readString(Path.of(classes).resolve("response-template.json"))
                .replace("0123456789abcdef0123456789abcdef", id);
        if (scenario.equals("wrong-identity")) {
            json = json.replace(id, "ffffffffffffffffffffffffffffffff");
        } else if (scenario.equals("bad-json")) {
            json = "{broken";
        } else if (scenario.equals("failed")) {
            json = json.replace("\"completed\"", "\"failed\"")
                    .replace("\"captured\"", "\"failed\"")
                    .replace("\"diagnostics\": [", "\"diagnostics\": [{\"code\":\"TEST_FAILURE\",\"severity\":\"error\",\"stage\":\"demo\",\"sourceId\":null,\"message\":\"controlled test failure\"},");
        }
        Files.writeString(directory.resolve("result.tmp"), json);
        Files.move(directory.resolve("result.tmp"), directory.resolve("result.json"));
        if (scenario.equals("cancel")) {
            Thread.sleep(30000);
        } else if (scenario.equals("pressure")) {
            byte[] chunk = ("中".repeat(2730) + "xx").getBytes(StandardCharsets.UTF_8);
            Thread stdout = new Thread(() -> { for (int i = 0; i < 160; i++) System.out.write(chunk, 0, chunk.length); });
            Thread stderr = new Thread(() -> { for (int i = 0; i < 160; i++) System.err.write(chunk, 0, chunk.length); });
            stdout.start();
            stderr.start();
            stdout.join();
            stderr.join();
        }
        System.exit(scenario.equals("failed") || scenario.equals("exit-mismatch") ? 2
                : scenario.equals("exit-other") ? 7 : 0);
    }
}
