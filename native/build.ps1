$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskVc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$taskSdk = 'C:\Program Files (x86)\Windows Kits\10'
$taskSdkVersion = '10.0.26100.0'
$taskBuild = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $taskBuild | Out-Null
Push-Location $taskRoot
try {
  & "$taskVc\bin\Hostx64\x86\cl.exe" /nologo /LD /MT /O2 /W4 /WX /EHsc /I"$taskVc\include" /I"$taskSdk\Include\$taskSdkVersion\ucrt" /I"$taskSdk\Include\$taskSdkVersion\shared" /I"$taskSdk\Include\$taskSdkVersion\um" /Fonative\build\ /Fe"$taskBuild\TFUPoseHistory.dll" native\TFUPoseHistory.cpp native\ClockJobRouter.cpp /link /DEF:native\TFUPoseHistory.def /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /LIBPATH:"$taskVc\lib\x86" /LIBPATH:"$taskSdk\Lib\$taskSdkVersion\ucrt\x86" /LIBPATH:"$taskSdk\Lib\$taskSdkVersion\um\x86" kernel32.lib
  if ($LASTEXITCODE -ne 0) { throw 'Native pose history module build failed' }
} finally { Pop-Location }
