# Memory helper for moh.exe experiments (read/write the running game's memory).
#   memtool.ps1 tables                         # addresses of MOHAHUDBase ResolutionScale_* tables
#   memtool.ps1 dump <hexaddr> [-Before 256] [-After 256]   # floats/ints around an address
#   memtool.ps1 write <hexaddr> <float>        # write one float
#   memtool.ps1 writei <hexaddr> <int>         # write one int32
#   memtool.ps1 findf <float> [<float>...]     # addresses where these floats follow each other (4-byte steps)
param([Parameter(Position = 0)][string]$Command, [Parameter(Position = 1, ValueFromRemainingArguments = $true)][string[]]$Rest,
      [int]$Before = 256, [int]$After = 256)

Add-Type @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class Mem {
  [StructLayout(LayoutKind.Sequential)] public struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll")] static extern int VirtualQueryEx(IntPtr h, IntPtr a, out MBI m, int len);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr r);
  [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr w);
  static IntPtr H;
  public static void Open(int pid) { H = OpenProcess(0x0438, false, pid); }
  public static byte[] Read(long addr, int n) { var b = new byte[n]; IntPtr r; ReadProcessMemory(H, new IntPtr(addr), b, new IntPtr(n), out r); return b; }
  public static bool WriteI(long addr, int v) { IntPtr w; return WriteProcessMemory(H, new IntPtr(addr), BitConverter.GetBytes(v), new IntPtr(4), out w); }
  public static bool WriteF(long addr, float v) { IntPtr w; return WriteProcessMemory(H, new IntPtr(addr), BitConverter.GetBytes(v), new IntPtr(4), out w); }
  static bool Near(float a, float b) { return Math.Abs(a - b) <= Math.Max(0.0002f, Math.Abs(b) * 1e-5f); }
  public static List<long> FindSeq(float[] seq) {
    var hits = new List<long>(); long addr = 0; MBI m;
    while (addr < 0x7FFF0000 && VirtualQueryEx(H, new IntPtr(addr), out m, Marshal.SizeOf(typeof(MBI))) != 0) {
      long size = m.Size.ToInt64();
      if (m.State == 0x1000 && (m.Protect & 0x04) != 0 && (m.Protect & 0x100) == 0 && size < 512L * 1024 * 1024) {
        var buf = new byte[size]; IntPtr got;
        if (ReadProcessMemory(H, m.Base, buf, new IntPtr(size), out got)) {
          for (int i = 0; i + 4 * seq.Length <= buf.Length; i += 4) {
            bool ok = true;
            for (int k = 0; k < seq.Length && ok; k++) ok = Near(BitConverter.ToSingle(buf, i + 4 * k), seq[k]);
            if (ok) hits.Add(m.Base.ToInt64() + i);
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
[Mem]::Open($p.Id)
function Hex([long]$v) { '0x{0:X8}' -f $v }
switch ($Command) {
  'tables' {
    # Layout in memory: 640x480, 800x600, 960x720, 1024x768, 1280x720, 1280x1024, 1600x1200 (or reversed).
    $a = [Mem]::FindSeq([float[]](0.66667, 0.83333, 1.0, 1.06667, 1.0, 1.42222, 1.66667))
    $b = [Mem]::FindSeq([float[]](1.66667, 1.42222, 1.0, 1.06667, 1.0, 0.83333, 0.66667))
    "ascending: " + (($a | ForEach-Object { Hex $_ }) -join ' ')
    "descending: " + (($b | ForEach-Object { Hex $_ }) -join ' ')
  }
  'findf' {
    $seq = [float[]]($Rest | ForEach-Object { [float]$_ })
    ([Mem]::FindSeq($seq) | ForEach-Object { Hex $_ }) -join ' '
  }
  'dump' {
    $at = [Convert]::ToInt64($Rest[0], 16)
    $bytes = [Mem]::Read($at - $Before, $Before + $After)
    for ($i = 0; $i -lt $bytes.Length; $i += 4) {
      $f = [BitConverter]::ToSingle($bytes, $i); $n = [BitConverter]::ToInt32($bytes, $i)
      $fs = if ([single]::IsNaN($f) -or [math]::Abs($f) -gt 1e7 -or ([math]::Abs($f) -lt 1e-4 -and $f -ne 0)) { '' } else { '{0:G6}' -f $f }
      '{0} {1,5} {2,12} {3:X8} {4}' -f (Hex ($at - $Before + $i)), ($i - $Before), $n, $n, $fs
    }
  }
  'write' {
    $at = [Convert]::ToInt64($Rest[0], 16)
    "write $(Hex $at) = $($Rest[1]): " + [Mem]::WriteF($at, [float]$Rest[1])
  }
  'writei' {
    $at = [Convert]::ToInt64($Rest[0], 16)
    "write $(Hex $at) = int $($Rest[1]): " + [Mem]::WriteI($at, [int]$Rest[1])
  }
}
