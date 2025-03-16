@echo off

powershell -Command "Invoke-WebRequest -Uri https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.614.1 -OutFile agility.zip"
powershell -Command "& {Expand-Archive agility.zip external/agility-sdk}"

xcopy external\agility-sdk\build\native\bin\x64\*  build\debug\bin\D3D12\
xcopy external\agility-sdk\build\native\bin\x64\* build\release\bin\D3D12\

powershell -Command "Invoke-WebRequest -Uri https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2407/dxc_2024_07_31.zip -OutFile dxc.zip"
powershell -Command "& {Expand-Archive dxc.zip external/dxc}"

xcopy External\dxc\bin\x64\* build\debug\bin\
xcopy External\dxc\bin\x64\* build\release\bin\
