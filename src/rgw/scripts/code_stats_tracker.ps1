# RGW Code Statistics Tracker
# 每日自动统计并保存到历史记录文件

param(
    [switch]$ShowHistory,
    [switch]$ShowTrend
)

$ErrorActionPreference = "Continue"

$ProjectRoot = "D:\NAS\ceph-20.1.1\src\rgw"
$StatsFile = "D:\NAS\ceph-20.1.1\src\rgw\scripts\code_stats_history.csv"
$LogFile = "D:\NAS\ceph-20.1.1\src\rgw\scripts\code_stats.log"

function Get-CodeStats {
    param([string]$Path, [string[]]$Include, [string]$Exclude = "\\build\\")
    
    $files = Get-ChildItem -Path $Path -Include $Include -Recurse -File -ErrorAction SilentlyContinue | 
             Where-Object { $_.FullName -notmatch $Exclude }
    
    $fileCount = $files.Count
    $totalLines = ($files | Get-Content | Measure-Object -Line).Lines
    
    return @($fileCount, $totalLines)
}

function Get-DetailedStats {
    # RGW C++ Source
    $rgwStats = Get-CodeStats -Path $ProjectRoot -Include "*.cc", "*.h"
    
    # sal_c layer
    $sal_c_src = Get-CodeStats -Path "$ProjectRoot\sal_c\src" -Include "*.c", "*.cc"
    $sal_c_inc = Get-CodeStats -Path "$ProjectRoot\sal_c\include" -Include "*.h"
    
    # c_common layer
    $c_src = Get-CodeStats -Path "$ProjectRoot\c_common\src" -Include "*.c"
    $c_cont = Get-CodeStats -Path "$ProjectRoot\c_common\containers" -Include "*.c"
    $c_inc = Get-CodeStats -Path "$ProjectRoot\c_common\include" -Include "*.h"
    
    # C implementation total (source code only)
    $cTotalLines = $sal_c_src[1] + $c_src[1] + $c_cont[1]
    $cTotalFiles = $sal_c_src[0] + $c_src[0] + $c_cont[0]
    
    # Progress calculation
    $cppLines = $rgwStats[1]
    if ($cppLines -and $cppLines -gt 0) {
        $progress = [math]::Round(($cTotalLines / $cppLines) * 100, 2)
    } else {
        $progress = 0
    }
    
    return @{
        Date = Get-Date -Format 'yyyy-MM-dd'
        Time = Get-Date -Format 'HH:mm:ss'
        RGW_Files = $rgwStats[0]
        RGW_Lines = $rgwStats[1]
        SAL_C_Files = $sal_c_src[0] + $sal_c_inc[0]
        SAL_C_Lines = $sal_c_src[1] + $sal_c_inc[1]
        SAL_C_SrcLines = $sal_c_src[1]
        C_COMMON_Files = $c_src[0] + $c_cont[0] + $c_inc[0]
        C_COMMON_Lines = $c_src[1] + $c_cont[1] + $c_inc[1]
        C_COMMON_SrcLines = $c_src[1]
        C_COMMON_ContLines = $c_cont[1]
        C_Total_Files = $cTotalFiles
        C_Total_Lines = $cTotalLines
        Progress = $progress
    }
}

function Show-Report {
    param($Stats)
    
    Write-Host ""
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host "  RGW C++ to C Migration Statistics" -ForegroundColor Cyan
    Write-Host "  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host ""
    
    Write-Host "  [1] RGW C++ Original Source" -ForegroundColor Yellow
    Write-Host "      Files: $($Stats.RGW_Files)"
    Write-Host "      Lines: $($Stats.RGW_Lines)"
    Write-Host ""
    
    Write-Host "  [2] sal_c Layer (C/C++ code)" -ForegroundColor Yellow
    Write-Host "      Files: $($Stats.SAL_C_Files)"
    Write-Host "      Lines: $($Stats.SAL_C_Lines)"
    Write-Host "      (Source only: $($Stats.SAL_C_SrcLines))"
    Write-Host ""
    
    Write-Host "  [3] c_common Layer (C code)" -ForegroundColor Yellow
    Write-Host "      Files: $($Stats.C_COMMON_Files)"
    Write-Host "      Lines: $($Stats.C_COMMON_Lines)"
    Write-Host "      (Source: $($Stats.C_COMMON_SrcLines), Containers: $($Stats.C_COMMON_ContLines))"
    Write-Host ""
    
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host "  Progress Summary" -ForegroundColor Yellow
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "  C Implementation (sal_c + c_common src):"
    Write-Host "    Files: $($Stats.C_Total_Files)"
    Write-Host "    Lines: $($Stats.C_Total_Lines)"
    Write-Host ""
    Write-Host "  C++ Original: $($Stats.RGW_Lines) lines"
    Write-Host ""
    Write-Host "  Migration Progress: " -NoNewline
    Write-Host "$($Stats.Progress)%" -ForegroundColor Green
    Write-Host ""
    
    # Progress bar
    $barLength = 50
    $filledLength = [math]::Floor(($Stats.Progress / 100) * $barLength)
    $emptyLength = $barLength - $filledLength
    $bar = "[" + ("=" * $filledLength) + (" " * $emptyLength) + "]"
    Write-Host "  $bar"
    Write-Host ""
}

