# Experiment: MOHAHUDBase objects keep Large/Medium/Small font sizes (26/20/16, 720p pixels) as three
# {FString "$DefaultFont", float size} entries in a row (16 bytes each). Multiplies them by -Factor
# (from the original 26/20/16, so it can be run again with another factor, or -Factor 1 to undo).
# Usage: hud_fonts.ps1 -Factor 3 [-WhatIf]
param([float]$Factor = 3, [switch]$WhatIf)
Add-Type @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Text;
public static class HudFonts {
  [StructLayout(LayoutKind.Sequential)] public struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll")] static extern int VirtualQueryEx(IntPtr h, IntPtr a, out MBI m, int len);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr r);
  [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr w);
  static readonly float[] Base = { 26f, 20f, 16f };
  public static int Run(int pid, float factor, bool dry) {
    var h = OpenProcess(0x0438, false, pid); int n = 0; long addr = 0; MBI m;
    while (addr < 0x7FFF0000 && VirtualQueryEx(h, new IntPtr(addr), out m, Marshal.SizeOf(typeof(MBI))) != 0) {
      long size = m.Size.ToInt64();
      if (m.State == 0x1000 && m.Type == 0x20000 && (m.Protect & 0x04) != 0 && (m.Protect & 0x100) == 0 && size < 256L * 1024 * 1024) {
        var buf = new byte[size]; IntPtr got;
        if (ReadProcessMemory(h, m.Base, buf, new IntPtr(size), out got)) {
          // entry k at o + 16k: ptr, 13, 13, size
          for (int o = 0; o + 48 <= buf.Length; o += 4) {
            bool ok = true;
            for (int k = 0; k < 3 && ok; k++) {
              int e = o + 16 * k;
              if (BitConverter.ToInt32(buf, e + 4) != 13 || BitConverter.ToInt32(buf, e + 8) != 13) ok = false;
              else { float s = BitConverter.ToSingle(buf, e + 12); float r = s / Base[k]; if (!(r >= 0.99f && r <= 8.01f)) ok = false; }
            }
            if (!ok) continue;
            // all three must have the same ratio to 26/20/16 (unchanged or changed by us)
            float r0 = BitConverter.ToSingle(buf, o + 12) / 26f, r1 = BitConverter.ToSingle(buf, o + 28) / 20f, r2 = BitConverter.ToSingle(buf, o + 44) / 16f;
            if (Math.Abs(r0 - r1) > 0.01f || Math.Abs(r0 - r2) > 0.01f) continue;
            n++;
            if (!dry) for (int k = 0; k < 3; k++) { IntPtr w; WriteProcessMemory(h, new IntPtr(m.Base.ToInt64() + o + 16 * k + 12), BitConverter.GetBytes(Base[k] * factor), new IntPtr(4), out w); }
          }
        }
      }
      addr = m.Base.ToInt64() + size;
    }
    return n;
  }
}
'@
$n = [HudFonts]::Run((Get-Process moh).Id, $Factor, [bool]$WhatIf)
"$n HUD font triple(s)" + $(if ($WhatIf) { ' (not changed)' } else { " -> x$Factor (26/20/16 -> $(26*$Factor)/$(20*$Factor)/$(16*$Factor))" })
