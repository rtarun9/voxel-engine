@echo off

powershell -Command "Invoke-WebRequest -Uri https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.711.3-preview -OutFile agility.zip"
powershell -Command "& {Expand-Archive agility.zip external/DirectXAgilitySDK}"

xcopy external\DirectXAgilitySDK\build\native\bin\x64\*  build\debug\bin\D3D12\
xcopy external\DirectXAgilitySDK\build\native\bin\x64\* build\release\bin\D3D12\

powershell -Command "Invoke-WebRequest -Uri https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.7.2212/dxc_2022_12_16.zip -OutFile dxc.zip"
powershell -Command "& {Expand-Archive dxc.zip external/DirectXShaderCompiler}"

xcopy External\DirectXShaderCompiler\bin\x64\* build\debug\bin\
xcopy External\DirectXShaderCompiler\bin\x64\* build\release\bin\
