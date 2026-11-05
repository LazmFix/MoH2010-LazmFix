# Experiment: finds MOHAHUDBase's ResolutionScale_* table (7 consecutive floats) in moh.exe memory and
# optionally overwrites every entry. Usage: hud_scale_probe.ps1 [-Set 3.0]
param([double]$Set = 0)

Add-Type @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class Probe {
  [StructLayout(LayoutKind.Sequential)] public struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll")] static extern int VirtualQueryEx(IntPtr h, IntPtr a, out MBI m, int len);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr r);
  [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr w);
  // Table values (float32) from Default__MOHAHUDBase.
  static readonly float[] Values = { 0.66667f, 0.83333f, 1.0f, 1.06667f, 1.0f, 1.42222f, 1.66667f };
  static bool Near(float a, float b) { return Math.Abs(a - b) < 0.0002f; }
  public static List<long> Find(int pid, float setTo) {
    var hits = new List<long>();
    IntPtr h = OpenProcess(0x0438, false, pid); // QUERY_INFORMATION | VM_READ | VM_WRITE | VM_OPERATION
    long addr = 0;
    MBI m;
    while (addr < 0x7FFF0000 && VirtualQueryEx(h, new IntPtr(addr), out m, Marshal.SizeOf(typeof(MBI))) != 0) {
      long size = m.Size.ToInt64();
      bool readable = m.State == 0x1000 && (m.Protect & 0x04) != 0 && (m.Protect & 0x100) == 0; // committed, PAGE_READWRITE
      if (readable && size < 512L * 1024 * 1024) {
        var buf = new byte[size]; IntPtr got;
        if (ReadProcessMemory(h, m.Base, buf, new IntPtr(size), out got)) {
          for (int i = 0; i + 28 <= buf.Length; i += 4) {
            float f = BitConverter.ToSingle(buf, i);
            if (!Near(f, 1.42222f)) continue;
            // The 7 values in either order around it.
            for (int start = i - 24; start <= i; start += 4) {
              if (start < 0 || start + 28 > buf.Length) continue;
              var seen = new List<float>();
              for (int k = 0; k < 7; k++) seen.Add(BitConverter.ToSingle(buf, start + 4 * k));
              int matched = 0; var pool = new List<float>(Values);
              foreach (var s in seen) { int j = pool.FindIndex(v => Near(v, s)); if (j >= 0) { pool.RemoveAt(j); matched++; } }
              if (matched == 7) {
                long at = m.Base.ToInt64() + start; hits.Add(at);
                if (setTo > 0) { var w = new byte[28]; for (int k = 0; k < 7; k++) BitConverter.GetBytes(setTo).CopyTo(w, 4 * k); IntPtr wr; WriteProcessMemory(h, new IntPtr(at), w, new IntPtr(28), out wr); }
                break;
              }
            }
          }
        }
      }
      addr = m.Base.ToInt64() + size;
    }
    return hits;
  }
}
'@
$p = Get-Process moh -ErrorAction Stop
$hits = [Probe]::Find($p.Id, [float]$Set)
"found $($hits.Count) table(s): " + (($hits | ForEach-Object { '0x{0:X8}' -f $_ }) -join ', ')
if ($Set -gt 0) { "set every entry to $Set" }
