$ErrorActionPreference="Continue"
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;using System.Runtime.InteropServices;
public class W8a{
[DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
[DllImport("user32.dll")]public static extern void mouse_event(uint f,uint dx,uint dy,uint d,IntPtr e);
[DllImport("user32.dll")]public static extern void keybd_event(byte b,byte s,uint f,IntPtr e);}
"@
Add-Type @"
using System;using System.Runtime.InteropServices;
public class W8b{
[DllImport("user32.dll")]public static extern bool EnumWindows(EnumWindowsProc cb,IntPtr l);
public delegate bool EnumWindowsProc(IntPtr h,IntPtr l);
[DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,System.Text.StringBuilder s,int n);
[DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
[DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);}
"@
$script:hwnds=@()
$cb={ param($h,$l)
 $p=[uint32]0; [void][W8b]::GetWindowThreadProcessId($h,[ref]$p)
 if($p -eq 14636 -and [W8b]::IsWindowVisible($h)){
  $t=New-Object System.Text.StringBuilder 256; [void][W8b]::GetWindowText($h,$t,256)
  if($t.Length -gt 0){ $script:hwnds+=[pscustomobject]@{H=$h;T=$t.ToString()} } }
 return $true }
[void][W8b]::EnumWindows($cb,[IntPtr]::Zero)
$sb=New-Object System.Text.StringBuilder
function L([string]$t){ [void]$sb.AppendLine($t) }
L ("BEFORE windows="+$hwnds.Count)
foreach($w in $hwnds){ L (" h="+$w.H+" t="+$w.T) }
$fw=$hwnds | Where-Object { $_.T -like "*openbus*" } | Select-Object -First 1
if(-not $fw -and $hwnds.Count -gt 0){ $fw=$hwnds[0] }
if($fw){ [void][W8b]::SetForegroundWindow($fw.H); Start-Sleep -m 500 }
function KP([byte]$k,[uint32]$f){ [W8a]::keybd_event($k,0,$f,[IntPtr]::Zero) }
KP 0x1B 0; Start-Sleep -m 60; KP 0x1B 2; Start-Sleep -m 300
KP 0x12 0; Start-Sleep -m 50; KP 0x46 0; Start-Sleep -m 50
KP 0x46 2; Start-Sleep -m 50; KP 0x12 2; Start-Sleep -m 700
$bx=258;$by=106;$bw=280;$bh=260
$bmp=New-Object System.Drawing.Bitmap($bw,$bh)
$g=[System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($bx,$by,0,0,(New-Object System.Drawing.Size($bw,$bh)))
$bmp.Save("d:\sin\sin_20260727\sin\test_screenshots\_debug_file_menu2.png")
$rows=@()
for($y=0;$y -lt $bh;$y++){
 $dark=0
 for($x=0;$x -lt $bw;$x+=2){
  $c=$bmp.GetPixel($x,$y)
  if(($c.R*0.3+$c.G*0.59+$c.B*0.11) -lt 120){ $dark++ } }
 if($dark -gt 3){ $rows+=$y } }
$g.Dispose();$bmp.Dispose()
L ("rows relY: "+(($rows|ForEach-Object{$_.ToString()}) -join ","))
$clusters=@();$cur=@()
foreach($y in $rows){
 if($cur.Count -eq 0 -or ($y-$cur[-1]) -le 2){ $cur+=$y }
 else{ $clusters+=,@($cur);$cur=@($y) } }
if($cur.Count -gt 0){ $clusters+=,@($cur) }
$cs=@(); foreach($c in $clusters){ $cs+="$($c[0])-$($c[-1])" }
L ("clusters: "+($cs -join " | "))
$exitY=-1
foreach($c in $clusters){
 $mid=[int](($c[0]+$c[-1])/2)
 if($mid -ge 180 -and $mid -le 235){ $exitY=$mid } }
$mo=$false
foreach($c in $clusters){
 $mid=[int](($c[0]+$c[-1])/2)
 if($mid -ge 40 -and $mid -le 65){ $mo=$true } }
if(-not $mo){ $exitY=-1 }
if($exitY -ge 0){
 $sy=$by+$exitY
 L ("CLICK exit at (330,"+$sy+")")
 [void][W8a]::SetCursorPos(330,$sy); Start-Sleep -m 200
 [W8a]::mouse_event(2,0,0,0,[IntPtr]::Zero); Start-Sleep -m 80
 [W8a]::mouse_event(4,0,0,0,[IntPtr]::Zero)
}else{
 L "cluster not found, fallback Alt+F4"
 KP 0x12 0; Start-Sleep -m 50; KP 0x73 0; Start-Sleep -m 50
 KP 0x73 2; Start-Sleep -m 50; KP 0x12 2 }
Start-Sleep -m 3200
$pr=Get-Process -Id 14636 -ErrorAction SilentlyContinue
if($pr){ L ("PROCESS ALIVE responding="+$pr.Responding) } else { L "PROCESS EXITED" }
$script:h2=@()
$cb2={ param($h,$l)
 $p=[uint32]0; [void][W8b]::GetWindowThreadProcessId($h,[ref]$p)
 if($p -eq 14636 -and [W8b]::IsWindowVisible($h)){
  $t=New-Object System.Text.StringBuilder 256; [void][W8b]::GetWindowText($h,$t,256)
  if($t.Length -gt 0){ $script:h2+=[pscustomobject]@{H=$h;T=$t.ToString()} } }
 return $true }
[void][W8b]::EnumWindows($cb2,[IntPtr]::Zero)
L ("AFTER windows="+$h2.Count)
foreach($w in $h2){ L (" h="+$w.H+" t="+$w.T) }
$wer=@(Get-Process WerFault -ErrorAction SilentlyContinue)
L ("WerFault="+$wer.Count)
$vs=[System.Windows.Forms.SystemInformation]::VirtualScreen
$fb=New-Object System.Drawing.Bitmap($vs.Width,$vs.Height)
$fg=[System.Drawing.Graphics]::FromImage($fb)
$fg.CopyFromScreen($vs.X,$vs.Y,0,0,(New-Object System.Drawing.Size($vs.Width,$vs.Height)))
$fb.Save("d:\sin\sin_20260727\sin\test_screenshots\SM-08_after_exit.png")
$fg.Dispose();$fb.Dispose()
$sb.ToString()|Out-File -FilePath d:\sin\sin_20260727\sin\test_screenshots\sm08_exit.txt -Encoding utf8


