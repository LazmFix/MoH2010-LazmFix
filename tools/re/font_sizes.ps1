# Histogram of the float after every FString {ptr -> L"$DefaultFont", 13, 13} in moh.exe memory
# (HUD text objects keep their font size there). Usage: font_sizes.ps1 [-Font '$DefaultFont'] [-Show 20]
param([string]$Font = '$DefaultFont', [float]$Show = -1)
Add-Type @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Text;
public static class FontSizes {
  [StructLayout(LayoutKind.Sequential)] public struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll")] static extern int VirtualQueryEx(IntPtr h, IntPtr a, out MBI m, int len);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr r);
  public static Dictionary<float, int> Hist = new Dictionary<float, int>();
  public static List<string> Shown = new List<string>();
  public static void Run(int pid, string font, float show) {
    var h = OpenProcess(0x0410, false, pid);
    var needle = Encoding.Unicode.GetBytes(font + "\0"); int len = font.Length + 1;
    long addr = 0; MBI m;
    while (addr < 0x7FFF0000 && VirtualQueryEx(h, new IntPtr(addr), out m, Marshal.SizeOf(typeof(MBI))) != 0) {
      long size = m.Size.ToInt64();
      if (m.State == 0x1000 && m.Type == 0x20000 && (m.Protect & 0x04) != 0 && (m.Protect & 0x100) == 0 && size < 256L * 1024 * 1024) {
        var buf = new byte[size]; IntPtr got;
        if (ReadProcessMemory(h, m.Base, buf, new IntPtr(size), out got)) {
          for (int i = 4; i + 40 <= buf.Length; i += 4) {
            if (BitConverter.ToInt32(buf, i) != len || BitConverter.ToInt32(buf, i + 4) != len) continue;
            uint p = BitConverter.ToUInt32(buf, i - 4);
            var s = new byte[needle.Length]; IntPtr r2;
            if (p == 0 || !ReadProcessMemory(h, new IntPtr(p), s, new IntPtr(s.Length), out r2)) continue;
            bool same = true; for (int j = 0; j < s.Length && same; j++) same = s[j] == needle[j];
            if (!same) continue;
            float v = BitConverter.ToSingle(buf, i + 8);
            int c; Hist.TryGetValue(v, out c); Hist[v] = c + 1;
            if (v == show) {
              var sb = new StringBuilder();
              for (int k = -8; k < 10; k++) { int o = i + 4 * k; if (o < 0 || o + 4 > buf.Length) continue; uint u = BitConverter.ToUInt32(buf, o); float f = BitConverter.ToSingle(buf, o);
                sb.Append(Math.Abs(f) > 1e-3 && Math.Abs(f) < 1e6 ? f.ToString("0.###") : "x" + u.ToString("X")); sb.Append(' '); }
              Shown.Add(string.Format("0x{0:X8}: {1}", m.Base.ToInt64() + i - 4, sb));
            }
          }
        }
      }
      addr = m.Base.ToInt64() + size;
    }
  }
}
'@
[FontSizes]::Run((Get-Process moh).Id, $Font, $Show)
[FontSizes]::Hist.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 40 | ForEach-Object { '{0,5} x {1}' -f $_.Value, $_.Key }
[FontSizes]::Shown | Select-Object -First 40
