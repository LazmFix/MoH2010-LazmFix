# Lists addresses in moh.exe holding the float -Value (default 70 = MOHAPlayerCamera.DefaultFOV) with the
# 32 bytes after it, to tell the camera objects (DefaultAspectRatio 1.3333 follows DefaultFOV) from noise.
# Usage: fov_scan.ps1 [-Value 70] [-Near 1.3333334] [-Window 12]  (Window = dwords searched after the value)
param([float]$Value = 70, [float]$Near = 1.3333334, [int]$Window = 12, [switch]$All)
Add-Type @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class FovScan {
  [StructLayout(LayoutKind.Sequential)] public struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll")] static extern int VirtualQueryEx(IntPtr h, IntPtr a, out MBI m, int len);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr r);
  public static List<string> Run(int pid, float value, float near, int window, bool all, out int total) {
    var h = OpenProcess(0x0410, false, pid); var r = new List<string>(); long addr = 0; MBI m; total = 0;
    while (addr < 0x7FFF0000 && VirtualQueryEx(h, new IntPtr(addr), out m, Marshal.SizeOf(typeof(MBI))) != 0) {
      long size = m.Size.ToInt64();
      if (m.State == 0x1000 && (m.Protect & 0x04) != 0 && (m.Protect & 0x100) == 0 && size < 512L * 1024 * 1024) {
        var buf = new byte[size]; IntPtr got;
        if (ReadProcessMemory(h, m.Base, buf, new IntPtr(size), out got)) {
          for (int i = 0; i + 4 * (window + 1) <= buf.Length; i += 4) {
            if (BitConverter.ToSingle(buf, i) != value) continue;
            total++;
            int at = -1;
            for (int k = 1; k <= window; k++) if (Math.Abs(BitConverter.ToSingle(buf, i + 4 * k) - near) < 1e-4f) { at = k; break; }
            if (at < 0 && !all) continue;
            var sb = new System.Text.StringBuilder();
            for (int k = -2; k <= window; k++) { uint u = BitConverter.ToUInt32(buf, i + 4 * k); float f = BitConverter.ToSingle(buf, i + 4 * k);
              sb.Append(Math.Abs(f) > 1e-3 && Math.Abs(f) < 1e6 ? f.ToString("0.###") : "x" + u.ToString("X")); sb.Append(' '); }
            r.Add(string.Format("0x{0:X8} type {1:X} aspect@+{2}  {3}", m.Base.ToInt64() + i, m.Type, at * 4, sb));
          }
        }
      }
      addr = m.Base.ToInt64() + size;
    }
    return r;
  }
}
'@
$total = 0
$r = [FovScan]::Run((Get-Process moh).Id, $Value, $Near, $Window, [bool]$All, [ref]$total)
"$total copies of $Value, $($r.Count) listed"
$r | Select-Object -First 80
