using System.ComponentModel;
using System.Runtime.InteropServices;

namespace ContractCaller;

// 功能：校验三个配置路径。背景：本演示运行在 Windows，不能依靠当前目录补全路径。
// 方法：拒绝非完整绝对路径，再用文件属性确认启动器、class 根目录及入口文件的类型。
public sealed record CallerConfiguration(string JavaExecutable, string ClassDirectory, string RunsRoot)
{
    public static CallerConfiguration FromArguments(string[] args, bool resultDemo = false)
    {
        if (!OperatingSystem.IsWindows())
        {
            throw new PlatformNotSupportedException("demo-args-v1 要求 Windows 完整绝对路径。");
        }

        if (args.Length != 3)
        {
            throw new ArgumentException("用法：Caller <javaExecutable> <classDirectory> <runsRoot>；必须提供三项路径。");
        }

        string java = RequireAbsolutePath(args[0], "javaExecutable");
        string classes = RequireAbsolutePath(args[1], "classDirectory");
        string runs = RequireAbsolutePath(args[2], "runsRoot");
        if (!string.Equals(Path.GetFileName(java), "java.exe", StringComparison.OrdinalIgnoreCase))
        {
            throw new ArgumentException("javaExecutable 必须指向已存在的 java.exe 文件。");
        }

        RequireEntryType(java, mustBeDirectory: false);
        RequireEntryType(classes, mustBeDirectory: true);
        string entry = resultDemo ? "cn/bibapcs/evidence/cli/ResultCommand.class" : "DemoCommand.class";
        RequireEntryType(Path.Combine(classes, entry), mustBeDirectory: false);

        try
        {
            RequireEntryType(runs, mustBeDirectory: true);
        }
        catch (FileNotFoundException)
        {
            // 允许在任务准备阶段创建缺失的 runsRoot。
        }
        catch (DirectoryNotFoundException)
        {
            // 允许同时创建缺失的父目录。
        }

        return new CallerConfiguration(java, classes, runs);
    }

    // 功能：创建本次任务目录。背景：目录既有内容不得被复用或清理。
    // 方法：原演示先检查再创建；新结果接口使用 CreateDirectoryW 原子拒绝已有同名项。
    public (string ExecutionId, string TaskDirectory) CreateTaskDirectory(bool exclusive = false)
    {
        Directory.CreateDirectory(RunsRoot);
        string executionId = Guid.NewGuid().ToString("N");
        string taskDirectory = Path.GetFullPath(Path.Combine(RunsRoot, "run-" + executionId));
        if (exclusive)
        {
            if (!CreateDirectoryW(taskDirectory, IntPtr.Zero))
            {
                throw new IOException("无法独占创建本次任务目录。", new Win32Exception(Marshal.GetLastWin32Error()));
            }
        }
        else
        {
            RequireMissingPath(taskDirectory);
            Directory.CreateDirectory(taskDirectory);
        }
        return (executionId, taskDirectory);
    }

    private static string RequireAbsolutePath(string value, string label)
    {
        if (string.IsNullOrWhiteSpace(value) || !Path.IsPathFullyQualified(value))
        {
            throw new ArgumentException($"{label} 必须是非空 Windows 完整绝对路径。");
        }

        return Path.GetFullPath(value);
    }

    private static void RequireEntryType(string path, bool mustBeDirectory)
    {
        FileAttributes attributes = File.GetAttributes(path);
        bool isDirectory = (attributes & FileAttributes.Directory) != 0;
        if (isDirectory != mustBeDirectory)
        {
            throw new IOException($"路径类型不符，要求{(mustBeDirectory ? "目录" : "文件")}：{path}");
        }
    }

    private static void RequireMissingPath(string path)
    {
        try
        {
            _ = File.GetAttributes(path);
        }
        catch (FileNotFoundException)
        {
            return;
        }
        catch (DirectoryNotFoundException)
        {
            return;
        }

        throw new IOException($"任务路径已被文件或目录占用，拒绝复用：{path}");
    }

    // Windows 原生创建失败即保留原错误，不把已有目录误认为本次新目录。
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CreateDirectoryW(string path, IntPtr securityAttributes);
}
