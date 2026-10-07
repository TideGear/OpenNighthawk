<#
 hidden86.ps1 - run 86Box on its own, invisible Windows desktop, type into it,
 and capture its window. Nothing appears on the user's screen and the user's
 mouse and keyboard cannot reach it (each desktop has its own focus).
 PrintWindow works there, and a key posted to a window that holds the
 desktop's focus is delivered like a real one.

   powershell -NoProfile -ExecutionPolicy Bypass -File hidden86.ps1 `
       -Profile D:\86box\vm -Script "wait 25; shot a.png; keys abc; wait 3; shot b.png" `
       [-Exe D:\86box\app\86Box.exe]

 Script words, separated by ';': wait SECONDS | shot FILE | key VKHEX SCANHEX
 | keys TEXT (letters, digits, space, \r for Enter).
 86Box is killed at the end. It runs at below-normal priority so it never
 competes with real work.
#>
param([string]$Profile = "D:\86box\vm", [string]$Exe = "D:\86box\app\86Box.exe",
      [string]$Script = "wait 20; shot D:\86box\hid.png", [string]$Desktop = "f117box86", [switch]$Inner)
$src = @"
using System;using System.Text;using System.Collections.Generic;using System.Drawing;using System.Drawing.Imaging;
using System.Runtime.InteropServices;using System.Threading;
public class H{
 [StructLayout(LayoutKind.Sequential)] public struct RECT{public int L,T,R,B;}
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Auto)] public struct SI{public int cb;public string r;public string d;public string t;public int x,y,xs,ys,xc,yc,fa,fl;public short sw,rs;public IntPtr rs2,i,o,e;}
 [StructLayout(LayoutKind.Sequential)] public struct PI{public IntPtr hp,ht;public int pid,tid;}
 public delegate bool EP(IntPtr h,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Auto)] public static extern IntPtr CreateDesktop(string n,IntPtr d,IntPtr dm,int f,uint a,IntPtr sa);
 [DllImport("user32.dll")] public static extern bool SetThreadDesktop(IntPtr d);
 [DllImport("user32.dll")] public static extern bool CloseDesktop(IntPtr d);
 [DllImport("user32.dll")] public static extern bool EnumDesktopWindows(IntPtr d,EP cb,IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p,EP cb,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Auto)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint f);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr SetActiveWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a,uint b,bool f);
 [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
 [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
 [DllImport("kernel32.dll",CharSet=CharSet.Auto,SetLastError=true)] public static extern bool CreateProcess(string app,string cmd,IntPtr pa,IntPtr ta,bool inh,uint fl,IntPtr env,string dir,ref SI si,out PI pi);
 [DllImport("user32.dll")] public static extern IntPtr GetThreadDesktop(uint t);
 public static IntPtr Desk; public static IntPtr Main;
 public static void UseThreadDesktop(){Desk=GetThreadDesktop(GetCurrentThreadId());}
 public static string MakeDesktop(string name){
  SetProcessDPIAware();
  Desk=CreateDesktop(name,IntPtr.Zero,IntPtr.Zero,0,0x10000000,IntPtr.Zero);
  return Desk==IntPtr.Zero ? "CreateDesktop failed "+Marshal.GetLastWin32Error() : "ok";}
 public static string Start(string cmd,string dir,string name,uint prio){
  SI si=new SI(); si.cb=Marshal.SizeOf(si); si.d="winsta0\\"+name; PI pi;
  if(!CreateProcess(null,cmd,IntPtr.Zero,IntPtr.Zero,false,prio,IntPtr.Zero,dir,ref si,out pi)) return "CreateProcess failed "+Marshal.GetLastWin32Error();
  return "pid "+pi.pid;}
 public static List<IntPtr> Tops(){var l=new List<IntPtr>();EnumDesktopWindows(Desk,(h,x)=>{l.Add(h);return true;},IntPtr.Zero);return l;}
 public static string Text(IntPtr h){var s=new StringBuilder(256);GetWindowText(h,s,256);return s.ToString();}
 public static IntPtr FindMain(){foreach(var h in Tops()){ if(Text(h).StartsWith("vm - 86Box")) {Main=h;return h;} } return IntPtr.Zero;}
 public static List<IntPtr> Kids(){var l=new List<IntPtr>();EnumChildWindows(Main,(h,x)=>{l.Add(h);return true;},IntPtr.Zero);return l;}
 public static void Focus(){
  uint pid; uint t=GetWindowThreadProcessId(Main,out pid); uint me=GetCurrentThreadId();
  AttachThreadInput(me,t,true); SetForegroundWindow(Main); SetActiveWindow(Main); SetFocus(Main); AttachThreadInput(me,t,false);}
 public static void Key(int vk,int sc,bool ext){
  uint down=(uint)(1|(sc<<16)|(ext?1<<24:0)); uint up=down|(1u<<30)|(1u<<31);
  foreach(var h in Kids()){ PostMessage(h,0x100,(IntPtr)vk,(IntPtr)down); }
  PostMessage(Main,0x100,(IntPtr)vk,(IntPtr)down);
  Thread.Sleep(60);
  foreach(var h in Kids()){ PostMessage(h,0x101,(IntPtr)vk,(IntPtr)up); }
  PostMessage(Main,0x101,(IntPtr)vk,(IntPtr)up);
  Thread.Sleep(60);}
 public static string Shot(string path){RECT r;GetWindowRect(Main,out r);int w=r.R-r.L,hh=r.B-r.T;if(w<=0||hh<=0)return "bad rect";
  using(var bmp=new Bitmap(w,hh)){using(var g=Graphics.FromImage(bmp)){IntPtr dc=g.GetHdc();PrintWindow(Main,dc,2);g.ReleaseHdc(dc);}bmp.Save(path,ImageFormat.Png);}return "ok";}
}
"@
Add-Type -TypeDefinition $src -ReferencedAssemblies System.Drawing
if (-not $Inner) {
    Get-Process 86Box -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep 1
    $r = [H]::MakeDesktop($Desktop); "desktop: $r"
    if ($r -ne "ok") { exit 1 }
    $q = [string][char]34
    $r = [H]::Start($q + $Exe + $q + " -P " + $q + $Profile + $q + " -N", (Split-Path $Exe), $Desktop, 0x4000); "86Box: $r"
    if ($r -notlike "pid *") { exit 1 }
    # The driver runs on the same desktop, so it can focus the 86Box window and
    # its posted keys are delivered. It gets the script through the environment.
    $env:H86_SCRIPT = $Script
    $self = $MyInvocation.MyCommand.Path
    $r = [H]::Start("powershell.exe -NoProfile -ExecutionPolicy Bypass -File " + $q + $self + $q + " -Inner -Desktop " + $Desktop, (Split-Path $self), $Desktop, 0x4000); "driver: $r"
    $dpid = [int]($r -replace "pid ", "")
    Wait-Process -Id $dpid -ErrorAction SilentlyContinue
    Get-Process 86Box -ErrorAction SilentlyContinue | Stop-Process -Force
    [void][H]::CloseDesktop([H]::Desk)
    "done"
    exit 0
}
[H]::UseThreadDesktop()
$Script = $env:H86_SCRIPT
$main = [IntPtr]::Zero
for ($i = 0; $i -lt 60 -and $main -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 500; $main = [H]::FindMain() }
if ($main -eq [IntPtr]::Zero) { "no 86Box window"; Get-Process 86Box -ErrorAction SilentlyContinue | Stop-Process -Force; exit 1 }
"window: $main"
[H]::Focus()
$sc = @{ 'a'=0x1E;'b'=0x30;'c'=0x2E;'d'=0x20;'e'=0x12;'f'=0x21;'g'=0x22;'h'=0x23;'i'=0x17;'j'=0x24;'k'=0x25;'l'=0x26;'m'=0x32;'n'=0x31;'o'=0x18;'p'=0x19;'q'=0x10;'r'=0x13;'s'=0x1F;'t'=0x14;'u'=0x16;'v'=0x2F;'w'=0x11;'x'=0x2D;'y'=0x15;'z'=0x2C;
  '1'=0x02;'2'=0x03;'3'=0x04;'4'=0x05;'5'=0x06;'6'=0x07;'7'=0x08;'8'=0x09;'9'=0x0A;'0'=0x0B;' '=0x39 }
foreach ($step in $Script.Split(';')) {
  $w = $step.Trim().Split(' ', 2); $verb = $w[0]; $arg = if ($w.Count -gt 1) { $w[1] } else { "" }
  switch ($verb) {
    'wait' { Start-Sleep -Seconds ([double]$arg) }
    'shot' { "shot $arg : " + [H]::Shot($arg) }
    'key'  { $p = $arg.Split(' '); [H]::Focus(); [H]::Key([Convert]::ToInt32($p[0], 16), [Convert]::ToInt32($p[1], 16), $false) }
    'keys' { [H]::Focus(); $t = $arg; $i = 0
             while ($i -lt $t.Length) {
               if ($t[$i] -eq '\' -and $i + 1 -lt $t.Length -and $t[$i+1] -eq 'r') { [H]::Key(0x0D, 0x1C, $false); $i += 2; continue }
               $c = ([string]$t[$i]).ToLower()
               if ($sc.ContainsKey($c)) { [H]::Key([int][char]$c.ToUpper(), $sc[$c], $false) }
               $i++ } }
  }
}
