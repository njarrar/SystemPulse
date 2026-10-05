using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Pulse.Core.Presentation;
using Telemetry = Pulse.App.Telemetry;

namespace Pulse.App.UI;

public sealed partial class FlyoutView
{
    /// <summary>
    /// Settings (Part 1.3): two demo switches laid over real data (CPU hog,
    /// charging), live updates (pauses polling), °C / °F, and Restore ended apps
    /// (relaunches what Pulse ended). Theme and language live in the header.
    /// </summary>
    UIElement BuildSettings()
    {
        var col = new StackPanel { Spacing = Gap };
        var head = Ui.Columns("auto *", 8);
        var back = Ui.Button(c, Ui.Glyph("", 12, c.T.Brush("ink"), mirror: true), "card", "card-hover");
        back.Width = 30; back.Height = 30; back.Padding = new Thickness(0);
        AutomationProperties.SetName(back, c.L.T("back"));
        back.Click += (_, _) => Navigate(ViewMode.Overview);
        head.Children.Add(back.At(0));
        head.Children.Add(Ui.Text(c, c.L.T("aSettings"), 14, Ui.Bold).At(1));
        col.Children.Add(head);

        var secs = c.L.T("seconds", ("n", c.F.N(Telemetry.Sampler.Interval.TotalSeconds, 1)));
        var switches = new StackPanel { Spacing = 10 };
        switches.Children.Add(SwitchRow(c.L.T("simHog"), c.L.T("simHogSub", ("pct", c.F.Pct(Texts.HogThresholdPct))),
            host.Sampler.DemoHog, on => host.SetDemoHog(on)));
        switches.Children.Add(SwitchRow(c.L.T("simCharging"), c.L.T("simChargingSub", ("adapter", DemoOverlay.Adapter)),
            host.Sampler.DemoCharging, on => host.SetDemoCharging(on)));
        switches.Children.Add(SwitchRow(c.L.T("simLive"), c.L.T("simLiveSub", ("secs", secs)),
            host.Settings.LiveUpdates, on => host.SetLiveUpdates(on)));
        var note = Ui.Text(c, c.L.T("simNote"), 11, Ui.Regular, "ink3");
        note.TextWrapping = TextWrapping.Wrap;
        note.TextTrimming = TextTrimming.None;
        switches.Children.Add(note);
        col.Children.Add(Ui.Panel(c, switches, 12));

        col.Children.Add(Ui.Panel(c, BuildLanguageRow(), 12));

        // Temperature unit: °C | °F, always left to right so the symbols read correctly.
        var unitRow = Ui.Columns("* auto", 8);
        unitRow.Children.Add(Ui.Text(c, c.L.T("tempUnit"), 12.5, Ui.SemiBold).At(0));
        var seg = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 2, FlowDirection = FlowDirection.LeftToRight };
        foreach (var (label, f) in new[] { ("°C", false), ("°F", true) })
        {
            bool on = host.Settings.Fahrenheit == f;
            var b = Ui.Button(c, Ui.Value(c, label, 12, on ? Ui.Bold : Ui.SemiBold, on ? "ink" : "ink2"), on ? "seg-pill" : "track", on ? "seg-pill" : "hover", on ? "card-b" : "track", "ink", 999);
            b.Height = 26; b.MinWidth = 40;
            b.HorizontalContentAlignment = HorizontalAlignment.Center;
            AutomationProperties.SetItemStatus(b, on ? "selected" : "");
            b.Click += (_, _) => host.SetFahrenheit(f);
            seg.Children.Add(b);
        }
        unitRow.Children.Add(Ui.Pill(c, seg, c.T.Brush("track"), new Thickness(2)).At(1));
        col.Children.Add(Ui.Panel(c, unitRow, 12));

        // Restore ended apps
        int ended = host.EndedCount;
        var restoreRow = Ui.Columns("* auto", 8);
        restoreRow.Children.Add(Ui.Column(2, Ui.Text(c, c.L.T("restore"), 12.5, Ui.SemiBold),
            Ui.Text(c, c.L.Plural("ended", ended), 11, Ui.Regular, "ink2")).At(0));
        var restore = Ui.Button(c, Ui.Text(c, c.L.T("restoreBtn"), 12, Ui.SemiBold), "card", "card-hover");
        restore.Height = 28;
        restore.IsEnabled = ended > 0;
        restore.Opacity = ended > 0 ? 1 : 0.5;
        restore.Click += (_, _) =>
        {
            host.RestoreEnded();
            ShowToast(c.L.T("toastRestored"));
            Navigate(ViewMode.Settings);
        };
        restoreRow.Children.Add(restore.At(1));
        col.Children.Add(Ui.Panel(c, restoreRow, 12));
        return col;
    }

    /// <summary>
    /// Language: "Match system" plus every locale the catalog holds, each by its
    /// own name in its own script and direction. Picking one switches live and
    /// is saved; the header pill stays as a quick toggle.
    /// </summary>
    UIElement BuildLanguageRow()
    {
        var row = Ui.Columns("* auto", 8);
        var title = Ui.Text(c, c.L.T("language"), 12.5, Ui.SemiBold);
        var sub = Ui.Text(c, c.L.T("languageSub"), 11, Ui.Regular, "ink2");
        row.Children.Add(Ui.Column(2, title, sub).At(0));

        var combo = new ComboBox { MinWidth = 150, MaxWidth = 190, FontFamily = c.UiFont, FontSize = 12, VerticalAlignment = VerticalAlignment.Center };
        AutomationProperties.SetName(combo, c.L.T("language"));
        AutomationProperties.SetHelpText(combo, c.L.T("languageSub"));
        var codes = new List<string?> { null };
        combo.Items.Add(new ComboBoxItem { Content = c.L.T("langSystem"), FontFamily = c.UiFont });
        foreach (var info in host.Locales.List())
        {
            codes.Add(info.Code);
            combo.Items.Add(new ComboBoxItem
            {
                Content = info.Name,
                FontFamily = new Microsoft.UI.Xaml.Media.FontFamily(FontStack.ToXaml(host.Locales.Get(info.Code).Fonts("win").Ui, Theme.UiFont)),
                FlowDirection = info.Dir == "rtl" ? FlowDirection.RightToLeft : FlowDirection.LeftToRight,
                Language = info.Code
            });
        }
        combo.SelectedIndex = Math.Max(0, codes.IndexOf(host.Settings.Language is string saved ? host.Locales.Resolve(saved) : null));
        combo.SelectionChanged += (_, _) =>
        {
            int i = combo.SelectedIndex;
            if (i < 0 || codes[i] == host.Settings.Language) return;
            host.SetLanguage(codes[i]);
        };
        row.Children.Add(combo.At(1));
        return row;
    }

    Grid SwitchRow(string title, string sub, bool on, Action<bool> changed)
    {
        var row = Ui.Columns("* auto", 8);
        var subText = Ui.Text(c, sub, 11, Ui.Regular, "ink2");
        row.Children.Add(Ui.Column(2, Ui.Text(c, title, 12.5, Ui.SemiBold), subText).At(0));
        var toggle = new ToggleSwitch { IsOn = on, OnContent = "", OffContent = "", MinWidth = 0 };
        AutomationProperties.SetName(toggle, title);
        AutomationProperties.SetHelpText(toggle, sub);
        toggle.Toggled += (_, _) => changed(toggle.IsOn);
        row.Children.Add(toggle.At(1));
        return row;
    }
}
