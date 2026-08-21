; ============================================================
;  openbus Windows 安装器 — Inno Setup 脚本（doc/打包安装方案.md §8）
;
;  编译（package.py 步骤 8b 自动调用，亦可手动）：
;      iscc /DAppVersion=0.1.0 installer\openbus.iss
;
;  要点（方案 §8）：
;  - 安装目录默认 C:\Program Files\openbus，可自定义；
;  - 开始菜单快捷方式必选、桌面图标用户可选；
;  - 卸载只删安装目录与快捷方式，不动 %APPDATA%\openbus（确认页提示）；
;  - 完成页检测 ZCANPRO / PCANUSB / canlib32 缺失并提示（不阻塞安装）；
;  - 中英双语向导；
;  - 允许覆盖安装（升级）；openbus.exe 运行中提示先退出。
;  - 签名：预留 SignTool 参数位（无证书阶段跳过）。
; ============================================================

#define AppName "openbus"
#ifndef AppVersion
#define AppVersion "0.1.0"
#endif
#define AppPublisher "openbus"
#define AppExe "openbus.exe"

[Setup]
AppId={{8E2B7E4C-6D9F-4A31-9C57-A10B7E4C0B01}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\{#AppExe}
; 覆盖安装（升级）允许；运行中进程检测（提示先退出）
CloseApplications=yes
RestartApplications=no
; 输出到 dist/（与便携版 zip 同目录）
OutputDir=..\dist
OutputBaseFilename=openbus-{#AppVersion}-win64-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; 双语向导
ShowLanguageDialog=yes

[Languages]
Name: "zh_cn"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; staging 目录整树打包（package.py 步骤 3-5 已组装完毕）
Source: "..\dist\stage\openbus\*"; DestDir: "{app}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 只清理安装目录内程序文件；%APPDATA%\openbus 用户数据不删（§8 第 4 点）

[Code]
// ---- 完成页：硬件厂商驱动缺失提示（§8 第 5 点，不阻塞安装） ----
function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo,
    MemoTypeInfo, MemoComponentsInfo, String): String;
var
    Memo: String;
begin
    Memo := MemoDirInfo + NewLine;
    if not FileExists(ExpandConstant('{win}\zlgcan.dll'))
       and not FileExists(ExpandConstant('{app}\zlgcan.dll')) then
        Memo := Memo + NewLine +
            'ZLG 驱动未检测到（ZCANPRO 未安装）——ZLG 设备将不可用' + NewLine +
            'ZLG driver not found (ZCANPRO not installed)' + NewLine;
    if not FileExists(ExpandConstant('{win}\PCANUSB.dll')) then
        Memo := Memo + NewLine +
            'PEAK 驱动未检测到——PEAK 设备将不可用' + NewLine +
            'PEAK driver not found' + NewLine;
    if not FileExists(ExpandConstant('{win}\canlib32.dll')) then
        Memo := Memo + NewLine +
            'Kvaser CANlib 未检测到——Kvaser 设备将不可用' + NewLine +
            'Kvaser CANlib not found' + NewLine;
    Memo := Memo + NewLine +
        '说明：未安装驱动不影响软件其它功能；安装指引见 README-PORTABLE.txt' + NewLine +
        'Note: missing drivers only disable corresponding devices.';
    Result := Memo;
end;
