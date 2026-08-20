param([int]$procId,[string]$n)
Add-Type -AssemblyName UIAutomationClient
$a=[System.Windows.Automation.AutomationElement]
$ws=$a::RootElement.FindAll('Children',(New-Object System.Windows.Automation.PropertyCondition($a::ProcessIdProperty,$procId)))
foreach($w in $ws){
  $e=$w.FindFirst('Descendants',(New-Object System.Windows.Automation.PropertyCondition($a::NameProperty,$n)))
  if($e){$b=$e.Current.BoundingRectangle;"FOUND x=$([int]$b.X) y=$([int]$b.Y) w=$([int]$b.Width) h=$([int]$b.Height)";exit}
}
'NOT_FOUND'
