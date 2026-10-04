using System;
using System.Collections.Generic;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace YanJi.Plugin.KeyboardTest
{
    public class FocusableBorder : Border
    {
        static FocusableBorder()
        {
            FocusableProperty.OverrideMetadata(typeof(FocusableBorder), new FrameworkPropertyMetadata(true));
        }

        protected override void OnPreviewMouseDown(MouseButtonEventArgs e)
        {
            base.OnPreviewMouseDown(e);
            Focus();
        }
    }
}
