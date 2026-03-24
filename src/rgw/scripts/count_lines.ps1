# RGW Code Statistics Script
# Usage: powershell -ExecutionPolicy Bypass -File count_lines.ps1

param(
    [switch]$Json
)

$ErrorActionPreference = "Continue"

$ProjectRoot = "D:\NAS\ceph-20.1.1\src\rgw"

function Get-CodeStats {
    param([string]$Path, [string[]]$Include, [string]$Exclude = "\\build\\")
    
    $files = Get-ChildItem -Path $Path -Include $Include -Recurse -File -ErrorAction SilentlyContinue | 
             Where-Object { $_.FullName -notmatch $Exclude }
    
    $fileCount = $files.Count
    $totalLines = ($files | Get-Content | Measure-Object -Line).Lines
    
    return @($fileCount, $totalLines)
}

Write-Host "============================================================"
Write-Host "RGW Code Statistics Report"
Write-Host "============================================================"
Write-Host ""

# RGW C++ Source
$rgwStats = Get-CodeStats -Path $ProjectRoot -Include "*.cc", "*.h"
Write-Host "[1] RGW C++ Source Files"
Write-Host "    Files: $($rgwStats[0])"
Write-Host "    Lines: $($rgwStats[1])"
Write-Host ""

# sal_c layer
$sal_c_src = Get-CodeStats -Path "$ProjectRoot\sal_c\src" -Include "*.c", "*.cc"
$sal_c_inc = Get-CodeStats -Path "$ProjectRoot\sal_c\include" -Include "*.h"
Write-Host "[2] sal_c Layer"
Write-Host "    src files: $($sal_c_src[0]), lines: $($sal_c_src[1])"
Write-Host "    include files: $($sal_c_inc[0]), lines: $($sal_c_inc[1])"
Write-Host "    Total: $($sal_c_src[0] + $sal_c_inc[0]) files, $($sal_c_src[1] + $sal_c_inc[1]) lines"
Write-Host ""

# c_common layer
$c_src = Get-CodeStats -Path "$ProjectRoot\c_common\src" -Include "*.c"
$c_cont = Get-CodeStats -Path "$ProjectRoot\c_common\containers" -Include "*.c"
$c_inc = Get-CodeStats -Path "$ProjectRoot\c_common\include" -Include "*.h"
Write-Host "[3] c_common Layer"
Write-Host "    src files: $($c_src[0]), lines: $($c_src[1])"
Write-Host "    containers files: $($c_cont[0]), lines: $($c_cont[1])"
Write-Host "    include files: $($c_inc[0]), lines: $($c_inc[1])"
Write-Host "    Total: $($c_src[0] + $c_cont[0] + $c_inc[0]) files, $($c_src[1] + $c_cont[1] + $c_inc[1]) lines"
Write-Host ""

# Progress calculation
$cTotalLines = $sal_c_src[1] + $c_src[1] + $c_cont[1]
$cppLines = $rgwStats[1]
if ($cppLines -and $cppLines -gt 0) {
    $progress = [math]::Round(($cTotalLines / $cppLines) * 100, 2)
} else {
    $progress = 0
}

Write-Host "============================================================"
Write-Host "Progress Summary"
Write-Host "============================================================"
Write-Host "    C Implementation: $cTotalLines lines"
Write-Host "    C++ Original: $cppLines lines"
Write-Host "    Progress: $progress%"
Write-Host ""
Write-Host "Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"

if ($Json) {
    $result = @{
        Date = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
        RGW_CPP = @{ Files = $rgwStats[0]; Lines = $rgwStats[1] }
        SAL_C = @{ Files = $sal_c_src[0] + $sal_c_inc[0]; Lines = $sal_c_src[1] + $sal_c_inc[1] }
        C_COMMON = @{ Files = $c_src[0] + $c_cont[0] + $c_inc[0]; Lines = $c_src[1] + $c_cont[1] + $c_inc[1] }
        Progress = @{ CLines = $cTotalLines; CPPLines = $cppLines; Percent = $progress }
    }
    ConvertTo-Json -InputObject $result -Depth 3
}
