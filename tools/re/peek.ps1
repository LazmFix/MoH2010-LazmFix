# Reads floats from moh.exe, optionally sampled over time.
# Usage: peek.ps1 0x7AD529CC,0x7AD529D8 [-Samples 40] [-IntervalMs 15] [-RmbDownAt 5] [-RmbUpAt 60]
#   -RmbDownAt / -RmbUpAt: press / release the right mouse button before that sample (aim in / out).
param([Parameter(Mandatory)][string]$Addresses, [int]$Samples = 1, [int]$IntervalMs = 15, [int]$RmbDownAt = -1,
      [int]$RmbUpAt = -1)
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Peek {
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int x, int y, uint d, UIntPtr e);
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, int p);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr r);
  static IntPtr h;
  public static void Open(int pid) { h = OpenProcess(0x10, false, pid); }
  public static float F(long a) { var b = new byte[4]; IntPtr r; ReadProcessMemory(h, new IntPtr(a), b, new IntPtr(4), out r); return BitConverter.ToSingle(b, 0); }
}
'@
[Peek]::Open((Get-Process moh -ErrorAction Stop).Id)
$list = $Addresses.Split(',') | ForEach-Object { [Convert]::ToInt64($_.Trim().Replace('0x', ''), 16) }
$sw = [Diagnostics.Stopwatch]::StartNew()
for ($i = 0; $i -lt $Samples; $i++) {
  if ($i -eq $RmbDownAt) { [Peek]::mouse_event(0x0008, 0, 0, 0, [UIntPtr]::Zero); 'RMB down' }
  if ($i -eq $RmbUpAt) { [Peek]::mouse_event(0x0010, 0, 0, 0, [UIntPtr]::Zero); 'RMB up' }
  $values = $list | ForEach-Object { '{0,8:F3}' -f [Peek]::F($_) }
  '{0,5} ms {1}' -f $sw.ElapsedMilliseconds, ($values -join ' ')
  if ($i + 1 -lt $Samples) { Start-Sleep -Milliseconds $IntervalMs }
}
