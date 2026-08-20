param($n)
Add-Type -AssemblyName UIAutomationClient
$a=[System.Windows.Automation.AutomationElement]
$w=$a::RootElement.FindFirst('Children',(New-Object System.Windows.Automation.PropertyCondition($a::ProcessIdProperty,8152)))
$e=$w.FindFirst('Descendants',(New-Object System.Windows.Automation.PropertyCondition($a::NameProperty,$n)))
if($e){$b=$e.Current.BoundingRectangle;"x=$([int]$b.X) y=$([int]$b.Y) w=$([int]$b.Width) h=$([int]$b.Height)"}else{'NOT_FOUND'}
