#include <iostream>
#include <map>
#include <list>
#include <deque>
#include <set>
#include <string>
#include <algorithm>  // 算法库（排序、查找）
#include <mutex>      // 线程安全锁
#include <stdint.h>   // 固定宽度整数类型

// 定义业务结构体：模拟文件句柄（对应实际项目中的核心数据结构）
struct FileHandle {
    uint64_t fh_id;          // 文件句柄ID
    std::string path;        // 文件路径
    uint32_t size;           // 文件大小
    bool is_readonly;        // 是否只读

    // 构造函数（初始化）
    FileHandle(uint64_t id, const std::string& p, uint32_t s, bool ro)
        : fh_id(id), path(p), size(s), is_readonly(ro) {}

    // 默认构造函数
    FileHandle() : fh_id(0), path(""), size(0), is_readonly(false) {}

    // 重载 < 运算符（用于set/map的排序）
    bool operator<(const FileHandle& other) const {
        return fh_id < other.fh_id;  // 按ID升序排序
    }

    // 重载 == 运算符（用于查找）
    bool operator==(const FileHandle& other) const {
        return fh_id == other.fh_id;
    }

    // 打印文件句柄信息（辅助函数）
    void print() const {
        std::cout << "ID: " << fh_id 
                  << ", Path: " << path 
                  << ", Size: " << size 
                  << ", Readonly: " << (is_readonly ? "Yes" : "No") 
                  << std::endl;
    }
};

// 全局互斥锁（模拟线程安全场景）
std::mutex g_container_mutex;

// ===================== 容器操作封装类（模拟实际项目的工具类） =====================
class ContainerManager {
private:
    // 1. std::list：存储无序、频繁插入/删除的文件句柄（双向链表）
    std::list<FileHandle> fh_list;

    // 2. std::set：存储唯一、有序的文件句柄（红黑树实现，自动去重）
    std::set<FileHandle> fh_set;

    // 3. std::map：键值对存储（key=文件ID，value=文件句柄，快速查找）
    std::map<uint64_t, FileHandle> fh_map;

    // 4. std::deque：双端队列（头尾插入/删除高效，适合缓存场景）
    std::deque<FileHandle> fh_deque;

public:
    // ========== std::list 操作 ==========
    void add_to_list(const FileHandle& fh) {
        std::lock_guard<std::mutex> lock(g_container_mutex);  // 线程安全
        fh_list.push_back(fh);  // 尾部插入
        std::cout << "[List] 添加元素: " << fh.fh_id << std::endl;
    }

    void remove_from_list(uint64_t fh_id) {
        std::lock_guard<std::mutex> lock(g_container_mutex);
        // 遍历查找并删除
        for (auto it = fh_list.begin(); it != fh_list.end(); ++it) {
            if (it->fh_id == fh_id) {
                fh_list.erase(it);
                std::cout << "[List] 删除元素: " << fh_id << std::endl;
                break;
            }
        }
    }

    void traverse_list() const {
        std::lock_guard<std::mutex> lock(g_container_mutex);
        std::cout << "\n[List] 遍历所有元素:" << std::endl;
        for (const auto& fh : fh_list) {  // 范围for遍历
            fh.print();
        }
    }

    // ========== std::set 操作 ==========
    bool add_to_set(const FileHandle& fh) {
        std::lock_guard<std::mutex> lock(g_container_mutex);
        // insert返回pair<迭代器, bool>，bool表示是否插入成功（去重）
        auto [it, success] = fh_set.insert(fh);
        if (success) {
            std::cout << "[Set] 添加元素: " << fh.fh_id << std::endl;
        } else {
            std::cout << "[Set] 元素已存在: " << fh.fh_id << std::endl;
        }
        return success;
    }

    FileHandle* find_in_set(uint64_t fh_id) {
        std::lock_guard<std::mutex> lock(g_container_mutex);
        // 构造临时对象用于查找
        FileHandle tmp(fh_id, "", 0, false);
        auto it = fh_set.find(tmp);
        if (it != fh_set.end()) {
            return const_cast<FileHandle*>(&(*it));  // 注意：set元素只读，此处仅演示
        }
        return nullptr;
    }

    void traverse_set() const {
        std::lock_guard<std::mutex> lock(g_container_mutex);
        std::cout << "\n[Set] 遍历所有元素（自动排序）:" << std::endl;
        for (const auto& fh : fh_set) {
            fh.print();
        }
    }

    // ========== std::map 操作 ==========
    void add_to_map(const FileHandle& fh) {
        std::lock_guard<std::mutex> lock(g_container_mutex);
        // 直接赋值（存在则覆盖，不存在则插入）
        fh_map[fh.fh_id] = fh;
        std::cout << "[Map] 添加/更新元素: " << fh.fh_id << std::endl;
    }

