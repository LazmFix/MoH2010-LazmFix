# Reads floats/ints from any process at module-relative offsets (RVA), optionally sampled over time.
# Used to measure Call of Duty: MWR (h1_sp64_ship.exe) for comparison with MoH.
# Usage: peekx.ps1 -Process h1_sp64_ship -Rva 0x252B1D8,0x252AF08 [-Samples 40] [-IntervalMs 15]
#        [-Deref 0x252AF08]   (a pointer at that RVA; reads the float at pointer + 0x10, e.g. a dvar value)
#        [-RmbDownAt n] [-RmbUpAt n]
param([Parameter(Mandatory)][string]$Process, [string]$Rva = '', [string]$Deref = '', [int]$Samples = 1,
      [int]$IntervalMs = 15, [int]$RmbDownAt = -1, [int]$RmbUpAt = -1, [int]$DerefOffset = 0x10)
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class PX {
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int x, int y, uint d, UIntPtr e);
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, int p);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr r);
  static IntPtr h;
  public static void Open(int pid) { h = OpenProcess(0x10, false, pid); }
  public static byte[] R(long a, int n) { var b = new byte[n]; IntPtr r; ReadProcessMemory(h, new IntPtr(a), b, new IntPtr(n), out r); return b; }
  public static float F(long a) { return BitConverter.ToSingle(R(a, 4), 0); }
  public static long P(long a) { return BitConverter.ToInt64(R(a, 8), 0); }
}
'@
$p = Get-Process $Process -ErrorAction Stop | Select-Object -First 1
[PX]::Open($p.Id)
$base = $p.MainModule.BaseAddress.ToInt64()
function parse([string]$s) { if ($s -eq '') { @() } else { $s.Split(',') | ForEach-Object { [Convert]::ToInt64($_.Trim().Replace('0x', ''), 16) } } }
$direct = parse $Rva
$ptrs = parse $Deref
$sw = [Diagnostics.Stopwatch]::StartNew()
for ($i = 0; $i -lt $Samples; $i++) {
  if ($i -eq $RmbDownAt) { [PX]::mouse_event(0x0008, 0, 0, 0, [UIntPtr]::Zero); 'RMB down' }
  if ($i -eq $RmbUpAt) { [PX]::mouse_event(0x0010, 0, 0, 0, [UIntPtr]::Zero); 'RMB up' }
  $values = @($direct | ForEach-Object { '{0,9:F4}' -f [PX]::F($base + $_) })
  $values += @($ptrs | ForEach-Object { $q = [PX]::P($base + $_); '{0,9:F4}' -f [PX]::F($q + $DerefOffset) })
  '{0,5} ms {1}' -f $sw.ElapsedMilliseconds, ($values -join ' ')
  if ($i + 1 -lt $Samples) { Start-Sleep -Milliseconds $IntervalMs }
}
