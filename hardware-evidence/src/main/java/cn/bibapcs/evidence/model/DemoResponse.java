package cn.bibapcs.evidence.model;

import java.util.List;

// 人工响应的共享语义由 result-demo-v1 定义；不得表示真实硬件正常。
public record DemoResponse(
        int schemaVersion, String engineVersion, String ruleSetVersion,
        String mode, String executionId, String captureId, String executionStatus,
        List<Source> sources, List<Object> fields, List<Check> checks,
        List<Diagnostic> diagnostics) {
    public record Source(String sourceId, String kind, boolean required,
                         String status, List<String> evidencePaths) {}
    public record Check(String ruleId, String status, List<String> fieldIds,
                        String reason) {}
    public record Diagnostic(String code, String severity, String stage,
                             String sourceId, String message) {}

    public static DemoResponse completed(String executionId) {
        return new DemoResponse(1, "0.1.0", "demo-only-v1", "result-demo",
                executionId, "demo-cpu-v1", "completed",
                List.of(new Source("demo", "synthetic", false, "captured", List.of())),
                List.of(),
                List.of(new Check("demoOnly", "insufficientEvidence", List.of(),
                        "人工演示只验证结果交接，不支持硬件核验结论。")),
                List.of(new Diagnostic("ARTIFICIAL_DEMO", "info", "demo", "demo",
                        "本次数据为人工演示，没有采集硬件。")));
    }
}
