using System.Windows.Controls;
using YanJi.PluginSdk;

namespace YanJi.Plugin.InspectionReport;

public class InspectionReportPlugin : IYanJiPlugin
{
    public PluginInfo Info => new("report", "验机报告", "1.0.0", 90, true);
    public UserControl CreatePage(IHostContext host) => new InspectionReportPage(host);
}
