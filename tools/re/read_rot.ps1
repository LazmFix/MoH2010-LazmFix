# Prints Pitch, Yaw (int32, UE3 units, 65536 = 360 deg) at the given rotator address in moh.exe.
# Usage: read_rot.ps1 0x034FD754   (address of Pitch; Yaw follows)
param([Parameter(Mandatory)][string]$Address)
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Rot {
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr r);
  public static int[] Read(int pid, long addr) { var h = OpenProcess(0x0410, false, pid); var b = new byte[8]; IntPtr r; ReadProcessMemory(h, new IntPtr(addr), b, new IntPtr(8), out r); return new[] { BitConverter.ToInt32(b, 0), BitConverter.ToInt32(b, 4) }; }
}
'@
$v = [Rot]::Read((Get-Process moh).Id, [Convert]::ToInt64($Address.Replace('0x', ''), 16))
"pitch $($v[0]) yaw $($v[1])"
