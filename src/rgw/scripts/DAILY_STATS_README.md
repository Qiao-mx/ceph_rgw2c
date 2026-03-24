# RGW 代码统计 - 每日任务设置说明
# ======================================

## 快速开始

### 方法 1: 手动运行
```powershell
powershell -ExecutionPolicy Bypass -File "D:\NAS\ceph-20.1.1\src\rgw\scripts\code_stats_tracker.ps1"
```

### 方法 2: 查看历史
```powershell
# 查看所有历史记录
powershell -ExecutionPolicy Bypass -File "D:\NAS\ceph-20.1.1\src\rgw\scripts\code_stats_tracker.ps1" -ShowHistory

# 查看趋势分析
powershell -ExecutionPolicy Bypass -File "D:\NAS\ceph-20.1.1\src\rgw\scripts\code_stats_tracker.ps1" -ShowTrend
```

### 方法 3: 设置 Windows 计划任务 (每日自动统计)

1. 打开 PowerShell (管理员)
2. 运行以下命令创建计划任务:

```powershell
$action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument "-ExecutionPolicy Bypass -File `"D:\NAS\ceph-20.1.1\src\rgw\scripts\code_stats_tracker.ps1`""
$trigger = New-ScheduledTaskTrigger -Daily -At "09:00"
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -Action $action -Trigger $trigger -TaskName "RGW Code Statistics" -Description "每日统计 RGW 代码行数" -Settings $settings
```

3. 查看计划任务:
```powershell
Get-ScheduledTask -TaskName "RGW Code Statistics"
```

4. 删除计划任务 (如需):
```powershell
Unregister-ScheduledTask -TaskName "RGW Code Statistics" -Confirm:$false
```

## 输出文件

- `code_stats_history.csv` - 历史统计数据 (CSV格式)
- `code_stats.log` - 运行日志

## 统计数据说明

- **RGW C++ Original Source**: RGW 目录下的所有 .cc 和 .h 文件
- **sal_c Layer**: C 实现的 SAL (Storage Abstraction Layer) 层
- **c_common Layer**: C 实现的通用数据结构层
- **Progress**: C 实现代码行数 / C++ 原代码行数

## 分析 C++ 模块对应的 C 实现

由于 C++ 代码和 C 实现代码不是简单的一对一对应关系，建议按以下方式分析:

### 模块对应关系分析

| C++ 模块 | C 实现文件 | 说明 |
|---------|----------|------|
| rgw_sal*.h | rgw_sal.c, rgw_sal_types.c | SAL 核心 |
| rgw_bucket*.h | rgw_rados_bucket.c, rgw_bucket_serde.c | Bucket |
| rgw_user*.h | rgw_rados_user.c, rgw_user_serde.c | User |
| rgw_obj*.h | rgw_rados_obj.c, rgw_rados_object.c | Object |
| rgw_acl*.h | rgw_acl_serde.c | ACL |
| rgw_policy*.h | rgw_policy_serde.c | Policy |
| std::vector | rgw_carray.c | 动态数组 |
| std::string | rgw_cstring.c | 字符串 |
| std::map | rgw_cmap.c | 有序Map |
| std::set | rgw_cset.c | 有序Set |
| std::list | rgw_clist.c | 双向链表 |
| std::deque | rgw_cdeque.c | 双端队列 |
| std::stack | rgw_cstack.c | 栈 |
| std::queue | rgw_cqueue.c | 队列 |
| std::priority_queue | rgw_cpriority_queue.c | 优先队列 |
| std::unordered_map | rgw_chash_map.c | 哈希表 |
| std::optional | rgw_coptional.c | 可选类型 |
