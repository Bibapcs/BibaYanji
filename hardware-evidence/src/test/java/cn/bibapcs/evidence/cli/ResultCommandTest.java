package cn.bibapcs.evidence.cli;

import cn.bibapcs.evidence.model.DemoResponse;
import cn.bibapcs.evidence.report.JsonReportWriter;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.io.TempDir;
import static org.junit.jupiter.api.Assertions.*;

class ResultCommandTest {
    private static final String ID = "0123456789abcdef0123456789abcdef";
    @TempDir Path root;
    private final PrintStream originalOut = System.out;
    private final PrintStream originalErr = System.err;
    private final ByteArrayOutputStream stdout = new ByteArrayOutputStream();
    private final ByteArrayOutputStream stderr = new ByteArrayOutputStream();

    @BeforeEach void captureUtf8Logs() {
        System.setOut(new PrintStream(stdout, true, StandardCharsets.UTF_8));
        System.setErr(new PrintStream(stderr, true, StandardCharsets.UTF_8));
    }

    @AfterEach void restoreLogs() {
        System.setOut(originalOut);
        System.setErr(originalErr);
    }

    private String[] request(Path directory) {
        return new String[] {"result-demo", "--execution-id", ID, "--out", directory.toString()};
    }

    @Test void realResponseMatchesSharedContractAndHasNoBomOrTemporaryFile() throws Exception {
        Path directory = Files.createDirectory(root.resolve("中文 空格目录"));
        assertEquals(0, ResultCommand.run(request(directory)));
        assertEquals("", stderr.toString(StandardCharsets.UTF_8));
        assertTrue(stdout.toString(StandardCharsets.UTF_8).contains("人工演示"));
        byte[] bytes = Files.readAllBytes(directory.resolve("result.json"));
        assertEquals('{', bytes[0]);
        assertTrue(new String(bytes, StandardCharsets.UTF_8).contains("人工演示"));
        ObjectMapper mapper = new ObjectMapper();
        JsonNode expected = mapper.readTree(Path.of("contracts/fixtures/result-demo-success.json").toFile());
        assertEquals(expected, mapper.readTree(bytes));
        try (var contents = Files.list(directory)) {
            assertEquals(List.of("result.json"), contents.map(path -> path.getFileName().toString()).toList());
        }
    }

    @Test void swappedOptionsAreAccepted() {
        assertEquals(0, ResultCommand.run(new String[] {"result-demo", "--out", root.toString(), "--execution-id", ID}));
    }

    @Test void invalidRequestsDoNotCreateOutput() throws Exception {
        List<String[]> cases = List.of(
                new String[0],
                new String[] {"demo", "--execution-id", ID, "--out", root.toString()},
                new String[] {"result-demo", "--execution-id", ID, "--out"},
                new String[] {"result-demo", "--execution-id", ID, "--out", root.toString(), "extra"},
                new String[] {"result-demo", "--execution-id", ID, "--execution-id", ID},
                new String[] {"result-demo", "--out", root.toString(), "--out", root.toString()},
                new String[] {"result-demo", "--unknown", ID, "--out", root.toString()},
                new String[] {"result-demo", "--execution-id", ID.toUpperCase(), "--out", root.toString()},
                new String[] {"result-demo", "--execution-id", "bad", "--out", root.toString()},
                new String[] {"result-demo", "--execution-id", ID, "--out", ""},
                new String[] {"result-demo", "--execution-id", ID, "--out", "relative"},
                request(root.resolve("missing")));
        for (String[] args : cases) {
            assertEquals(2, ResultCommand.run(args));
        }
        assertFalse(Files.exists(root.resolve("missing")));
        assertFalse(Files.exists(root.resolve("result.json")));
        assertFalse(Files.exists(root.resolve("result.tmp")));
        assertEquals("", stdout.toString(StandardCharsets.UTF_8));
        assertTrue(stderr.toString(StandardCharsets.UTF_8).startsWith("RESULT_REQUEST_INVALID:"));
    }

    @Test void oldFilesAndOrdinaryContentsAreRejectedWithoutModification() throws Exception {
        for (String name : List.of("result.json", "result.tmp", "unrelated.txt")) {
            Path directory = Files.createDirectory(root.resolve(name));
            Path file = directory.resolve(name);
            Files.writeString(file, "original bytes");
            assertEquals(2, ResultCommand.run(request(directory)));
            assertEquals("original bytes", Files.readString(file));
            try (var contents = Files.list(directory)) {
                assertEquals(1, contents.count());
            }
        }
    }

    @Test void fileIsNotTreatedAsDirectory() throws Exception {
        Path file = Files.writeString(root.resolve("file"), "keep");
        assertEquals(2, ResultCommand.run(request(file)));
        assertEquals("keep", Files.readString(file));
    }

    @Test void writerRefusesExistingTemporaryFile() throws Exception {
        Files.writeString(root.resolve("result.tmp"), "original temporary");
        assertThrows(java.io.IOException.class, () -> JsonReportWriter.write(root, DemoResponse.completed(ID)));
        assertEquals("original temporary", Files.readString(root.resolve("result.tmp")));
        assertFalse(Files.exists(root.resolve("result.json")));
    }

    @Test void writerRefusesFinalFileEvenAfterRequestValidation() throws Exception {
        Files.writeString(root.resolve("result.json"), "original final");
        assertThrows(java.io.IOException.class, () -> JsonReportWriter.write(root, DemoResponse.completed(ID)));
        assertEquals("original final", Files.readString(root.resolve("result.json")));
        assertTrue(Files.exists(root.resolve("result.tmp")), "提交失败保留现场，不能偷偷清理重试");
    }

    @Test void writerEscapesExternalTextUsingJackson() throws Exception {
        DemoResponse good = DemoResponse.completed(ID);
        String text = "引号\"、反斜杠\\、换行\n、emoji \uD83D\uDE00";
        DemoResponse response = new DemoResponse(good.schemaVersion(), text, good.ruleSetVersion(),
                good.mode(), good.executionId(), good.captureId(), good.executionStatus(),
                good.sources(), good.fields(), good.checks(), good.diagnostics());
        JsonReportWriter.write(root, response);
        assertEquals(text, new ObjectMapper().readTree(root.resolve("result.json").toFile()).get("engineVersion").asText());
    }
}
