/**
 * @file rgw_sal_c_wrapper.cc
 * @brief SAL C++ 包装器实现
 */

#include "rgw_sal_c_wrapper.h"
#include "rgw_buffer.h"
#include <cstring>

namespace rgw {
namespace sal {

/*============================================================================
 * SalDriver 实现
 *============================================================================*/

std::unique_ptr<SalDriver> SalDriver::create(const std::string& type, void* cct) {
    rgw_sal_driver_t* c_driver = rgw_sal_create_driver(type.c_str(), cct);
    if (!c_driver) {
        return nullptr;
    }
    return std::unique_ptr<SalDriver>(new SalDriver(c_driver));
}

SalDriver::~SalDriver() {
    if (c_driver_) {
        rgw_sal_destroy_driver(c_driver_);
        c_driver_ = nullptr;
    }
}

const char* SalDriver::get_name() const {
    return c_driver_ ? rgw_sal_get_driver_name(c_driver_) : nullptr;
}

int SalDriver::initialize(void* cct, void* dpp) {
    if (!c_driver_) return -1;
    return rgw_sal_init_driver(c_driver_, cct, (const rgw_sal_dpp_t*)dpp);
}

/*============================================================================
 * SalUser 实现
 *============================================================================*/

std::unique_ptr<SalUser> SalUser::create(rgw_sal_driver_t* driver, const std::string& id) {
    if (!driver) return nullptr;

    rgw_sal_user_id_t uid = {};
    uid.id = (char*)id.c_str();

    rgw_sal_user_t* c_user = driver->vtable->get_user(driver, &uid);
    if (!c_user) {
        return nullptr;
    }

    return std::unique_ptr<SalUser>(new SalUser(c_user));
}

SalUser::~SalUser() {
    if (c_user_) {
        if (c_user_->vtable && c_user_->vtable->destroy) {
            c_user_->vtable->destroy(c_user_);
        }
        // 注意：不使用 free()，因为 C 层可能使用不同的分配器
        c_user_ = nullptr;
    }
}

const char* SalUser::get_id() const {
    if (!c_user_ || !c_user_->vtable) return nullptr;
    return c_user_->vtable->get_id(c_user_);
}

const char* SalUser::get_display_name() const {
    if (!c_user_ || !c_user_->vtable) return nullptr;
    return c_user_->vtable->get_display_name(c_user_);
}

int SalUser::set_display_name(const std::string& name) {
    if (!c_user_ || !c_user_->vtable) return -1;
    return c_user_->vtable->set_display_name(c_user_, name.c_str());
}

const char* SalUser::get_tenant() const {
    if (!c_user_ || !c_user_->vtable) return nullptr;
    return c_user_->vtable->get_tenant(c_user_);
}

int SalUser::load(void* dpp, void* y) {
    if (!c_user_ || !c_user_->vtable || !c_user_->vtable->load) return -1;
    return c_user_->vtable->load(c_user_, (const rgw_sal_dpp_t*)dpp, (rgw_sal_yield_t*)y);
}

int SalUser::store(void* dpp, void* y, bool exclusive) {
    if (!c_user_ || !c_user_->vtable || !c_user_->vtable->store) return -1;
    return c_user_->vtable->store(c_user_, (const rgw_sal_dpp_t*)dpp,
                                  (rgw_sal_yield_t*)y, exclusive);
}

int SalUser::remove(void* dpp, void* y) {
    if (!c_user_ || !c_user_->vtable || !c_user_->vtable->remove) return -1;
    return c_user_->vtable->remove(c_user_, (const rgw_sal_dpp_t*)dpp, (rgw_sal_yield_t*)y);
}

/*============================================================================
 * SalBucket 实现
 *============================================================================*/

std::unique_ptr<SalBucket> SalBucket::create(rgw_sal_driver_t* driver,
                                             const std::string& name,
                                             SalUser* owner) {
    if (!driver) return nullptr;

    rgw_sal_bucket_info_t info = {};
    info.bucket.name = (char*)name.c_str();

    rgw_sal_bucket_t* c_bucket = driver->vtable->get_bucket(driver, &info);
    if (!c_bucket) {
        return nullptr;
    }

    return std::unique_ptr<SalBucket>(new SalBucket(c_bucket));
}

SalBucket::~SalBucket() {
    if (c_bucket_) {
        if (c_bucket_->vtable && c_bucket_->vtable->destroy) {
            c_bucket_->vtable->destroy(c_bucket_);
        }
        c_bucket_ = nullptr;
    }
}

const char* SalBucket::get_name() const {
    if (!c_bucket_ || !c_bucket_->vtable) return nullptr;
    return c_bucket_->vtable->get_name(c_bucket_);
}

const char* SalBucket::get_tenant() const {
    if (!c_bucket_ || !c_bucket_->vtable) return nullptr;
    return c_bucket_->vtable->get_tenant(c_bucket_);
}

const char* SalBucket::get_marker() const {
    if (!c_bucket_ || !c_bucket_->vtable) return nullptr;
    return c_bucket_->vtable->get_marker(c_bucket_);
}

const char* SalBucket::get_bucket_id() const {
    if (!c_bucket_ || !c_bucket_->vtable) return nullptr;
    return c_bucket_->vtable->get_bucket_id(c_bucket_);
}

int SalBucket::load(void* dpp, void* y) {
    if (!c_bucket_ || !c_bucket_->vtable || !c_bucket_->vtable->load) return -1;
    return c_bucket_->vtable->load(c_bucket_, (const rgw_sal_dpp_t*)dpp, (rgw_sal_yield_t*)y);
}

int SalBucket::store(void* dpp, void* y, bool exclusive) {
    if (!c_bucket_ || !c_bucket_->vtable || !c_bucket_->vtable->store) return -1;
    return c_bucket_->vtable->store(c_bucket_, (const rgw_sal_dpp_t*)dpp,
                                    (rgw_sal_yield_t*)y, exclusive);
}

int SalBucket::remove(void* dpp, void* y) {
    if (!c_bucket_ || !c_bucket_->vtable || !c_bucket_->vtable->remove) return -1;
    return c_bucket_->vtable->remove(c_bucket_, (const rgw_sal_dpp_t*)dpp, (rgw_sal_yield_t*)y);
}

/*============================================================================
 * SalObject 实现
 *============================================================================*/

std::unique_ptr<SalObject> SalObject::create(SalBucket* bucket, const std::string& name) {
    if (!bucket || !bucket->c_bucket()) return nullptr;

    rgw_sal_obj_key_t key = {};
    key.name = (char*)name.c_str();

    rgw_sal_object_t* c_obj = bucket->c_bucket()->driver->vtable->get_object(
        bucket->c_bucket()->driver, bucket->c_bucket(), &key);

    if (!c_obj) {
        return nullptr;
    }

    return std::unique_ptr<SalObject>(new SalObject(c_obj));
}

SalObject::~SalObject() {
    if (c_object_) {
        if (c_object_->vtable && c_object_->vtable->destroy) {
            c_object_->vtable->destroy(c_object_);
        }
        c_object_ = nullptr;
    }
}

const char* SalObject::get_name() const {
    if (!c_object_ || !c_object_->vtable) return nullptr;
    return c_object_->vtable->get_name(c_object_);
}

const char* SalObject::get_instance() const {
    if (!c_object_ || !c_object_->vtable) return nullptr;
    return c_object_->vtable->get_instance(c_object_);
}

int SalObject::read(void* dpp, void* y, int64_t offset, int64_t size, bufferlist& bl) {
    if (!c_object_ || !c_object_->vtable || !c_object_->vtable->read) return -1;

    std::vector<uint8_t> buffer(size);
    size_t bytes_read = size;

    int ret = c_object_->vtable->read(c_object_, offset, offset + size - 1,
                                       buffer.data(), &bytes_read,
                                       (const rgw_sal_dpp_t*)dpp,
                                       (rgw_sal_yield_t*)y);

    if (ret == 0 && bytes_read > 0) {
        bl.append((const char*)buffer.data(), bytes_read);
    }

    return ret;
}

int SalObject::write(void* dpp, void* y, int64_t offset, bufferlist& bl) {
    if (!c_object_ || !c_object_->vtable || !c_object_->vtable->write) return -1;

    return c_object_->vtable->write(c_object_, offset, bl.length(),
                                      (const uint8_t*)bl.c_str(),
                                      (const rgw_sal_dpp_t*)dpp,
                                      (rgw_sal_yield_t*)y);
}

int SalObject::delete_obj(void* dpp, void* y, uint32_t flags) {
    if (!c_object_ || !c_object_->vtable || !c_object_->vtable->delete_obj) return -1;
    return c_object_->vtable->delete_obj(c_object_, flags,
                                          (const rgw_sal_dpp_t*)dpp,
                                          (rgw_sal_yield_t*)y);
}

} // namespace sal
} // namespace rgw
