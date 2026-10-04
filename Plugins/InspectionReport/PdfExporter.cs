using System.IO;
using System.Printing;
using System.Runtime.InteropServices;
using System.Windows.Documents;
using System.Windows.Xps.Packaging;

namespace YanJi.Plugin.InspectionReport;

/// <summary>WPF → XPS → 系统 Microsoft Print to PDF，保留文字和矢量；无额外 PDF 库。
/// https://learn.microsoft.com/windows/win32/api/xpsprint/nf-xpsprint-startxpsprintjob</summary>
internal static class PdfExporter
{
    public static async Task ExportAsync(DocumentPaginator document, string path)
    {
        var (printer, ticket) = FindPrinter();
        string xpsPath = Path.Combine(Path.GetTempPath(), $"YanJi-report-{Guid.NewGuid():N}.xps");
        string pdfPath = Path.Combine(Path.GetDirectoryName(path)!, $".YanJi-{Guid.NewGuid():N}.pdf");
        try
        {
            using (var xps = new XpsDocument(xpsPath, FileAccess.ReadWrite))
                XpsDocument.CreateXpsDocumentWriter(xps).Write(document);
            await Task.Run(() => PrintXps(xpsPath, pdfPath, printer, ticket)).ConfigureAwait(false);
            using (var file = File.OpenRead(pdfPath))
            {
                var header = new byte[5];
                if (file.Read(header) != 5 || System.Text.Encoding.ASCII.GetString(header) != "%PDF-")
                    throw new IOException("系统打印未生成有效 PDF，请检查打印服务。 ");
            }
            // 只有成功生成后才替换目标文件，失败不会破坏已有报告。
            File.Move(pdfPath, path, overwrite: true);
        }
        finally
        {
            TryDelete(xpsPath);
            TryDelete(pdfPath);
        }
    }

    static (string Name, byte[] Ticket) FindPrinter()
    {
        using var server = new LocalPrintServer();
        using var queues = server.GetPrintQueues([EnumeratedPrintQueueTypes.Local]);
        foreach (var queue in queues)
        {
            using (queue)
            {
                if (!queue.QueueDriver.Name.Equals("Microsoft Print To PDF", StringComparison.OrdinalIgnoreCase)) continue;
                var requested = new PrintTicket
                {
                    PageMediaSize = new PageMediaSize(PageMediaSizeName.ISOA4), PageOrientation = PageOrientation.Portrait
                };
                var validated = queue.MergeAndValidatePrintTicket(queue.DefaultPrintTicket, requested).ValidatedPrintTicket;
                using var stream = validated.GetXmlStream();
                return (queue.FullName, stream.ToArray());
            }
        }
        throw new InvalidOperationException("未找到 Microsoft Print to PDF。请在 Windows「启用或关闭 Windows 功能」中启用它，并确认 Print Spooler 服务正在运行。");
    }

    static void PrintXps(string xpsPath, string pdfPath, string printer, byte[] ticket)
    {
        using var completed = new EventWaitHandle(false, EventResetMode.ManualReset);
        IXpsPrintJob? job = null;
        IXpsPrintJobStream? documentStream = null, ticketStream = null;
        bool documentClosed = false, ticketClosed = false;
        try
        {
            Marshal.ThrowExceptionForHR(StartXpsPrintJob(printer, "笔吧验机报告", pdfPath, IntPtr.Zero,
                completed.SafeWaitHandle.DangerousGetHandle(), IntPtr.Zero, 0, out job, out documentStream, out ticketStream));
            Write(ticketStream!, ticket);
            ticketStream!.Close();
            ticketClosed = true;
            using (var input = File.OpenRead(xpsPath))
            {
                var buffer = new byte[65536];
                int read;
                while ((read = input.Read(buffer)) > 0) Write(documentStream!, buffer, read);
            }
            documentStream!.Close();
            documentClosed = true;
            if (!completed.WaitOne(TimeSpan.FromSeconds(60)))
                throw new TimeoutException("PDF 打印超过 60 秒，请检查打印队列后重试。");
            job!.GetJobStatus(out var status);
            if (status.Completion != 1)
            {
                Marshal.ThrowExceptionForHR(status.ErrorCode);
                throw new IOException($"PDF 打印被取消或未完成（状态 {status.Completion}，错误 0x{status.ErrorCode:X8}）。");
            }
        }
        catch
        {
            try
            {
                job?.Cancel();
                if (job != null) completed.WaitOne(TimeSpan.FromSeconds(5));
            }
            catch { /* 保留原始错误 */ }
            throw;
        }
        finally
        {
            if (!documentClosed) { try { documentStream?.Close(); } catch { } }
            if (!ticketClosed) { try { ticketStream?.Close(); } catch { } }
            if (documentStream != null) Marshal.ReleaseComObject(documentStream);
            if (ticketStream != null) Marshal.ReleaseComObject(ticketStream);
            if (job != null) Marshal.ReleaseComObject(job);
        }
    }

    static void Write(IXpsPrintJobStream stream, byte[] bytes, int? count = null)
    {
        uint expected = (uint)(count ?? bytes.Length);
        stream.Write(bytes, expected, out uint written);
        if (written != expected) throw new IOException("打印数据写入不完整。");
    }

    static void TryDelete(string path)
    {
        try { File.Delete(path); } catch { /* 清理失败不覆盖导出结果 */ }
    }

    [DllImport("XpsPrint.dll", CharSet = CharSet.Unicode)]
    static extern int StartXpsPrintJob(string printerName, string jobName, string outputFileName,
        IntPtr progressEvent, IntPtr completionEvent, IntPtr printablePagesOn, uint printablePagesOnCount,
        out IXpsPrintJob job, out IXpsPrintJobStream documentStream, out IXpsPrintJobStream printTicketStream);

    [ComImport, Guid("5AB89B06-8194-425F-AB3B-D7A96E350161"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IXpsPrintJob
    {
        void Cancel();
        void GetJobStatus(out JobStatus status);
    }

    [StructLayout(LayoutKind.Sequential)]
    struct JobStatus
    {
        public uint JobId;
        public int CurrentDocument, CurrentPage, CurrentPageTotal;
        public int Completion, ErrorCode;
    }

    [ComImport, Guid("7A77DC5F-45D6-4DFF-9307-D8CB846347CA"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IXpsPrintJobStream
    {
        void Read(IntPtr buffer, uint count, out uint read);
        void Write([MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 1)] byte[] buffer, uint count, out uint written);
        void Close();
    }
}
