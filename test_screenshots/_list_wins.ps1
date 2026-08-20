Add-Type -AssemblyName UIAutomationClient
$a=[System.Windows.Automation.AutomationElement]
$ws=$a::RootElement.FindAll('Children',(New-Object System.Windows.Automation.PropertyCondition($a::ProcessIdProperty,8152)))
Write-Output ("count=" + $ws.Count)
foreach($w in $ws){
  try{
    $n=$w.Current.Name
    $pid2=$w.Current.ProcessId
    $ct=$w.Current.ControlType.ProgrammaticName
    Write-Output ("[" + $n + "] pid=" + $pid2 + " type=" + $ct)
  }catch{
    Write-Output ("ERR: " + $_.Exception.Message)
  }
}
