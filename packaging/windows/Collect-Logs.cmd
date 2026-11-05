@echo off
rem Zips the launcher's and the game's logs and settings (never your game files or keys) into kartrecomp-logs.zip in this folder.
cd /d "%~dp0"
if not exist userdata (echo There is no userdata folder yet: run the game first. & pause & exit /b 1)
powershell -NoProfile -Command "$ErrorActionPreference='SilentlyContinue'; $f=@(); $f+=Get-ChildItem -Recurse userdata -Include *.log,*.txt,*.ini,*.json,*.env | Where-Object { $_.FullName -notmatch '\\(content|keys|export|build-work|nand)\\' }; if(Test-Path kartrecomp-logs.zip){Remove-Item kartrecomp-logs.zip}; Compress-Archive -Path $f.FullName -DestinationPath kartrecomp-logs.zip; Write-Host ('Wrote ' + (Resolve-Path kartrecomp-logs.zip))"
pause
