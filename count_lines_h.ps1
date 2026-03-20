$files = @(
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\csort.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\rgw_b64.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\rgw_bucket_serde.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\rgw_buffer.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\rgw_ccommon.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\rgw_errors.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\rgw_hex.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\rgw_omap.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\rgw_oop.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\rgw_rados_ctx.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\rgw_rados_object.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\rgw_sal.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\rgw_sqlite.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\rgw_user_serde.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\rgw_xml.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\containers\rgw_carray.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\containers\rgw_cdeque.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\containers\rgw_chash_map.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\containers\rgw_clist.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\containers\rgw_cmap.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\containers\rgw_cmemory.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\containers\rgw_coptional.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\containers\rgw_cpriority_queue.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\containers\rgw_cqueue.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\containers\rgw_cset.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\containers\rgw_cstack.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\containers\rgw_cstring.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\internal\rgw_avltree.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\internal\rgw_list.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\internal\rgw_rbt_node.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\internal\rgw_rbtree.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\internal\utarray.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\internal\uthash.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\internal\utlist.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\internal\utringbuffer.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\internal\utstack.h",
    "d:\NAS\ceph-20.1.1\src\rgw\c_common\include\internal\utstring.h"
)

$total_lines = 0

foreach ($file in $files) {
    if (Test-Path $file) {
        $lines = (Get-Content $file | Measure-Object -Line).Lines
        $total_lines += $lines
    }
}

Write-Output "Total lines in .h files: $total_lines"