function Save-Stats {
    param($Stats)
    
    # Create header if file doesn't exist
    if (-not (Test-Path $StatsFile)) {
        $header = "Date,Time,RGW_Files,RGW_Lines,SAL_C_Files,SAL_C_Lines,SAL_C_SrcLines,C_COMMON_Files,C_COMMON_Lines,C_COMMON_SrcLines,C_COMMON_ContLines,C_Total_Files,C_Total_Lines,Progress"
        $header | Out-File -FilePath $StatsFile -Encoding UTF8
    }
    
    $line = "$($Stats.Date),$($Stats.Time),$($Stats.RGW_Files),$($Stats.RGW_Lines),$($Stats.SAL_C_Files),$($Stats.SAL_C_Lines),$($Stats.SAL_C_SrcLines),$($Stats.C_COMMON_Files),$($Stats.C_COMMON_Lines),$($Stats.C_COMMON_SrcLines),$($Stats.C_COMMON_ContLines),$($Stats.C_Total_Files),$($Stats.C_Total_Lines),$($Stats.Progress)"
    Add-Content -Path $StatsFile -Value $line
    
    # Also log to log file
    $logEntry = "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') - RGW Lines: $($Stats.RGW_Lines), C Lines: $($Stats.C_Total_Lines), Progress: $($Stats.Progress)%"
    Add-Content -Path $LogFile -Value $logEntry
}

function Show-History {
    if (Test-Path $StatsFile) {
        Write-Host ""
        Write-Host "============================================================" -ForegroundColor Cyan
        Write-Host "  Historical Statistics" -ForegroundColor Cyan
        Write-Host "============================================================" -ForegroundColor Cyan
        Write-Host ""
        
        $data = Import-Csv -Path $StatsFile
        $data | Format-Table -AutoSize
    } else {
        Write-Host "No history data found. Run the script without flags to collect data." -ForegroundColor Yellow
    }
}

function Show-Trend {
    if (Test-Path $StatsFile) {
        Write-Host ""
        Write-Host "============================================================" -ForegroundColor Cyan
        Write-Host "  Progress Trend" -ForegroundColor Cyan
        Write-Host "============================================================" -ForegroundColor Cyan
        Write-Host ""
        
        $data = Import-Csv -Path $StatsFile | Select-Object Date, RGW_Lines, C_Total_Lines, Progress
        
        Write-Host "  Date       | RGW Lines | C Lines | Progress"
        Write-Host "  " + ("-" * 45)
        
        foreach ($row in $data) {
            Write-Host "  $($row.Date) | $($row.RGW_Lines.PadLeft(9)) | $($row.C_Total_Lines.PadLeft(7)) | $($row.Progress)%"
        }
        
        if ($data.Count -gt 1) {
            Write-Host ""
            Write-Host "  Trend Analysis:" -ForegroundColor Yellow
            
            $first = $data[0]
            $last = $data[-1]
            
            $cDiff = [int]$last.C_Total_Lines - [int]$first.C_Total_Lines
            $pDiff = [float]$last.Progress - [float]$first.Progress
            
            if ($cDiff -gt 0) {
                Write-Host "    C code increased by $cDiff lines" -ForegroundColor Green
            } elseif ($cDiff -lt 0) {
                Write-Host "    C code decreased by $([math]::Abs($cDiff)) lines" -ForegroundColor Red
            }
            
            if ($pDiff -gt 0) {
                Write-Host "    Progress increased by $pDiff%" -ForegroundColor Green
            }
        }
    } else {
        Write-Host "No trend data available." -ForegroundColor Yellow
    }
}

# Main logic
if ($ShowHistory) {
    Show-History
} elseif ($ShowTrend) {
    Show-Trend
} else {
    $stats = Get-DetailedStats
    Show-Report -Stats $stats
    Save-Stats -Stats $stats
    
    Write-Host "  Statistics saved to: $StatsFile" -ForegroundColor Green
    Write-Host "  Log saved to: $LogFile" -ForegroundColor Green
}
