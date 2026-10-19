using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.Unicode;

namespace ContractCaller;

internal static class Program
{
    // 功能：独立控制台入口。背景：只演示参数交接，成功不能扩展为硬件或报告结论。
    // 方法：按三个位置参数调用，再把内存结果打印为 JSON；不会创建结果文件。
    private static async Task<int> Main(string[] args)
    {
        try
        {
            Console.OutputEncoding = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false);
            CallerResult result = args.Length > 0 && args[0] == "--result-demo"
                ? await JavaCaller.RunResultAsync(args[1..])
                : await JavaCaller.RunAsync(args);
            Console.WriteLine(result.Status switch
            {
                CallerStatus.Validated => "本次参数校验通过。",
                CallerStatus.RequestInvalid => "Java 拒绝本次参数或目录。",
                CallerStatus.JavaFailure => "Java 进程以其他非零退出码结束。",
                CallerStatus.Timeout => "等待 Java 自然结束超时；请核查收尾说明。",
                CallerStatus.ResultReady => "人工演示结果已接收，无硬件核验结论。",
                CallerStatus.ProtocolFailure => "Java 结果未通过接口校验；请核查原因。",
                CallerStatus.Cancelled => "本次任务已取消，未接收结果。",
                _ => "调用方失败；请核查配置、目录或进程错误说明。"
            });

            var options = new JsonSerializerOptions
            {
                PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
                WriteIndented = true,
                Encoder = JavaScriptEncoder.Create(UnicodeRanges.All)
            };
            options.Converters.Add(new JsonStringEnumConverter<CallerStatus>(JsonNamingPolicy.SnakeCaseLower));
            Console.WriteLine(JsonSerializer.Serialize(result, options));
            return result.CallerExitCode;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"caller_failure：控制台入口失败：{exception.GetType().Name}: {exception.Message}");
            return 1;
        }
    }
}
