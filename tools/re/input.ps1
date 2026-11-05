# Sends input to the foreground window with SendInput (system-wide, as a real device would).
# Usage:
#   input.ps1 key <vk> [-Hold ms]          e.g. key 0x24 (Home), key 0x0D (Enter), key 0x1B (Esc)
#   input.ps1 combo <vk,vk,...>            press in order, release in reverse (e.g. 0x11,0x10,0x79 = Ctrl+Shift+F10)
#   input.ps1 move <dx> <dy> [-Steps n] [-IntervalMs ms]   relative mouse movement split into n reports
#   input.ps1 rdown | rup | ldown | lup
#   input.ps1 click <x> <y>               absolute left click at screen pixels (primary monitor)
#   input.ps1 front [-Process moh]         bring the game window to the front
param(
  [Parameter(Mandatory)][string]$Action,
  [string]$A,
  [string]$B,
  [int]$Steps = 1,
  [int]$IntervalMs = 1,
  [int]$Hold = 60,
  [string]$Process = 'moh'   # for 'front'
)
Add-Type @'
using System; using System.Runtime.InteropServices; using System.Threading;
public static class In {
  [StructLayout(LayoutKind.Sequential)] struct MI { public int dx, dy; public uint data, flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] struct KI { public ushort vk, scan; public uint flags, time; public IntPtr extra; public uint pad1, pad2; }
  [StructLayout(LayoutKind.Explicit)] struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public MI mi; [FieldOffset(8)] public KI ki; }
  [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] i, int size);
  [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
  static void Send(INPUT x) { SendInput(1, new[] { x }, Marshal.SizeOf(typeof(INPUT))); }
  public static void Mouse(int dx, int dy, uint flags) { var x = new INPUT(); x.type = 0; x.mi.dx = dx; x.mi.dy = dy; x.mi.flags = flags; Send(x); }
  public static void Key(ushort vk, bool up) {
    var x = new INPUT(); x.type = 1; x.ki.vk = vk; x.ki.scan = (ushort)MapVirtualKey(vk, 0);
    x.ki.flags = (up ? 2u : 0u) | ((vk >= 0x21 && vk <= 0x2E) ? 1u : 0u); Send(x);
  }
  public static void Move(int dx, int dy, int steps, int interval) {
    int sx = 0, sy = 0;
    for (int i = 1; i <= steps; i++) {
      int tx = (int)Math.Round((double)dx * i / steps), ty = (int)Math.Round((double)dy * i / steps);
      Mouse(tx - sx, ty - sy, 1); sx = tx; sy = ty;
      if (interval > 0) Thread.Sleep(interval);
    }
  }
}
'@
[In]::SetProcessDPIAware() | Out-Null
function vk([string]$s) { [uint16][Convert]::ToInt32($s, $(if ($s.StartsWith('0x')) { 16 } else { 10 })) }
switch ($Action) {
  'key'   { $k = vk $A; [In]::Key($k, $false); Start-Sleep -Milliseconds $Hold; [In]::Key($k, $true) }
  'combo' { $ks = $A.Split(',') | ForEach-Object { vk $_ }; foreach ($k in $ks) { [In]::Key($k, $false); Start-Sleep -Milliseconds 30 }
            Start-Sleep -Milliseconds $Hold; [array]::Reverse($ks); foreach ($k in $ks) { [In]::Key($k, $true); Start-Sleep -Milliseconds 30 } }
  'move'  { [In]::Move([int]$A, [int]$B, $Steps, $IntervalMs) }
  'rdown' { [In]::Mouse(0, 0, 0x0008) }
  'rup'   { [In]::Mouse(0, 0, 0x0010) }
  'ldown' { [In]::Mouse(0, 0, 0x0002) }
  'lup'   { [In]::Mouse(0, 0, 0x0004) }
  'click' {
    $w = [In]::GetSystemMetrics(0); $h = [In]::GetSystemMetrics(1)
    [In]::Mouse([int]([int]$A * 65535 / ($w - 1)), [int]([int]$B * 65535 / ($h - 1)), 0x8001)
    Start-Sleep -Milliseconds 30; [In]::Mouse(0, 0, 0x0002); Start-Sleep -Milliseconds $Hold; [In]::Mouse(0, 0, 0x0004)
  }
  'front' {
    Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Front {
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
'@
    $p = Get-Process $Process -ErrorAction Stop | Where-Object MainWindowHandle -ne 0 | Select-Object -First 1
    [In]::Key(0x12, $false); [In]::Key(0x12, $true)   # Alt: lets this process hand over the foreground
    [Front]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
    [Front]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
  }
  default { throw "unknown action $Action" }
}
