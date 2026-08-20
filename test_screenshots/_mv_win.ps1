param([int]$hwnd,[int]$x,[int]$y,[int]$w,[int]$h)
$m='[DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool r);'
Add-Type -MemberDefinition $m -Name W2 -Namespace U
[U.W2]::MoveWindow([IntPtr]$hwnd,$x,$y,$w,$h,$true)
Write-Output 'MOVED'
