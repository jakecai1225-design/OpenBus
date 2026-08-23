# build_e2e.ps1 - manual moc+g++ build of scripts/e2e_plugin_flow.cpp (not in CMake)
# ASCII only (PS 5.1 tokenizer), no &&/||.
param([string]$Name = "e2e_plugin_flow")

$qt = "C:/Qt/6.8.3/mingw_64"
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;" + $qt + "\bin;" + $env:PATH
Set-Location (Join-Path $PSScriptRoot "..")

$src = "scripts/" + $Name + ".cpp"
$moc = "scripts/" + $Name + ".moc"
Write-Host ("== moc " + $src)
moc.exe -o $moc $src
if ($LASTEXITCODE -ne 0) { Write-Host "MOC_FAILED"; exit 1 }

Write-Host "== g++ link"
g++ -std=c++17 -DUNICODE -D_UNICODE -DQT_CORE_LIB -DQT_NETWORK_LIB `
    -Ibuild/src/openbus_data_autogen/include -Isrc `
    -Ithird_party/nlohmann_json -Ithird_party/spdlog/include `
    -isystem "$qt/include" -isystem "$qt/include/QtCore" `
    -isystem "$qt/include/QtNetwork" -isystem "$qt/mkspecs/win32-g++" `
    $src -Lbuild/src -lopenbus_data `
    "$qt/lib/libQt6Core.a" "$qt/lib/libQt6Network.a" `
    -lole32 -luuid -lws2_32 -o ("build/bin/" + $Name + ".exe")
exit $LASTEXITCODE
