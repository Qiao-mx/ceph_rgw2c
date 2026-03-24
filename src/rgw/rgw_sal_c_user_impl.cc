/**
 * @file rgw_sal_c_user_impl.cc
 * @brief C++ User 到 C SAL 的桥接实现
 *
 * 实现继承自 C++ User 基类的包装器，将 C SAL 数据暴露给 C++ RGW 代码。
 */

#include "rgw_sal_c_bridge.h"

#ifdef WITH_RGW_SAL_C

#include <memory>
#include <string>
#include <cstring>

#include "rgw_sal_c_wrapper.h"
#include "rgw_sal.h"
#include "rgw_user.h"
#include "rgw_common.h"

#define dout_subsys ceph_subsys_rgw

namespace rgw::sal {

/*============================================================================
 * SalCUser - C++ User 抽象类的 C SAL 实现
 *============================================================================*/

/**
 * @brief 从 C SAL 用户创建 C++ User 包装器
 */
class SalCUser : public User {
private:
    rgw_sal_driver_t* driver_;  // 拥有者驱动
    rgw_sal_user_t* c_user_;   // 底层 C 用户
    RGWUserInfo info_;          // 缓存的用户信息

public:
    /**
     * @brief 从 rgw_user 创建用户
     */
    static std::unique_ptr<User> create(rgw_sal_driver_t* driver, const rgw_user& uid) {
        auto user = std::make_unique<SalCUser>(driver);

        // 转换 rgw_user 到 C 结构
        rgw_sal_user_id_t c_uid = {};
        c_uid.id = (char*)uid.id.c_str();
        c_uid.tenant = (char*)uid.tenant.c_str();
        c_uid.ns = (char*)uid.ns.c_str();
        c_uid.type = uid.type;

        // 调用 C 层获取用户
        user->c_user_ = driver->vtable->get_user(driver, &c_uid);
        if (!user->c_user_) {
            return nullptr;
        }

        // 填充基本信息到 C++ 结构
        user->info_.user_id = uid;

        return user;
    }

    /**
     * @brief 从已有 C 用户创建包装器
     */
    static std::unique_ptr<User> create_from_c(rgw_sal_driver_t* driver, rgw_sal_user_t* c_user) {
        auto user = std::make_unique<SalCUser>(driver);
        user->c_user_ = c_user;

        // 如果有 vtable，尝试加载信息
        if (c_user && c_user->vtable && c_user->vtable->get_id) {
            const char* id = c_user->vtable->get_id(c_user);
            if (id) {
                user->info_.user_id.id = id;
            }
        }

        return user;
    }

    virtual ~SalCUser() {
        if (c_user_) {
            if (c_user_->vtable && c_user_->vtable->destroy) {
                c_user_->vtable->destroy(c_user_);
            }
            c_user_ = nullptr;
        }
    }

    std::unique_ptr<User> clone() override {
        if (!c_user_ || !c_user_->vtable || !c_user_->vtable->clone) {
            return nullptr;
        }
        rgw_sal_user_t* cloned = (rgw_sal_user_t*)c_user_->vtable->clone(c_user_);
        if (!cloned) {
            return nullptr;
        }
        return create_from_c(driver_, cloned);
    }

    std::string& get_display_name() override {
        static std::string empty;
        if (!c_user_ || !c_user_->vtable || !c_user_->vtable->get_display_name) {
            return empty;
        }
        const char* name = c_user_->vtable->get_display_name(c_user_);
        if (name) {
            info_.display_name = name;
        }
        return info_.display_name;
    }

    const std::string& get_tenant() override {
        return info_.user_id.tenant;
    }

    void set_tenant(std::string& _t) override {
        info_.user_id.tenant = _t;
        // TODO: 更新 C 层
    }

    const std::string& get_ns() override {
        return info_.user_id.ns;
    }

    void set_ns(std::string& _ns) override {
        info_.user_id.ns = _ns;
    }

    void clear_ns() override {
        info_.user_id.ns.clear();
    }

    const rgw_user& get_id() const override {
        return info_.user_id;
    }

    uint32_t get_type() const override {
        return info_.user_id.type;
    }

    int32_t get_max_buckets() const override {
        return info_.max_buckets;
    }

    void set_max_buckets(int32_t _max_buckets) override {
        info_.max_buckets = _max_buckets;
        // TODO: 更新 C 层
    }

