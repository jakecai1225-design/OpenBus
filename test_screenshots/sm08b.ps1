$ErrorActionPreference="Continue"
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;using System.Runtime.InteropServices;
public class K8{
[DllImport("user32.dll")]public static extern void keybd_event(byte b,byte s,uint f,IntPtr e);
[DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")]public static extern bool EnumWindows(EnumWindowsProc cb,IntPtr l);
public delegate bool EnumWindowsProc(IntPtr h,IntPtr l);
[DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,System.Text.StringBuilder s,int n);
[DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);}
"@
$sb=New-Object System.Text.StringBuilder
function L([string]$t){ [void]$sb.AppendLine($t) }
function KP([byte]$k,[uint32]$f){ [K8]::keybd_event($k,0,$f,[IntPtr]::Zero) }
$script:hs=@()
$cb={ param($h,$l)
 $p=[uint32]0; [void][K8]::GetWindowThreadProcessId($h,[ref]$p)
 if($p -eq 14636 -and [K8]::IsWindowVisible($h)){
  $t=New-Object System.Text.StringBuilder 256; [void][K8]::GetWindowText($h,$t,256)
  if($t.Length -gt 0){ $script:hs+=[pscustomobject]@{H=$h;T=$t.ToString()} } }
 return $true }
function EnumOb{ $script:hs=@(); [void][K8]::EnumWindows($cb,[IntPtr]::Zero); $script:hs }
$before=EnumOb
L ("BEFORE windows="+$before.Count)
foreach($w in $before){ L (" h="+$w.H+" t="+$w.T) }
$fw=$before | Where-Object { $_.T -like "*CAN*" } | Select-Object -First 1
if(-not $fw -and $before.Count -gt 0){ $fw=$before[0] }
if($fw){ [void][K8]::SetForegroundWindow($fw.H); Start-Sleep -m 500 }
KP 0x1B 0; Start-Sleep -m 60; KP 0x1B 2; Start-Sleep -m 300
KP 0x12 0; Start-Sleep -m 50; KP 0x46 0; Start-Sleep -m 50
KP 0x46 2; Start-Sleep -m 50; KP 0x12 2; Start-Sleep -m 600
1..4 | ForEach-Object { KP 0x28 0; Start-Sleep -m 60; KP 0x28 2; Start-Sleep -m 140 }
Start-Sleep -m 300
$bmp=New-Object System.Drawing.Bitmap(280,300)
$g=[System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen(258,66,0,0,(New-Object System.Drawing.Size(280,300)))
$bmp.Save("d:\sin\sin_20260727\sin\test_screenshots\_debug_exit_highlight.png")
$g.Dispose();$bmp.Dispose()
KP 0x0D 0; Start-Sleep -m 60; KP 0x0D 2
Start-Sleep -m 1500
$pr=Get-Process -Id 14636 -ErrorAction SilentlyContinue
if($pr){
 L "menu Enter did NOT exit, fallback Alt+F4"
 KP 0x12 0; Start-Sleep -m 50; KP 0x73 0; Start-Sleep -m 50
 KP 0x73 2; Start-Sleep -m 50; KP 0x12 2
 Start-Sleep -m 3500
}else{ L "exited via menu Enter" }
$pr=Get-Process -Id 14636 -ErrorAction SilentlyContinue
if($pr){ L ("PROCESS ALIVE responding="+$pr.Responding) } else { L "PROCESS EXITED" }
$after=EnumOb
L ("AFTER windows="+$after.Count)
foreach($w in $after){ L (" h="+$w.H+" t="+$w.T) }
$wer=@(Get-Process WerFault -ErrorAction SilentlyContinue)
L ("WerFault="+$wer.Count)
$vs=[System.Windows.Forms.SystemInformation]::VirtualScreen
$fb=New-Object System.Drawing.Bitmap($vs.Width,$vs.Height)
$fg=[System.Drawing.Graphics]::FromImage($fb)
$fg.CopyFromScreen($vs.X,$vs.Y,0,0,(New-Object System.Drawing.Size($vs.Width,$vs.Height)))
$fb.Save("d:\sin\sin_20260727\sin\test_screenshots\SM-08_after_exit2.png")
$fg.Dispose();$fb.Dispose()
$sb.ToString()|Out-File -FilePath d:\sin\sin_20260727\sin\test_screenshots\sm08b_exit.txt -Encoding utf8
