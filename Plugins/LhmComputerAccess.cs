using LibreHardwareMonitor.Hardware;

namespace YanJi.Hardware;

/// <summary>两个插件链接此源码；命名信号量在同进程的不同程序集间协调完整 Computer 生命周期。
/// 不放进 SDK，避免契约程序集依赖 LHM；不允许一方 Close 释放另一方正在使用的静态资源。</summary>
internal static class LhmComputerAccess
{
    const string FailureKey = "YanJi.Lhm.ComputerFailure";

    public static void Run(Computer computer, Action<Computer> read, CancellationToken token)
    {
        using var gate = new Semaphore(1, 1, $"Local\\YanJi.Lhm.Computer.{Environment.ProcessId}");
        if (WaitHandle.WaitAny([gate, token.WaitHandle]) == 1)
            token.ThrowIfCancellationRequested();
        try
        {
            token.ThrowIfCancellationRequested();
            if (AppContext.GetData(FailureKey) is string failure)
                throw new InvalidOperationException("LHM 初始化或释放曾失败，请重启程序后重试：" + failure);

            bool opened = false;
            try
            {
                try
                {
                    computer.Open();
                    opened = true;
                }
                catch (Exception ex)
                {
                    // LHM Open 中途失败时，公开 Close 不能保证回收部分初始化的静态资源。
                    AppContext.SetData(FailureKey, ex.Message);
                    throw;
                }
                token.ThrowIfCancellationRequested();
                read(computer);
            }
            finally
            {
                try { computer.Close(); }
                catch (Exception ex)
                {
                    AppContext.SetData(FailureKey, ex.Message);
                    if (opened) throw;
                }
            }
        }
        finally { gate.Release(); }
    }
}