    void set_info(RGWQuotaInfo& _quota) override {
        info_.quota = _quota;
    }

    const RGWUserCaps& get_caps() const override {
        return info_.caps;
    }

    RGWObjVersionTracker& get_version_tracker() override {
        // TODO: 需要从 C 层获取
        static RGWObjVersionTracker tracker;
        return tracker;
    }

    Attrs& get_attrs() override {
        // TODO: 从 C 层获取属性
        static Attrs empty_attrs;
        return empty_attrs;
    }

    void set_attrs(Attrs& _attrs) override {
        // TODO: 更新 C 层
    }

    bool empty() const override {
        return info_.user_id.id.empty();
    }

    int read_attrs(const DoutPrefixProvider* dpp, optional_yield y) override {
        if (!c_user_ || !c_user_->vtable || !c_user_->vtable->read_attrs) {
            return -EINVAL;
        }
        return c_user_->vtable->read_attrs(c_user_, (const rgw_sal_dpp_t*)dpp,
                                            (rgw_sal_yield_t*)&y);
    }

    int merge_and_store_attrs(const DoutPrefixProvider* dpp, Attrs& new_attrs, optional_yield y) override {
        if (!c_user_ || !c_user_->vtable || !c_user_->vtable->merge_and_store_attrs) {
            return -EINVAL;
        }
        // TODO: 需要将 Attrs 转换为 C 格式
        return -ENOTSUP;
    }

    int read_usage(const DoutPrefixProvider* dpp, uint64_t start_epoch,
                   uint64_t end_epoch, uint32_t max_entries,
                   bool* is_truncated, RGWUsageIter& usage_iter,
                   std::map<rgw_user_bucket, rgw_usage_log_entry>& usage) override {
        // TODO: 实现
        return -ENOTSUP;
    }

    int trim_usage(const DoutPrefixProvider* dpp, uint64_t start_epoch, uint64_t end_epoch, optional_yield y) override {
        return -ENOTSUP;
    }

    int load_user(const DoutPrefixProvider* dpp, optional_yield y) override {
        if (!c_user_ || !c_user_->vtable || !c_user_->vtable->load) {
            return -EINVAL;
        }
        int ret = c_user_->vtable->load(c_user_, (const rgw_sal_dpp_t*)dpp,
                                         (rgw_sal_yield_t*)&y);
        if (ret == 0) {
            // 加载成功后，更新 C++ 信息
            if (c_user_->vtable->get_display_name) {
                const char* dn = c_user_->vtable->get_display_name(c_user_);
                if (dn) info_.display_name = dn;
            }
        }
        return ret;
    }

    int store_user(const DoutPrefixProvider* dpp, optional_yield y, bool exclusive, RGWUserInfo* old_info) override {
        if (!c_user_ || !c_user_->vtable || !c_user_->vtable->store) {
            return -EINVAL;
        }
        // TODO: 需要将 RGWUserInfo 转换为 C 格式并存储
        return c_user_->vtable->store(c_user_, (const rgw_sal_dpp_t*)dpp,
                                      (rgw_sal_yield_t*)&y, exclusive);
    }

    int remove_user(const DoutPrefixProvider* dpp, optional_yield y) override {
        if (!c_user_ || !c_user_->vtable || !c_user_->vtable->remove) {
            return -EINVAL;
        }
        return c_user_->vtable->remove(c_user_, (const rgw_sal_dpp_t*)dpp,
                                       (rgw_sal_yield_t*)&y);
    }

    int verify_mfa(const std::string& mfa_str, bool* verified, const DoutPrefixProvider* dpp, optional_yield y) override {
        return -ENOTSUP;
    }

    int list_groups(const DoutPrefixProvider* dpp, optional_yield y,
                    std::string_view marker, uint32_t max_items,
                    GroupList& listing) override {
        return -ENOTSUP;
    }

    RGWUserInfo& get_info() override {
        return info_;
    }

    void print(std::ostream& out) const override {
        out << "SalCUser(" << info_.user_id << ", " << info_.display_name << ")";
    }

