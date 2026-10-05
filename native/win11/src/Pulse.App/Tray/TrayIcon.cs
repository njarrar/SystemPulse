using System.Runtime.InteropServices;
using Pulse.App.Interop;

namespace Pulse.App.Tray;

/// <summary>
/// Notification-area icon owned by a message-only window on the UI thread.
/// Uses NOTIFYICON_VERSION_4: a click or Enter raises <see cref="Selected"/>,
/// right-click or Shift+F10 raises <see cref="ContextMenu"/> with screen coordinates.
/// Re-adds itself when Explorer restarts (TaskbarCreated).
/// </summary>
public sealed unsafe class TrayIcon : IDisposable
{
    const uint CallbackMessage = Win32.WM_APP + 1;
    const uint IconId = 1;
    const string ClassName = "PulseTrayWindow";

    static TrayIcon? s_instance;
    static uint s_taskbarCreated;

    readonly nint _hwnd;
    nint _icon;
    string _tip = "Pulse";
    bool _added;

    public event Action? Selected;
    public event Action<int, int>? ContextMenu;

    public nint Hwnd => _hwnd;

    public TrayIcon()
    {
        s_instance = this;
        s_taskbarCreated = Win32.RegisterWindowMessageW("TaskbarCreated");
        nint hinst = Win32.GetModuleHandleW(null);
        fixed (char* cls = ClassName)
        {
            var wc = new WNDCLASSEXW
            {
                cbSize = (uint)sizeof(WNDCLASSEXW),
                lpfnWndProc = &WndProc,
                hInstance = hinst,
                lpszClassName = cls
            };
            Win32.RegisterClassExW(&wc);
        }
        _hwnd = Win32.CreateWindowExW(0, ClassName, "Pulse", 0, 0, 0, 0, 0, Win32.HWND_MESSAGE, 0, hinst, 0);
    }

    public void Update(nint icon, string tip)
    {
        if (icon != 0)
        {
            if (_icon != 0 && _icon != icon) Win32.DestroyIcon(_icon);
            _icon = icon;
        }
        _tip = tip;
        Apply(_added ? Win32.NIM_MODIFY : Win32.NIM_ADD);
    }

    void Apply(uint message)
    {
        var d = Data();
        d.uFlags = Win32.NIF_MESSAGE | Win32.NIF_ICON | Win32.NIF_TIP | Win32.NIF_SHOWTIP;
        d.uCallbackMessage = CallbackMessage;
        d.hIcon = _icon;
        Win32.Copy(_tip, d.szTip, 128);
        bool ok = Win32.Shell_NotifyIconW(message, &d);
        if (message == Win32.NIM_ADD && ok)
        {
            _added = true;
            d.uVersion = Win32.NOTIFYICON_VERSION_4;
            Win32.Shell_NotifyIconW(Win32.NIM_SETVERSION, &d);
        }
        else if (message == Win32.NIM_MODIFY && !ok)
        {
            _added = false; // Explorer lost it; add again next time
        }
    }

    NOTIFYICONDATAW Data() => new() { cbSize = (uint)sizeof(NOTIFYICONDATAW), hWnd = _hwnd, uID = IconId };

    /// <summary>Screen rectangle of the icon, in physical pixels.</summary>
    public RECT? Bounds()
    {
        var id = new NOTIFYICONIDENTIFIER { cbSize = (uint)sizeof(NOTIFYICONIDENTIFIER), hWnd = _hwnd, uID = IconId };
        RECT r;
        return Win32.Shell_NotifyIconGetRect(&id, &r) == 0 ? r : null;
    }

    /// <summary>Shows a native popup menu and returns the chosen id, or 0.</summary>
    public int ShowMenu(int x, int y, IEnumerable<(int Id, string? Label)> items, bool rtl)
    {
        nint menu = Win32.CreatePopupMenu();
        try
        {
            foreach (var (id, label) in items)
                if (label is null) Win32.AppendMenuW(menu, Win32.MF_SEPARATOR, 0, null);
                else Win32.AppendMenuW(menu, Win32.MF_STRING, (nuint)id, label);
            Win32.SetForegroundWindow(_hwnd); // so the menu closes when focus moves away
            uint flags = Win32.TPM_RIGHTBUTTON | Win32.TPM_RETURNCMD | Win32.TPM_BOTTOMALIGN | (rtl ? Win32.TPM_LAYOUTRTL : 0);
            return Win32.TrackPopupMenuEx(menu, flags, x, y, _hwnd, 0);
        }
        finally { Win32.DestroyMenu(menu); }
    }

    [UnmanagedCallersOnly]
    static nint WndProc(nint hwnd, uint msg, nint wParam, nint lParam)
    {
        var self = s_instance;
        if (self is not null)
        {
            if (msg == CallbackMessage)
            {
                uint ev = (uint)(lParam & 0xFFFF);
                if (ev is Win32.NIN_SELECT or Win32.NIN_KEYSELECT) self.Selected?.Invoke();
                else if (ev == Win32.WM_CONTEXTMENU)
                    self.ContextMenu?.Invoke((short)(wParam & 0xFFFF), (short)((wParam >> 16) & 0xFFFF));
                return 0;
            }
            if (msg == s_taskbarCreated && s_taskbarCreated != 0)
            {
                self._added = false;
                self.Apply(Win32.NIM_ADD);
                return 0;
            }
        }
        return Win32.DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    public void Dispose()
    {
        var d = Data();
        Win32.Shell_NotifyIconW(Win32.NIM_DELETE, &d);
        if (_icon != 0) Win32.DestroyIcon(_icon);
        if (_hwnd != 0) Win32.DestroyWindow(_hwnd);
        s_instance = null;
    }
}
