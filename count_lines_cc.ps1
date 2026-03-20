$files = @(
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\examples\test_c_wrapper.cpp",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\cpp_test.cc"
)

$total_lines = 0

foreach ($file in $files) {
    if (Test-Path $file) {
        $lines = (Get-Content $file | Measure-Object -Line).Lines
        $total_lines += $lines
    }
}

Write-Output "Total lines in .cc/.cpp files: $total_lines"
