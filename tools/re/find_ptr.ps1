# Finds dwords equal to -Value in moh.exe's private memory and dumps -Before/-After bytes around each
# as floats/ints. Usage: find_ptr.ps1 0x62F25680 [-Before 64] [-After 64]
param([Parameter(Mandatory)][string]$Value, [int]$Before = 64, [int]$After = 64, [int]$Max = 20)
Add-Type @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Text;
public static class FindP {
  [StructLayout(LayoutKind.Sequential)] public struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll")] static extern int VirtualQueryEx(IntPtr h, IntPtr a, out MBI m, int len);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr r);
  public static List<string> Run(int pid, uint v, int before, int after, int max) {
    var h = OpenProcess(0x0410, false, pid); var r = new List<string>(); long addr = 0; MBI m;
    while (addr < 0x7FFF0000 && VirtualQueryEx(h, new IntPtr(addr), out m, Marshal.SizeOf(typeof(MBI))) != 0 && r.Count < max) {
      long size = m.Size.ToInt64();
      if (m.State == 0x1000 && (m.Protect & 0x04) != 0 && (m.Protect & 0x100) == 0 && size < 512L * 1024 * 1024) {
        var buf = new byte[size]; IntPtr got;
        if (ReadProcessMemory(h, m.Base, buf, new IntPtr(size), out got)) {
          for (int i = 0; i + 4 <= buf.Length && r.Count < max; i += 4) {
            if (BitConverter.ToUInt32(buf, i) != v) continue;
            var sb = new StringBuilder(); sb.AppendFormat("0x{0:X8}:", m.Base.ToInt64() + i);
            for (int o = i - before; o < i + after; o += 4) { if (o < 0 || o + 4 > buf.Length) continue; uint u = BitConverter.ToUInt32(buf, o); float f = BitConverter.ToSingle(buf, o);
              if (o == i) sb.Append(" |"); sb.Append(' '); sb.Append(Math.Abs(f) > 1e-3 && Math.Abs(f) < 1e6 ? f.ToString("0.###") : "x" + u.ToString("X")); }
            r.Add(sb.ToString());
          }
        }
      }
      addr = m.Base.ToInt64() + size;
    }
    return r;
  }
}
'@
[FindP]::Run((Get-Process moh).Id, [Convert]::ToUInt32($Value.Replace('0x', ''), 16), $Before, $After, $Max)