    // 访问底层 C 用户
    rgw_sal_user_t* c_user() { return c_user_; }

private:
    explicit SalCUser(rgw_sal_driver_t* driver)
        : driver_(driver), c_user_(nullptr) {
        info_.max_buckets = RGW_DEFAULT_MAX_BUCKETS;
    }
};

/*============================================================================
 * SalCBucket - C++ Bucket 抽象类的 C SAL 实现
 *============================================================================*/

class SalCBucket : public Bucket {
private:
    rgw_sal_driver_t* driver_;
    rgw_sal_bucket_t* c_bucket_;
    RGWBucketInfo info_;
    RGWAccessControlPolicy acl_;
    Attrs attrs_;

public:
    static std::unique_ptr<Bucket> create(rgw_sal_driver_t* driver, const RGWBucketEnt& ent) {
        auto bucket = std::make_unique<SalCBucket>(driver);

        rgw_sal_bucket_info_t info = {};
        info.bucket.name = (char*)ent.bucket.name.c_str();
        info.bucket.tenant = (char*)ent.bucket.tenant.c_str();
        info.bucket.marker = (char*)ent.bucket.marker.c_str();
        info.bucket.bucket_id = (char*)ent.bucket.bucket_id.c_str();

        bucket->c_bucket_ = driver->vtable->get_bucket(driver, &info);
        if (!bucket->c_bucket_) {
            return nullptr;
        }

        bucket->info_.bucket = ent.bucket;
        bucket->info_.creation_time = ent.creation_time;
        bucket->info_.size = ent.size;
        bucket->info_.size_rounded = ent.size_rounded;
        bucket->info_.count = ent.count;
        bucket->info_.placement_rule = ent.placement_rule;

        return bucket;
    }

    static std::unique_ptr<Bucket> create(rgw_sal_driver_t* driver,
                                          const std::string& name,
                                          std::optional<std::string> tenant = std::nullopt) {
        auto bucket = std::make_unique<SalCBucket>(driver);

        rgw_sal_bucket_info_t info = {};
        info.bucket.name = (char*)name.c_str();
        if (tenant) {
            info.bucket.tenant = (char*)tenant->c_str();
        }

        bucket->c_bucket_ = driver->vtable->get_bucket(driver, &info);
        if (!bucket->c_bucket_) {
            return nullptr;
        }

        bucket->info_.bucket.name = name;
        if (tenant) {
            bucket->info_.bucket.tenant = *tenant;
        }

        return bucket;
    }

    static std::unique_ptr<Bucket> create_from_c(rgw_sal_driver_t* driver, rgw_sal_bucket_t* c_bucket) {
        auto bucket = std::make_unique<SalCBucket>(driver);
        bucket->c_bucket_ = c_bucket;

        // 填充基本信息
        if (c_bucket && c_bucket->vtable && c_bucket->vtable->get_name) {
            bucket->info_.bucket.name = c_bucket->vtable->get_name(c_bucket) ?: "";
        }

        return bucket;
    }

    virtual ~SalCBucket() {
        if (c_bucket_) {
            if (c_bucket_->vtable && c_bucket_->vtable->destroy) {
                c_bucket_->vtable->destroy(c_bucket_);
            }
            c_bucket_ = nullptr;
        }
    }

    std::unique_ptr<Object> get_object(const rgw_obj_key& key) override {
        // TODO: 需要创建 C++ Object 包装器
        return nullptr;
    }

    int list(const DoutPrefixProvider* dpp, ListParams& params, int max,
              ListResults& results, optional_yield y) override {
        // TODO: 实现
        return -ENOTSUP;
    }

    Attrs& get_attrs() override {
        return attrs_;
    }

    int set_attrs(Attrs a) override {
        attrs_ = std::move(a);
        return 0;
    }

    int remove(const DoutPrefixProvider* dpp, bool delete_children, optional_yield y) override {
        if (!c_bucket_ || !c_bucket_->vtable || !c_bucket_->vtable->remove) {
            return -EINVAL;
        }
        return c_bucket_->vtable->remove(c_bucket_, (const rgw_sal_dpp_t*)dpp,
                                         (rgw_sal_yield_t*)&y);
    }

    int remove_bypass_gc(int concurrent_max, bool keep_index_consistent,
                         optional_yield y, const DoutPrefixProvider* dpp) override {
        return -ENOTSUP;
    }

    RGWAccessControlPolicy& get_acl() override {
        return acl_;
    }

    int set_acl(const DoutPrefixProvider* dpp, RGWAccessControlPolicy& acl, optional_yield y) override {
        acl_ = acl;
        return 0;
    }

    int create(const DoutPrefixProvider* dpp, const CreateParams& params, optional_yield y) override {
        // TODO: 实现
        return -ENOTSUP;
    }

