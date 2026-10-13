Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class DesktopProbe {
 public delegate bool EnumProc(IntPtr h,IntPtr l);
 [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p,IntPtr l);
 [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h,EnumProc p,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] static extern IntPtr GetParent(IntPtr h);
 [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr h,uint c);
 [DllImport("user32.dll")] static extern IntPtr GetWindowLongPtr(IntPtr h,int n);
 [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h,out Rect r);
 [StructLayout(LayoutKind.Sequential)] struct Rect {public int l,t,r,b;}
 static string Class(IntPtr h){var s=new StringBuilder(256);GetClassName(h,s,256);return s.ToString();}
 static void Print(IntPtr h){uint pid;GetWindowThreadProcessId(h,out pid);Rect r;GetWindowRect(h,out r);Console.WriteLine("hwnd={0:X} parent={1:X} prev={2:X} next={3:X} pid={4} class={5} visible={6} style={7:X} ex={8:X} rect={9},{10},{11},{12}",h.ToInt64(),GetParent(h).ToInt64(),GetWindow(h,3).ToInt64(),GetWindow(h,2).ToInt64(),pid,Class(h),IsWindowVisible(h),GetWindowLongPtr(h,-16).ToInt64(),GetWindowLongPtr(h,-20).ToInt64(),r.l,r.t,r.r,r.b);}
 public static void Run(){EnumWindows((h,l)=>{var c=Class(h);if(c=="Progman"||c=="WorkerW"||c.StartsWith("Plume")){Print(h);EnumChildWindows(h,(ch,cl)=>{Print(ch);return true;},IntPtr.Zero);}return true;},IntPtr.Zero);}
}
"@
[DesktopProbe]::Run()
Get-Process PlumeV0 -ErrorAction SilentlyContinue | Select-Object Id,WorkingSet64,PrivateMemorySize64,Path
