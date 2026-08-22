[Setup]
AppName=Raksha Hub
AppVersion=1.0
AppPublisher=RiRiRonRon
DefaultDirName={autopf}\RakshaHub
DefaultGroupName=Raksha Hub
OutputDir=D:\RakshaHub_Installer
OutputBaseFilename=RakshaHub_Setup
SetupIconFile=D:\Raksha_Hub\build\Desktop_Qt_6_11_1_MinGW_64_bit_Release\app.ico
Compression=lzma
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Créer un raccourci sur le bureau"; GroupDescription: "Icônes supplémentaires:"

[Files]
Source: "D:\Raksha_Hub\build\Desktop_Qt_6_11_1_MinGW_64_bit_Release\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Raksha Hub"; Filename: "{app}\appRaksha_Hub.exe"
Name: "{commondesktop}\Raksha Hub"; Filename: "{app}\appRaksha_Hub.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\appRaksha_Hub.exe"; Description: "Lancer Raksha Hub"; Flags: nowait postinstall skipifsilent