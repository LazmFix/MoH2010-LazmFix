# Writes Pitch, Yaw (int32, UE3 units) at a rotator address in moh.exe. Usage: write_rot.ps1 0x034FD754 0 3692
param([Parameter(Mandatory)][string]$Address, [int]$Pitch, [int]$Yaw)
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class RotW {
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr w);
  public static bool Write(int pid, long addr, int pitch, int yaw) { var h = OpenProcess(0x0438, false, pid); var b = new byte[8]; BitConverter.GetBytes(pitch).CopyTo(b, 0); BitConverter.GetBytes(yaw).CopyTo(b, 4); IntPtr w; return WriteProcessMemory(h, new IntPtr(addr), b, new IntPtr(8), out w); }
}
'@
[RotW]::Write((Get-Process moh).Id, [Convert]::ToInt64($Address.Replace('0x', ''), 16), $Pitch, $Yaw)
