# Finds a UTF-16 string in moh.exe memory and prints the surrounding text (-Context chars each side).
# Usage: find_wstr.ps1 Voodoo [-Context 40] [-Max 30]
param([Parameter(Mandatory)][string]$Text, [int]$Context = 40, [int]$Max = 30, [switch]$Ascii)
Add-Type @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Text;
public static class FindW {
  [StructLayout(LayoutKind.Sequential)] public struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll")] static extern int VirtualQueryEx(IntPtr h, IntPtr a, out MBI m, int len);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr r);
  public static List<string> Run(int pid, string text, int ctx, int max, bool ascii) {
    var h = OpenProcess(0x0410, false, pid); var r = new List<string>(); long addr = 0; MBI m;
    var enc = ascii ? Encoding.ASCII : Encoding.Unicode; int cw = ascii ? 1 : 2;
    var needle = enc.GetBytes(text);
    while (addr < 0x7FFF0000 && VirtualQueryEx(h, new IntPtr(addr), out m, Marshal.SizeOf(typeof(MBI))) != 0 && r.Count < max) {
      long size = m.Size.ToInt64();
      if (m.State == 0x1000 && (m.Protect & 0x100) == 0 && (m.Protect & 0x66) != 0 && size < 512L * 1024 * 1024) {
        var buf = new byte[size]; IntPtr got;
        if (ReadProcessMemory(h, m.Base, buf, new IntPtr(size), out got)) {
          for (int i = 0; i + needle.Length <= buf.Length && r.Count < max; i += cw) {
            int k = 0; while (k < needle.Length && buf[i + k] == needle[k]) k++;
            if (k < needle.Length) continue;
            int s = Math.Max(0, i - ctx * cw), e = Math.Min(buf.Length, i + needle.Length + ctx * cw);
            s -= (s % cw); var t = enc.GetString(buf, s, (e - s) / cw * cw);
            var sb = new StringBuilder(); foreach (char c in t) sb.Append(c >= ' ' && c < 0x7f ? c : '.');
            r.Add(string.Format("0x{0:X8}: {1}", m.Base.ToInt64() + i, sb));
          }
        }
      }
      addr = m.Base.ToInt64() + size;
    }
    return r;
  }
}
'@
[FindW]::Run((Get-Process moh).Id, $Text, $Context, $Max, [bool]$Ascii)