    int load_bucket(const DoutPrefixProvider* dpp, optional_yield y) override {
        if (!c_bucket_ || !c_bucket_->vtable || !c_bucket_->vtable->load) {
            return -EINVAL;
        }
        return c_bucket_->vtable->load(c_bucket_, (const rgw_sal_dpp_t*)dpp,
                                       (rgw_sal_yield_t*)&y);
    }

    int read_stats(const DoutPrefixProvider* dpp,
                   std::optional<ShardId> shard_id,
                   RGWBucketEnt* entry,
                   optional_yield y) override {
        // TODO: 实现
        return -ENOTSUP;
    }

    int read_stats_async(const DoutPrefixProvider* dpp,
                         std::optional<ShardId> shard_id,
                         RGWBucketEnt* ent) override {
        return -ENOTSUP;
    }

    int complete_stats(const DoutPrefixProvider* dpp,
                       std::optional<ShardId> shard_id,
                       RGWBucketEnt& ent,
                       optional_yield y) override {
        return -ENOTSUP;
    }

    int get_bucket_info(const DoutPrefixProvider* dpp, RGWBucketEnt* ent, optional_yield y) override {
        if (ent) {
            *ent = info_;
        }
        return 0;
    }

    int get_manifest(const DoutPrefixProvider* dpp, std::optional<ShardId> shard_id,
                     RGWObjManifest::obj_iterator& iter, optional_yield y) override {
        return -ENOTSUP;
    }

    int set_instance(const std::string& instance) override {
        info_.bucket.bucket_id = instance;
        return 0;
    }

    const std::string& get_instance() const override {
        return info_.bucket.bucket_id;
    }

    bool has_instance() const override {
        return !info_.bucket.bucket_id.empty();
    }

    const rgw_bucket& get_bucket() const override {
        return info_.bucket;
    }

    RGWBucketInfo& get_info() override {
        return info_;
    }

    void set_info(RGWBucketInfo& info) override {
        info_ = info;
    }

    const RGWOwner& get_owner() const override {
        return info_.owner;
    }

    void set_owner(const DoutPrefixProvider* dpp, const RGWOwner& owner) override {
        info_.owner = owner;
    }

    const std::string& get_tenant() const override {
        return info_.bucket.tenant;
    }

    const std::string& get_name() const override {
        return info_.bucket.name;
    }

    const std::string& get_marker() const override {
        return info_.bucket.marker;
    }

    uint8_t get_key_count() const override {
        return info_.key_count;
    }

    void set_marker(const std::string& marker) override {
        info_.bucket.marker = marker;
    }

    std::string& get_tag() override {
        return info_.bucket.tag;
    }

    void set_tag(const std::string& tag) override {
        info_.bucket.tag = tag;
    }

    bool has_tag(const std::string& tag) const override {
        return info_.bucket.tag == tag;
    }

    const ceph::real_time& get_creation_time() const override {
        return info_.creation_time;
    }

    const ceph::real_time& get_mtime() const override {
        return info_.mtime;
    }

    int get_mtime() const override {
        return 0;
    }

    void set_mtime(const ceph::real_time& mtime) override {
        info_.mtime = mtime;
    }

    int64_t get_size() const override {
        return info_.size;
    }

    int64_t get_size_rounded() const override {
        return info_.size_rounded;
    }

    uint64_t get_count() const override {
        return info_.count;
    }

    rgw_placement_rule& get_placement_rule() override {
        return info_.placement_rule;
    }

    bool get_pg_ver() const override {
        return 0;
    }

    const std::string& get_zonegroup_id() const override {
        return info_.bucket.zonegroup;
    }

    std::string get_zonegroup() const override {
        return info_.bucket.zonegroup;
    }

    void print(std::ostream& out) const override {
        out << "SalCBucket(" << info_.bucket << ")";
    }

