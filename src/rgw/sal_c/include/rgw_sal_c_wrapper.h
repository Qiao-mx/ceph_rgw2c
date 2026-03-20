/**
 * @file rgw_sal_c_wrapper.h
 * @brief SAL C++ 包装器接口
 *
 * 提供 C SAL 层到 C++ 的包装，使原 C++ RGW 代码可以调用 C SAL 实现。
 * 使用 extern "C" 接口调用底层 C 函数。
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <optional>

#ifdef __cplusplus
extern "C" {
#endif
#include "rgw_sal.h"
#include "rgw_sal_rados.h"
#include "rgw_sal_dbstore.h"
#ifdef __cplusplus
}
#endif

namespace rgw {
namespace sal {

/**
 * @brief C++ SAL Driver 包装器
 *
 * 将 C SAL driver 包装为 C++ 类
 */
class SalDriver {
public:
    /**
     * @brief 从 C driver 创建包装器
     */
    static std::unique_ptr<SalDriver> create(const std::string& type, void* cct = nullptr);

    /**
     * @brief 析构函数
     */
    ~SalDriver();

    /**
     * @brief 获取驱动名称
     */
    const char* get_name() const;

    /**
     * @brief 初始化驱动
     */
    int initialize(void* cct, void* dpp);

    /**
     * @brief 获取底层 C driver 指针
     */
    rgw_sal_driver_t* c_driver() { return c_driver_; }

private:
    SalDriver(rgw_sal_driver_t* c_driver) : c_driver_(c_driver) {}
    rgw_sal_driver_t* c_driver_ = nullptr;
};

/**
 * @brief C++ SAL User 包装器
 */
class SalUser {
public:
    static std::unique_ptr<SalUser> create(rgw_sal_driver_t* driver, const std::string& id);

    ~SalUser();

    const char* get_id() const;
    const char* get_display_name() const;
    int set_display_name(const std::string& name);
    const char* get_tenant() const;

    int load(void* dpp, void* y);
    int store(void* dpp, void* y, bool exclusive = false);
    int remove(void* dpp, void* y);

    rgw_sal_user_t* c_user() { return c_user_; }

private:
    SalUser(rgw_sal_user_t* c_user) : c_user_(c_user) {}
    rgw_sal_user_t* c_user_ = nullptr;
};

/**
 * @brief C++ SAL Bucket 包装器
 */
class SalBucket {
public:
    static std::unique_ptr<SalBucket> create(rgw_sal_driver_t* driver,
                                              const std::string& name,
                                              SalUser* owner);

    ~SalBucket();

    const char* get_name() const;
    const char* get_tenant() const;
    const char* get_marker() const;
    const char* get_bucket_id() const;

    int load(void* dpp, void* y);
    int store(void* dpp, void* y, bool exclusive = false);
    int remove(void* dpp, void* y);

    rgw_sal_bucket_t* c_bucket() { return c_bucket_; }

private:
    SalBucket(rgw_sal_bucket_t* c_bucket) : c_bucket_(c_bucket) {}
    rgw_sal_bucket_t* c_bucket_ = nullptr;
};

/**
 * @brief C++ SAL Object 包装器
 */
class SalObject {
public:
    static std::unique_ptr<SalObject> create(SalBucket* bucket, const std::string& name);

    ~SalObject();

    const char* get_name() const;
    const char* get_instance() const;

    int read(void* dpp, void* y, int64_t offset, int64_t size, bufferlist& bl);
    int write(void* dpp, void* y, int64_t offset, bufferlist& bl);
    int delete_obj(void* dpp, void* y, uint32_t flags = 0);

    rgw_sal_object_t* c_object() { return c_object_; }

private:
    SalObject(rgw_sal_object_t* c_object) : c_object_(c_object) {}
    rgw_sal_object_t* c_object_ = nullptr;
};

} // namespace sal
} // namespace rgw
