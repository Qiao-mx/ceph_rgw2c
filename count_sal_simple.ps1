$files = @(
    "d:\NAS\ceph-20.1.1\src\rgw\rgw_sal.cc",
    "d:\NAS\ceph-20.1.1\src\rgw\rgw_sal.h",
    "d:\NAS\ceph-20.1.1\src\rgw\rgw_sal_store.h",
    "d:\NAS\ceph-20.1.1\src\rgw\rgw_sal_fwd.h",
    "d:\NAS\ceph-20.1.1\src\rgw\rgw_sal_config.h",
    "d:\NAS\ceph-20.1.1\src\rgw\rgw_sal_filter.cc",
    "d:\NAS\ceph-20.1.1\src\rgw\rgw_sal_filter.h",
    "d:\NAS\ceph-20.1.1\src\rgw\rgw_sal_dbstore.cc",
    "d:\NAS\ceph-20.1.1\src\rgw\rgw_sal_dbstore.h",
    "d:\NAS\ceph-20.1.1\src\rgw\driver\d4n\rgw_sal_d4n.cc",
    "d:\NAS\ceph-20.1.1\src\rgw\driver\d4n\rgw_sal_d4n.h",
    "d:\NAS\ceph-20.1.1\src\rgw\driver\daos\rgw_sal_daos.cc",
    "d:\NAS\ceph-20.1.1\src\rgw\driver\daos\rgw_sal_daos.h",
    "d:\NAS\ceph-20.1.1\src\rgw\driver\motr\rgw_sal_motr.cc",
    "d:\NAS\ceph-20.1.1\src\rgw\driver\motr\rgw_sal_motr.h",
    "d:\NAS\ceph-20.1.1\src\rgw\driver\posix\rgw_sal_posix.cc",
    "d:\NAS\ceph-20.1.1\src\rgw\driver\posix\rgw_sal_posix.h",
    "d:\NAS\ceph-20.1.1\src\rgw\driver\rados\rgw_sal_rados.cc",
    "d:\NAS\ceph-20.1.1\src\rgw\driver\rados\rgw_sal_rados.h",
    "d:\NAS\ceph-20.1.1\src\rgw\rgw_sal_c_bridge.cc"
)

$count = 0
$lines = 0

foreach ($file in $files) {
    if (Test-Path $file) {
        $count++
        $file_lines = Get-Content $file | Measure-Object -Line
        $lines += $file_lines.Lines
        Write-Output "$file : $($file_lines.Lines) lines"
    } else {
        Write-Output "$file : NOT FOUND"
    }
}

Write-Output ""
Write-Output "Total files: $count"
Write-Output "Total lines: $lines"
