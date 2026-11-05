# Prints the kernel object name of a handle in another process (e.g. the device moh.exe is blocked on).
# Usage: tools\handle-name.ps1 -ProcessName moh -Handle 0x121c
param([string]$ProcessName = 'moh', [Parameter(Mandatory)][string]$Handle)

Add-Type @'
using System; using System.Runtime.InteropServices; using System.Threading;
public static class HN {
  [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll", SetLastError=true)] static extern bool DuplicateHandle(IntPtr sp, IntPtr sh, IntPtr tp, out IntPtr th, uint a, bool i, uint o);
  [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  [DllImport("ntdll.dll")] static extern int NtQueryObject(IntPtr h, int cls, IntPtr buf, int len, out int ret);
  public static string Name(int pid, long handle) {
    IntPtr hp = OpenProcess(0x40, false, pid); // PROCESS_DUP_HANDLE
    if (hp == IntPtr.Zero) return "OpenProcess failed " + Marshal.GetLastWin32Error();
    IntPtr dup;
    if (!DuplicateHandle(hp, new IntPtr(handle), GetCurrentProcess(), out dup, 0, false, 2)) return "DuplicateHandle failed " + Marshal.GetLastWin32Error();
    string result = "(timeout: query blocked)";
    var t = new Thread(() => {
      IntPtr buf = Marshal.AllocHGlobal(4096); int ret;
      int st = NtQueryObject(dup, 1, buf, 4096, out ret); // ObjectNameInformation
      result = st == 0 ? (Marshal.PtrToStringUni(Marshal.ReadIntPtr(buf, IntPtr.Size)) ?? "(no name)") : ("NtQueryObject 0x" + st.ToString("X8"));
      Marshal.FreeHGlobal(buf);
    });
    t.IsBackground = true; t.Start(); t.Join(3000);
    return result;
  }
}
'@
$p = Get-Process $ProcessName -ErrorAction Stop | Select-Object -First 1
[HN]::Name($p.Id, [Convert]::ToInt64($Handle, 16))