    bool remove_from_map(uint64_t fh_id) {
        std::lock_guard<std::mutex> lock(g_container_mutex);
        auto it = fh_map.find(fh_id);
        if (it != fh_map.end()) {
            fh_map.erase(it);
            std::cout << "[Map] 删除元素: " << fh_id << std::endl;
            return true;
        }
        std::cout << "[Map] 元素不存在: " << fh_id << std::endl;
        return false;
    }

    void traverse_map() const {
        std::lock_guard<std::mutex> lock(g_container_mutex);
        std::cout << "\n[Map] 遍历所有元素（按键排序）:" << std::endl;
        for (const auto& pair : fh_map) {  // pair<key, value>
            std::cout << "Key: " << pair.first << " -> ";
            pair.second.print();
        }
    }

    // ========== std::deque 操作 ==========
    void add_to_deque_front(const FileHandle& fh) {
        std::lock_guard<std::mutex> lock(g_container_mutex);
        fh_deque.push_front(fh);  // 头部插入
        std::cout << "[Deque] 头部添加元素: " << fh.fh_id << std::endl;
    }

    void add_to_deque_back(const FileHandle& fh) {
        std::lock_guard<std::mutex> lock(g_container_mutex);
        fh_deque.push_back(fh);   // 尾部插入
        std::cout << "[Deque] 尾部添加元素: " << fh.fh_id << std::endl;
    }

    FileHandle get_deque_front() const {
        std::lock_guard<std::mutex> lock(g_container_mutex);
        if (!fh_deque.empty()) {
            return fh_deque.front();  // 获取头部元素
        }
        return FileHandle(0, "", 0, false);  // 空对象
    }

    void traverse_deque() const {
        std::lock_guard<std::mutex> lock(g_container_mutex);
        std::cout << "\n[Deque] 遍历所有元素:" << std::endl;
        for (size_t i = 0; i < fh_deque.size(); ++i) {  // 下标遍历
            std::cout << "Index " << i << ": ";
            fh_deque[i].print();
        }
    }

    // ========== 通用算法示例（排序、查找） ==========
    void sort_deque_by_size() {
        std::lock_guard<std::mutex> lock(g_container_mutex);
        // 按文件大小降序排序（自定义比较函数）
        std::sort(fh_deque.begin(), fh_deque.end(),
            [](const FileHandle& a, const FileHandle& b) {
                return a.size > b.size;
            });
        std::cout << "\n[Deque] 按大小排序完成" << std::endl;
    }

    FileHandle* find_in_list_by_path(const std::string& path) {
        std::lock_guard<std::mutex> lock(g_container_mutex);
        // 查找第一个匹配路径的元素
        auto it = std::find_if(fh_list.begin(), fh_list.end(),
            [&path](const FileHandle& fh) {
                return fh.path == path;
            });
        if (it != fh_list.end()) {
            return const_cast<FileHandle*>(&(*it));
        }
        return nullptr;
    }
};

// ===================== 主函数：测试所有容器功能 =====================
int main() {
    ContainerManager manager;

    // 构造测试数据
    FileHandle fh1(101, "/data/file1.txt", 1024, false);
    FileHandle fh2(102, "/data/file2.txt", 2048, true);
    FileHandle fh3(103, "/data/file3.txt", 512, false);
    FileHandle fh4(102, "/data/duplicate.txt", 1536, true);  // ID重复（用于set去重）

    // 1. 测试 std::list
    manager.add_to_list(fh1);
    manager.add_to_list(fh2);
    manager.add_to_list(fh3);
    manager.traverse_list();
    manager.remove_from_list(102);
    manager.traverse_list();

    // 2. 测试 std::set（自动去重+排序）
    manager.add_to_set(fh1);
    manager.add_to_set(fh2);
    manager.add_to_set(fh4);  // ID重复，插入失败
    manager.traverse_set();
    FileHandle* found_set = manager.find_in_set(101);
    if (found_set) {
        std::cout << "\n[Set] 找到元素: ";
        found_set->print();
    }

    // 3. 测试 std::map
    manager.add_to_map(fh1);
    manager.add_to_map(fh2);
    manager.add_to_map(fh3);
    manager.traverse_map();
    manager.remove_from_map(103);
    manager.traverse_map();

    // 4. 测试 std::deque
    manager.add_to_deque_back(fh1);
    manager.add_to_deque_front(fh2);
    manager.add_to_deque_back(fh3);
    manager.traverse_deque();
    manager.sort_deque_by_size();
    manager.traverse_deque();
    FileHandle front_deque = manager.get_deque_front();
    std::cout << "\n[Deque] 头部元素: ";
    front_deque.print();

    // 5. 测试通用算法（find_if）
    FileHandle* found_list = manager.find_in_list_by_path("/data/file1.txt");
    if (found_list) {
        std::cout << "\n[List] 按路径找到元素: ";
        found_list->print();
    }

    return 0;
}