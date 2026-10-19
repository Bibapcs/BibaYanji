package cn.bibapcs.evidence.report;

import cn.bibapcs.evidence.model.DemoResponse;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;

public final class JsonReportWriter {
    private static final ObjectMapper MAPPER = new ObjectMapper();
    private static final int MAX_RESULT_BYTES = 16 * 1024 * 1024;

    private JsonReportWriter() {}

    // 先关闭临时文件再提交最终名，CREATE_NEW 与不替换移动分别拒绝两种旧文件。
    public static void write(Path directory, DemoResponse response) throws IOException {
        byte[] json = MAPPER.writeValueAsBytes(response);
        if (json.length > MAX_RESULT_BYTES) {
            throw new IOException("结果超过 16 MiB 上限。");
        }
        Path temporary = directory.resolve("result.tmp");
        Path target = directory.resolve("result.json");
        try (FileChannel channel = FileChannel.open(temporary,
                StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE)) {
            ByteBuffer buffer = ByteBuffer.wrap(json);
            while (buffer.hasRemaining()) {
                channel.write(buffer);
            }
            channel.force(true);
        }
        Files.move(temporary, target);
    }
}
