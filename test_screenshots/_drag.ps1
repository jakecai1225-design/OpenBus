param([int]$x1,[int]$y1,[int]$x2,[int]$y2)
Add-Type -AssemblyName System.Windows.Forms
$m='[DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);[DllImport("user32.dll")] public static extern void mouse_event(uint f,uint dx,uint dy,uint d,UIntPtr i);'
Add-Type -MemberDefinition $m -Name W3 -Namespace U
[U.W3]::SetCursorPos($x1,$y1)
Start-Sleep -Milliseconds 300
[U.W3]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
Start-Sleep -Milliseconds 200
[U.W3]::SetCursorPos($x1+10,$y1+5)
Start-Sleep -Milliseconds 100
$steps=15
for($i=1;$i -le $steps;$i++){
  $nx=$x1+[int](($x2-$x1)*$i/$steps)
  $ny=$y1+[int](($y2-$y1)*$i/$steps)
  [U.W3]::SetCursorPos($nx,$ny)
  Start-Sleep -Milliseconds 45
}
[U.W3]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
Write-Output 'DRAG_DONE'
