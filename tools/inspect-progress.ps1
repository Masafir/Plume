Add-Type @"
using System;using System.Text;using System.Runtime.InteropServices;
public class PlumeProgressProbe {
 public delegate bool Callback(IntPtr h,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h,Callback p,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 public static void Run(){var h=FindWindow("PlumeV0.Settings",null);EnumChildWindows(h,(child,l)=>{var c=new StringBuilder(256);GetClassName(child,c,256);var text=new StringBuilder(1024);GetWindowText(child,text,1024);if(c.ToString()=="msctls_progress32")Console.WriteLine("progress-position="+SendMessage(child,0x408,IntPtr.Zero,IntPtr.Zero));else if(text.ToString().Contains("Préparation")||text.ToString().Contains("RAM"))Console.WriteLine(text.ToString());return true;},IntPtr.Zero);}
}
"@
[PlumeProgressProbe]::Run()
