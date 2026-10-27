# SPDX-License-Identifier: MPL-2.0
# The first visible window of a GUI process may be an IME/UAC helper.
# Bind input to the editor class in the exact child process instead.
if (-not ('BcTestWindow' -as [type])) {
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class BcTestWindow {
    private delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc callback, IntPtr l);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr h, out uint id);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetClassName(IntPtr h, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetWindowText(IntPtr h, StringBuilder text, int count);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint message, IntPtr w, IntPtr l);
    public static IntPtr Find(int process) {
        IntPtr found=IntPtr.Zero;
        EnumWindows((h,l)=>{
            uint owner; GetWindowThreadProcessId(h,out owner);
            if(owner==process && IsWindowVisible(h)) {
                var text=new StringBuilder(256); GetClassName(h,text,text.Capacity);
                if(text.ToString()=="BreathClickEditorWindow") {found=h;return false;}
            }
            return true;
        },IntPtr.Zero);
        return found;
    }
    public static string Text(IntPtr window) {
        var text=new StringBuilder(1024);GetWindowText(window,text,text.Capacity);return text.ToString();
    }
}
'@
}
function Wait-BcEditorWindow {
    param([Diagnostics.Process]$Process, [int]$TimeoutSeconds=15)
    $clock=[Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        $Process.Refresh()
        if ($Process.HasExited) { throw "Editor initialization failed: child exited ($($Process.ExitCode))" }
        $window=[BcTestWindow]::Find($Process.Id)
        if ($window -ne [IntPtr]::Zero) { return $window }
        Start-Sleep -Milliseconds 50
    }
    throw "Editor initialization failed: BreathClickEditorWindow absent in process $($Process.Id)"
}
