param([int]$mainHwnd,[int]$seconds,[string]$outFile)
$m='[DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);[DllImport("user32.dll")] public static extern bool SendMessageTimeout(IntPtr h, uint msg, IntPtr wp, IntPtr lp, uint flags, uint timeout, out IntPtr result);'
Add-Type -MemberDefinition $m -Name LT -Namespace U
$main=[IntPtr]$mainHwnd
$sb=New-Object System.Text.StringBuilder
$sw=[System.Diagnostics.Stopwatch]::StartNew()
while($sw.Elapsed.TotalSeconds -lt $seconds){
  $t=[int]$sw.Elapsed.TotalMilliseconds
  $en=[U.LT]::IsWindowEnabled($main)
  $r=[IntPtr]::Zero
  $ok=[U.LT]::SendMessageTimeout($main,0,[IntPtr]::Zero,[IntPtr]::Zero,2,40,[ref]$r)
  [void]$sb.AppendLine(("{0} enabled={1} resp={2}" -f $t,$en,$ok))
  Start-Sleep -Milliseconds 40
}
[System.IO.File]::WriteAllText($outFile,$sb.ToString())
Write-Output "TIMELINE_DONE"
