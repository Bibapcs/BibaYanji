using System.Windows.Input;

namespace YanJi.Plugin.KeyboardTest;

/// <summary>键盘测试页的一个键位：显示标签、对应 WPF 键值、位置与尺寸
/// （u 单位，1u = 标准键宽；X/Y 为左上角坐标，W/H 默认 1u）。</summary>
public record KeyCap(string Label, Key Key, double X, double Y, double W = 1, double H = 1);
