using System.IO;
using System.Windows;

namespace YanJi;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        // 在窗口创建前加载主题字典（读取设置或跟随系统）
        Services.ThemeManager.ApplyInitial();
        DispatcherUnhandledException += (_, args) =>
        {
            // 完整异常（含内部异常与堆栈）写入日志，便于诊断；弹窗只显示外层消息会丢失根因
            string logPath = Path.Combine(Path.GetTempPath(), "YanJi-crash.log");
            try { File.WriteAllText(logPath, args.Exception.ToString()); }
            catch { /* 日志失败不致命 */ }
            MessageBox.Show(args.Exception.Message + $"\n\n详细日志：{logPath}", "未处理的错误",
                MessageBoxButton.OK, MessageBoxImage.Error);
            args.Handled = true;
        };
    }
}
