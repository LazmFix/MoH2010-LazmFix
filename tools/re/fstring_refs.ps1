# Finds UE3 FString fields (pointer, length, capacity) whose text is the given string, and prints the
# 6 dwords after each (as int and float): config/localized values usually follow their font name.
# Usage: fstring_refs.ps1 '$DefaultFont'
param([Parameter(Mandatory)][string]$Text)

Add-Type @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Text;
public static class FRefs {
  [StructLayout(LayoutKind.Sequential)] public struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll")] static extern int VirtualQueryEx(IntPtr h, IntPtr a, out MBI m, int len);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr r);
  public static List<string> Run(int pid, string text) {
    var h = OpenProcess(0x0410, false, pid);
    var regions = new List<KeyValuePair<long, byte[]>>();
    long addr = 0; MBI m;
    while (addr < 0x7FFF0000 && VirtualQueryEx(h, new IntPtr(addr), out m, Marshal.SizeOf(typeof(MBI))) != 0) {
      long size = m.Size.ToInt64();
      if (m.State == 0x1000 && (m.Protect & 0x04) != 0 && (m.Protect & 0x100) == 0 && size < 512L * 1024 * 1024) {
        var buf = new byte[size]; IntPtr got;
        if (ReadProcessMemory(h, m.Base, buf, new IntPtr(size), out got)) regions.Add(new KeyValuePair<long, byte[]>(m.Base.ToInt64(), buf));
      }
      addr = m.Base.ToInt64() + size;
    }
    var needle = Encoding.Unicode.GetBytes(text + "\0");
    var strings = new HashSet<uint>();
    foreach (var r in regions) {
      var b = r.Value;
      for (int i = 0; i + needle.Length <= b.Length; i += 2) {
        int k = 0; while (k < needle.Length && b[i + k] == needle[k]) k++;
        if (k == needle.Length) strings.Add((uint)(r.Key + i));
      }
    }
    var lines = new List<string>();
    lines.Add("strings: " + strings.Count);
    int len = text.Length + 1;
    foreach (var r in regions) {
      var b = r.Value;
      for (int i = 0; i + 36 <= b.Length; i += 4) {
        uint p = BitConverter.ToUInt32(b, i);
        if (!strings.Contains(p)) continue;
        if (BitConverter.ToInt32(b, i + 4) != len) continue;
        var sb = new StringBuilder();
        sb.AppendFormat("0x{0:X8}:", r.Key + i);
        for (int k = 0; k < 6; k++) {
          int o = i + 12 + 4 * k; if (o + 4 > b.Length) break;
          sb.AppendFormat("  {0}/{1:G5}", BitConverter.ToInt32(b, o), BitConverter.ToSingle(b, o));
        }
        lines.Add(sb.ToString());
      }
    }
    return lines;
  }
}
'@
$p = Get-Process moh -ErrorAction Stop
[FRefs]::Run($p.Id, $Text)
