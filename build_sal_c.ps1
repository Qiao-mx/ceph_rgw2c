# Configure CMake with SAL C enabled
$ErrorActionPreference = "Continue"
Write-Host "Starting CMake configuration..."
Set-Location "d:/NAS/ceph-20.1.1/build"
cmake ../src -DWITH_RGW_SAL_C=ON -G "Visual Studio 17 2022" -A x64
Write-Host "CMake configuration complete. Exit code: $LASTEXITCODE"
