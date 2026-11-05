# Experiment (-List: print the floats after the first size and how often): multiplies the font sizes of HUD text objects ({ptr -> L"$DefaultFont", 13, 13, size, size})
# whose two sizes equal -Size, in one pass. Usage: scale_text.ps1 -Size 20 -Factor 3 [-WhatIf]
param([float]$Size = 20, [float]$Factor = 3, [switch]$Single, [switch]$WhatIf, [switch]$List)
Add-Type @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Text;
public static class ScaleText {
  [StructLayout(LayoutKind.Sequential)] public struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll")] static extern int VirtualQueryEx(IntPtr h, IntPtr a, out MBI m, int len);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr r);
  [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr w);
  public static Dictionary<string,int> Seen = new Dictionary<string,int>();
  public static int Run(int pid, float size, float factor, bool dry, bool single) {
    var h = OpenProcess(0x0438, false, pid);
    var pattern = new byte[16];
    BitConverter.GetBytes(13).CopyTo(pattern, 0); BitConverter.GetBytes(13).CopyTo(pattern, 4);
    BitConverter.GetBytes(size).CopyTo(pattern, 8); BitConverter.GetBytes(size).CopyTo(pattern, 12);
    var needle = Encoding.Unicode.GetBytes("$DefaultFont\0");
    int n = 0; long addr = 0; MBI m;
    while (addr < 0x7FFF0000 && VirtualQueryEx(h, new IntPtr(addr), out m, Marshal.SizeOf(typeof(MBI))) != 0) {
      long rsize = m.Size.ToInt64();
      if (m.State == 0x1000 && m.Type == 0x20000 && (m.Protect & 0x04) != 0 && (m.Protect & 0x100) == 0 && rsize < 256L * 1024 * 1024) {
        var buf = new byte[rsize]; IntPtr got;
        if (ReadProcessMemory(h, m.Base, buf, new IntPtr(rsize), out got)) {
          for (int i = 4; i + 16 <= buf.Length; i += 4) {
            int plen = single ? 12 : 16;
            int k = 0; while (k < plen && buf[i + k] == pattern[k]) k++;
            if (k < plen) continue;
            uint p = BitConverter.ToUInt32(buf, i - 4);
            var s = new byte[needle.Length]; IntPtr r2;
            if (!ReadProcessMemory(h, new IntPtr(p), s, new IntPtr(s.Length), out r2)) continue;
            bool same = true; for (int j = 0; j < s.Length && same; j++) same = s[j] == needle[j];
            if (!same) continue;
            n++;
            { string key = BitConverter.ToSingle(buf, i + 12).ToString() + " / next " + BitConverter.ToSingle(buf, i + 16).ToString() + " " + BitConverter.ToSingle(buf, i + 20).ToString(); int c; Seen.TryGetValue(key, out c); Seen[key] = c + 1; }
            if (!dry) { var w = new byte[8]; BitConverter.GetBytes(size * factor).CopyTo(w, 0); BitConverter.GetBytes(size * factor).CopyTo(w, 4); IntPtr wr; WriteProcessMemory(h, new IntPtr(m.Base.ToInt64() + i + 8), w, new IntPtr(single ? 4 : 8), out wr); }
          }
        }
      }
      addr = m.Base.ToInt64() + rsize;
    }
    return n;
  }
}
'@
$n = [ScaleText]::Run((Get-Process moh).Id, $Size, $Factor, [bool]$WhatIf, [bool]$Single)
if ($List) { [ScaleText]::Seen.GetEnumerator() | Sort-Object Value -Descending | % { "  $($_.Value) x second size $($_.Key)" } }
"$n text object(s) with size $Size" + $(if ($WhatIf) { ' (not changed)' } else { " -> $($Size * $Factor)" })
