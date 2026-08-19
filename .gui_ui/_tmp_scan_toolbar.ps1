Add-Type -AssemblyName System.Drawing
$img=[System.Drawing.Image]::FromFile('d:\sin\sin_20260727\sin\.gui_ui\step4_graphic.png')
Write-Host ('IMG size: '+$img.Width+'x'+$img.Height)
$bmp=New-Object System.Drawing.Bitmap($img)
$cols=@{}
for($y=75;$y -lt 112;$y++){
  for($x=310;$x -lt 1900;$x++){
    $c=$bmp.GetPixel($x,$y)
    $lum=0.299*$c.R+0.587*$c.G+0.114*$c.B
    if($lum -lt 150){
      if(-not $cols.ContainsKey($x)){$cols[$x]=0}
      $cols[$x]++
    }
  }
}
$xs=$cols.Keys | Sort-Object
Write-Host ('dark col count: '+$xs.Count)
$groups=@()
$start=-1; $prev=-1
foreach($x in $xs){
  if($start -lt 0){$start=$x;$prev=$x;continue}
  if($x - $prev -gt 6){ $groups+=,@($start,$prev); $start=$x }
  $prev=$x
}
if($start -ge 0){$groups+=,@($start,$prev)}
Write-Host 'icon groups (x ranges) y=75-112:'
foreach($g in $groups){ Write-Host ('  x='+$g[0]+'-'+$g[1]+' w='+($g[1]-$g[0]+1)) }
$bmp.Dispose(); $img.Dispose()