    rgw_sal_bucket_t* c_bucket() { return c_bucket_; }

private:
    explicit SalCBucket(rgw_sal_driver_t* driver)
        : driver_(driver), c_bucket_(nullptr) {}
};

/*============================================================================
 * SalCDriver 核心方法实现
 *============================================================================*/

std::unique_ptr<User> SalCDriver::get_user(const rgw_user& u) {
    return SalCUser::create(c_driver->c_driver(), u);
}

std::unique_ptr<User> SalCDriver::get_user_by_access_key(
    const rgw_access_key& key,
    std::unique_ptr<RGWRoleEnv> env,
    const DoutPrefixProvider* dpp) {
    if (!c_driver || !c_driver->c_driver() || !c_driver->c_driver()->vtable) {
        return nullptr;
    }

    auto vtable = c_driver->c_driver()->vtable;
    if (!vtable->get_user_by_access_key) {
        return nullptr;
    }

    rgw_sal_user_t* c_user = vtable->get_user_by_access_key(
        c_driver->c_driver(),
        key.id.c_str(),
        key.key.c_str(),
        (const rgw_sal_dpp_t*)dpp);

    if (!c_user) {
        return nullptr;
    }

    return SalCUser::create_from_c(c_driver->c_driver(), c_user);
}

std::unique_ptr<User> SalCDriver::get_user_by_email(
    const std::string& email,
    const DoutPrefixProvider* dpp) {
    if (!c_driver || !c_driver->c_driver() || !c_driver->c_driver()->vtable) {
        return nullptr;
    }

    auto vtable = c_driver->c_driver()->vtable;
    if (!vtable->get_user_by_email) {
        return nullptr;
    }

    rgw_sal_user_t* c_user = vtable->get_user_by_email(
        c_driver->c_driver(),
        email.c_str(),
        (const rgw_sal_dpp_t*)dpp);

    if (!c_user) {
        return nullptr;
    }

    return SalCUser::create_from_c(c_driver->c_driver(), c_user);
}

std::unique_ptr<User> SalCDriver::get_user_by_swift(
    const std::string& user,
    const DoutPrefixProvider* dpp) {
    if (!c_driver || !c_driver->c_driver() || !c_driver->c_driver()->vtable) {
        return nullptr;
    }

    auto vtable = c_driver->c_driver()->vtable;
    if (!vtable->get_user_by_swift) {
        return nullptr;
    }

    rgw_sal_user_t* c_user = vtable->get_user_by_swift(
        c_driver->c_driver(),
        user.c_str(),
        (const rgw_sal_dpp_t*)dpp);

    if (!c_user) {
        return nullptr;
    }

    return SalCUser::create_from_c(c_driver->c_driver(), c_user);
}

std::unique_ptr<Bucket> SalCDriver::get_bucket(const RGWBucketEnt& e) {
    return SalCBucket::create(c_driver->c_driver(), e);
}

std::unique_ptr<Bucket> SalCDriver::get_bucket(const RGWBucketInfo& i) {
    auto bucket = SalCBucket::create(c_driver->c_driver(), i.bucket.name, i.bucket.tenant);
    if (bucket) {
        bucket->set_info(const_cast<RGWBucketInfo&>(i));
    }
    return bucket;
}

std::unique_ptr<Bucket> SalCDriver::get_bucket(
    const std::string& name,
    std::optional<std::string> tenant,
    std::optional<std::string> bucket_id) {
    return SalCBucket::create(c_driver->c_driver(), name, tenant);
}

std::unique_ptr<Object> SalCDriver::get_object(const rgw_obj_key& k) {
    // TODO: 创建 SalCObject
    return nullptr;
}

int SalCDriver::load_user(const DoutPrefixProvider* dpp, User& u, optional_yield y) {
    // SalCUser 已经实现了 load_user
    return u.load_user(dpp, y);
}

int SalCDriver::load_bucket(const DoutPrefixProvider* dpp, const rgw_bucket& bucket,
                             std::unique_ptr<Bucket>* bucket_obj, optional_yield y) {
    auto bucket_impl = SalCBucket::create(c_driver->c_driver(), bucket.name, bucket.tenant);
    if (!bucket_impl) {
        return -ENOENT;
    }

    int ret = bucket_impl->load_bucket(dpp, y);
    if (ret < 0) {
        return ret;
    }

    *bucket_obj = std::move(bucket_impl);
    return 0;
}

int SalCDriver::list_all_users(const DoutPrefixProvider* dpp, std::string& marker,
                                std::list<std::string>& users) {
    // TODO: 实现
    return -ENOTSUP;
}

int SalCDriver::list_all_buckets(const DoutPrefixProvider* dpp, std::string& marker,
                                  std::list<rgw_user>& buckets) {
    // TODO: 实现
    return -ENOTSUP;
}

} // namespace rgw::sal

#endif /* WITH_RGW_SAL_C */
