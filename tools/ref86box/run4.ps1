param([int]$Interval = 15, [int]$Count = 6, [string]$Prefix = "D:\86box\r3")
# One launch, a timed series of window captures (PrintWindow works without a desktop session), then stop 86Box.
$src = @"
using System;using System.Drawing;using System.Drawing.Imaging;using System.Runtime.InteropServices;
public class Cap{
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint f);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
 [StructLayout(LayoutKind.Sequential)] public struct RECT{public int L,T,R,B;}
 public static string Shot(IntPtr h,string path){RECT r;GetWindowRect(h,out r);int w=r.R-r.L,hh=r.B-r.T;if(w<=0||hh<=0)return "bad rect";
  using(var bmp=new Bitmap(w,hh)){using(var g=Graphics.FromImage(bmp)){IntPtr dc=g.GetHdc();bool ok=PrintWindow(h,dc,2);g.ReleaseHdc(dc);}bmp.Save(path,ImageFormat.Png);return "ok";}}
}
"@
Add-Type -TypeDefinition $src -ReferencedAssemblies System.Drawing
Get-Process 86Box -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 1
$p = Start-Process -FilePath "D:\86box\app\86Box.exe" -ArgumentList '-P','D:\86box\vm','-N' -WorkingDirectory "D:\86box\app" -PassThru
for ($i = 1; $i -le $Count; $i++) {
  Start-Sleep -Seconds $Interval
  $q = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
  if (-not $q) { "86Box exited at shot $i"; break }
  [void][Cap]::SetWindowPos($q.MainWindowHandle, [IntPtr]::Zero, 0, 0, 330, 700, 0x14)
  Start-Sleep -Milliseconds 700
  [void][Cap]::Shot($q.MainWindowHandle, "$Prefix-$i.png")
}
Get-Process 86Box -ErrorAction SilentlyContinue | Stop-Process -Force
"done"
