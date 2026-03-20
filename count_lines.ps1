$files = @(
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_sqlite.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_xml.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_rados_obj.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_rados_object.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_rados_bucket.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_rados_user.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_bucket_serde.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_user_serde.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_omap.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_rados_ctx.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_rgw_xml.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_benchmark.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_buffer.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_buffer.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_hex.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_cstack.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_b64.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_b64.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_cqueue.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_cpriority_queue.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_hex.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_oop.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_cstring.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_cpp_to_c.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_cqueue.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\containers\rgw_chash_map.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\csort.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_errors.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_carray.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_ccontainer_edge_cases.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_cstring.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_cdeque.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_oop.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_carray.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_cpriority_queue.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_comprehensive.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_ccontainer.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_coptional.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_cdeque.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_errors.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_cstack.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\containers\rgw_clist.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_cmemory.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_coptional.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\containers\rgw_splaytree.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\src\rgw_cmap.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\containers\rgw_rbtree.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\containers\rgw_bst.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\containers\rgw_avl.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\containers\rgw_cset.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_cset.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_cmap.c",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\tests\test_memory.c"
)

$total_lines = 0

foreach ($file in $files) {
    if (Test-Path $file) {
        $lines = (Get-Content $file | Measure-Object -Line).Lines
        $total_lines += $lines
    }
}

Write-Output "Total lines in .c files: $total_lines"
