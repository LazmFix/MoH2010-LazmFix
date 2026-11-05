# Finds the camera yaw in moh.exe memory: three snapshots of all private read-write memory, taken when
# the signal files <dir>\snap1, snap2 appear (inject the same number of counts between them, e.g.
# Ctrl+Shift+F10 in game). Prints int32 addresses that changed by the same non-zero delta both times
# (plus the 16-bit-wrapped variant: UE3 rotators are 65536 per turn).
# Usage: yaw_finder.ps1 -Dir <folder>   (snapshot 0 is taken at once)
param([Parameter(Mandatory)][string]$Dir, [int]$TimeoutSec = 600, [int]$MinDelta = 1, [int]$Tolerance = 0)

Add-Type @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class Snap {
  public static int MinDelta = 1, Tolerance = 0;
  [StructLayout(LayoutKind.Sequential)] public struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll")] static extern int VirtualQueryEx(IntPtr h, IntPtr a, out MBI m, int len);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr r);
  public static Dictionary<long, byte[]> Take(int pid) {
    var h = OpenProcess(0x0410, false, pid);
    var map = new Dictionary<long, byte[]>(); long addr = 0; MBI m;
    while (addr < 0x7FFF0000 && VirtualQueryEx(h, new IntPtr(addr), out m, Marshal.SizeOf(typeof(MBI))) != 0) {
      long size = m.Size.ToInt64();
      if (m.State == 0x1000 && m.Type == 0x20000 && (m.Protect & 0x04) != 0 && (m.Protect & 0x100) == 0 && size < 256L * 1024 * 1024) {
        var buf = new byte[size]; IntPtr got;
        if (ReadProcessMemory(h, m.Base, buf, new IntPtr(size), out got)) map[m.Base.ToInt64()] = buf;
      }
      addr = m.Base.ToInt64() + size;
    }
    return map;
  }
  public static List<string> Compare(Dictionary<long, byte[]> a, Dictionary<long, byte[]> b, Dictionary<long, byte[]> c) {
    var r = new List<string>();
    foreach (var kv in a) {
      byte[] x = kv.Value, y, z;
      if (!b.TryGetValue(kv.Key, out y) || !c.TryGetValue(kv.Key, out z) || y.Length != x.Length || z.Length != x.Length) continue;
      for (int i = 0; i + 4 <= x.Length; i += 4) {
        int v0 = BitConverter.ToInt32(x, i), v1 = BitConverter.ToInt32(y, i), v2 = BitConverter.ToInt32(z, i);
        long d1 = (long)v1 - v0, d2 = (long)v2 - v1;
        int w1 = (short)(ushort)((v1 & 0xFFFF) - (v0 & 0xFFFF)), w2 = (short)(ushort)((v2 & 0xFFFF) - (v1 & 0xFFFF));
        bool same = d1 != 0 && Math.Abs(d1 - d2) <= Tolerance && Math.Abs(d1) >= MinDelta && Math.Abs(d1) < 65536;
        bool wrapped = !same && w1 != 0 && Math.Abs(w1 - w2) <= Tolerance && Math.Abs(w1) >= MinDelta && Math.Abs(d1) < (1L << 20);
        if (same || wrapped) r.Add(string.Format("0x{0:X8}  {1} -> {2} -> {3}  delta {4}{5}", kv.Key + i, v0, v1, v2, same ? d1 : w1, same ? (d1 == d2 ? "" : " then " + d2) : " (16-bit)"));
        if (r.Count > 400) return r;
      }
    }
    return r;
  }
}
'@
$p = Get-Process moh -ErrorAction Stop
[Snap]::MinDelta = $MinDelta; [Snap]::Tolerance = $Tolerance
New-Item -ItemType Directory -Force -Path $Dir | Out-Null
Remove-Item (Join-Path $Dir 'snap*'), (Join-Path $Dir 'ready*'), (Join-Path $Dir 'result.txt') -ErrorAction SilentlyContinue
$s0 = [Snap]::Take($p.Id); Set-Content (Join-Path $Dir 'ready0') 'ok'
$deadline = (Get-Date).AddSeconds($TimeoutSec)
while (-not (Test-Path (Join-Path $Dir 'snap1')) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
$s1 = [Snap]::Take($p.Id); Set-Content (Join-Path $Dir 'ready1') 'ok'
while (-not (Test-Path (Join-Path $Dir 'snap2')) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
$s2 = [Snap]::Take($p.Id)
$r = [Snap]::Compare($s0, $s1, $s2)
$r | Set-Content (Join-Path $Dir 'result.txt')
"candidates: $($r.Count)"
$r | Select-Object -First 60
