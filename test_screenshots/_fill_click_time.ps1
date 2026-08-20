param([int]$mainHwnd,[int]$procId,[string]$path)
Add-Type -AssemblyName UIAutomationClient
$m='[DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);[DllImport("user32.dll")] public static extern bool SendMessageTimeout(IntPtr h, uint msg, IntPtr wp, IntPtr lp, uint flags, uint timeout, out IntPtr result);'
Add-Type -MemberDefinition $m -Name LT -Namespace U
$a=[System.Windows.Automation.AutomationElement]
$title = -join ([char]25171,[char]24320,[char]25991,[char]20214)
$btnName = (-join ([char]25171,[char]24320)) + '(O)'
$dlg=$null
foreach($w in $a::RootElement.FindAll('Children',(New-Object System.Windows.Automation.PropertyCondition($a::ProcessIdProperty,$procId)))){
  if($w.Current.Name -eq $title){$dlg=$w;break}
}
if(-not $dlg){Write-Output 'DLG_NOT_FOUND';exit}
$edit=$dlg.FindFirst('Descendants',(New-Object System.Windows.Automation.PropertyCondition($a::AutomationIdProperty,'1148')))
if(-not $edit){Write-Output 'EDIT_NOT_FOUND';exit}
$vp=$edit.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
$vp.SetValue($path)
Write-Output 'FILLED'
$condBtn=New-Object System.Windows.Automation.AndCondition(
  (New-Object System.Windows.Automation.PropertyCondition($a::NameProperty,$btnName)),
  (New-Object System.Windows.Automation.PropertyCondition($a::ControlTypeProperty,[System.Windows.Automation.ControlType]::Button)))
$btn=$dlg.FindFirst('Descendants',$condBtn)
if(-not $btn){Write-Output 'BTN_NOT_FOUND';exit}
$main=[IntPtr]$mainHwnd
$sw=[System.Diagnostics.Stopwatch]::StartNew()
$inv=$btn.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
$inv.Invoke()
$t1=-1;$t2=-1;$seenEnabled=$false;$seenBusy=$false;$lastBusy=-1
while($sw.Elapsed.TotalMilliseconds -lt 25000){
  $en=[U.LT]::IsWindowEnabled($main)
  $r=[IntPtr]::Zero
  $ok=[U.LT]::SendMessageTimeout($main,0,[IntPtr]::Zero,[IntPtr]::Zero,2,40,[ref]$r)
  $ms=[int]$sw.Elapsed.TotalMilliseconds
  if($en -and -not $seenEnabled){$seenEnabled=$true;$t1=$ms}
  if($seenEnabled){
    if(-not $ok){$seenBusy=$true;$lastBusy=$ms}
    elseif($seenBusy){$t2=$ms;break}
    elseif(($ms-$t1) -gt 2000){$t2=$ms;break}
  }
  Start-Sleep -Milliseconds 15
}
$dur = if($t1 -ge 0 -and $t2 -ge 0){$t2-$t1}else{-1}
Write-Output ("t1_dlgclosed={0} t2_resp={1} busy_seen={2} lastBusy={3} load_duration={4}ms" -f $t1,$t2,$seenBusy,$lastBusy,$dur)
