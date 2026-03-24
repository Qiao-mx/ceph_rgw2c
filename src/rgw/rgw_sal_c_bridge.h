/**
 * @file rgw_sal_c_bridge.h
 * @brief SAL C 实现桥接层头文件
 *
 * 提供 C SAL 实现到 C++ SAL Driver 的桥接，
 * 使 C++ RGW 代码可以通过统一的 Driver 接口调用 C 实现。
 */

#pragma once

#include "rgw_sal.h"
#include "rgw_sal_c_wrapper.h"

#ifdef WITH_RGW_SAL_C

#include <memory>
#include <string>
#include <optional>

namespace rgw::sal {

/**
 * @brief C++ Driver 包装器 - 将 C SAL 包装为 C++ Driver
 *
 * 这个类是 C++ SAL Driver 的实现，内部调用 C SAL 层。
 * 所有方法都将 C++ 调用转发到底层 C 实现。
 */
class SalCDriver : public Driver {
private:
    std::unique_ptr<sal::SalDriver> c_driver;
    std::string name;

public:
    /**
     * @brief 构造函数
     * @param dpp 调试前缀提供者
     * @param cct Ceph 上下文
     * @param type 驱动类型 ("rados", "dbstore" 等)
     */
    SalCDriver(const DoutPrefixProvider* dpp, CephContext* cct, const std::string& type);

    /**
     * @brief 析构函数
     */
    ~SalCDriver() override;

    /**
     * @brief 获取驱动名称
     */
    const std::string& get_name() const override;

    /**
     * @brief 获取 Ceph 上下文
     */
    CephContext* ctx() override;

    /**
     * @brief 获取用户
     * @param u 用户 ID
     */
    std::unique_ptr<User> get_user(const rgw_user& u) override;

    /**
     * @brief 通过 Access Key 获取用户
     */
    std::unique_ptr<User> get_user_by_access_key(
        const rgw_access_key& key,
        std::unique_ptr<RGWRoleEnv> env = nullptr,
        const DoutPrefixProvider* dpp = nullptr) override;

    /**
     * @brief 通过 Email 获取用户
     */
    std::unique_ptr<User> get_user_by_email(
        const std::string& email,
        const DoutPrefixProvider* dpp = nullptr) override;

    /**
     * @brief 通过 Swift 用户名获取用户
     */
    std::unique_ptr<User> get_user_by_swift(
        const std::string& user,
        const DoutPrefixProvider* dpp = nullptr) override;

    /**
     * @brief 初始化驱动
     */
    int init_driver(const Config& cfg) override;

    /**
     * @brief 完成驱动
     */
    void finalize() override;

    /**
     * @brief 获取桶 (通过 RGWBucketEnt)
     */
    std::unique_ptr<Bucket> get_bucket(const RGWBucketEnt& e) override;

    /**
     * @brief 获取桶 (通过 RGWBucketInfo)
     */
    std::unique_ptr<Bucket> get_bucket(const RGWBucketInfo& i) override;

    /**
     * @brief 获取桶 (通过名称)
     */
    std::unique_ptr<Bucket> get_bucket(
        const std::string& name,
        std::optional<std::string> tenant = std::nullopt,
        std::optional<std::string> bucket_id = std::nullopt) override;

    /**
     * @brief 获取对象
     */
    std::unique_ptr<Object> get_object(const rgw_obj_key& k) override;

    std::unique_ptr<PlacementTarget> get_placement_target(
        const RGWZonePlacementInfo& info) override;
    std::unique_ptr<ZoneGroup> get_zonegroup(const std::string& id) override;
    std::unique_ptr<Notification> get_notification(
        const DoutPrefixProvider* dpp, Bucket* bucket, Object* obj,
        rgw::notify::EventTypeList& types) override;
    std::unique_ptr<Notification> get_notification(
        const DoutPrefixProvider* dpp, Bucket* bucket, Object* obj,
        const std::string& topic_name) override;
    std::unique_ptr<Policy> get_bucket_policy(
        const DoutPrefixProvider* dpp, std::unique_ptr<Bucket> bucket) override;
    std::unique_ptr<Policy> get_object_policy(
        const DoutPrefixProvider* dpp, std::unique_ptr<Object> object) override;

    std::unique_ptr<Bucket> create_bucket(
        std::unique_ptr<User> u, const rgw_bucket& b, const std::string& id,
        const std::string& zonegroup_id, const RGWZonePlacementInfo& placement_rule,
        const std::string& swift_versioning, const std::string& swift_ACL,
        const RGWBucketInfo& bucket_info, uint32_t finisher,
        const std::map<std::string, bufferlist>& attrs,
        RGWObjVersionTracker& objv_tracker, const std::string& entry_point_obj_tag,
        TagType tag_type, const std::string& tag,
        OptionalAcls& acls, bool add_entry_point_tag,
        RGWBucketInfo::CreateFlag flags = RGWBucketInfo::CreateFlag::None,
        optional_yield y = nullopt) override;

    std::unique_ptr<User> create_user(
        std::unique_ptr<User>& u, const rgw_user& auid, std::string& display_name,
        const std::string& email, std::map<std::string, bufferlist>* pattrs,
        const RGWUserCaps& caps, std::optional<RGWUserSource> source,
        std::optional<uint32_t> type = std::nullopt) override;

    RGWPostPolicyEnv* get_post_policy_env() override;

    void delete_system_user(
        const DoutPrefixProvider* dpp, const std::string& system_user_id,
        optional_yield y) override;

    int load_user(const DoutPrefixProvider* dpp, User& u, optional_yield y = nullopt) override;
    int load_bucket(const DoutPrefixProvider* dpp, const rgw_bucket& bucket,
                     std::unique_ptr<Bucket>* bucket_obj, optional_yield y = nullopt) override;
    int load_placement_target(const DoutPrefixProvider* dpp, const std::string& placement_id,
                               std::unique_ptr<PlacementTarget>& target) override;

    int list_all_users(const DoutPrefixProvider* dpp, std::string& marker,
                        std::list<std::string>& users) override;
    int list_all_buckets(const DoutPrefixProvider* dpp, std::string& marker,
                           std::list<rgw_user>& buckets) override;

    int sync_attrs() override;
    int validate_system_user(const std::string& user_id, std::unique_ptr<User>* new_user) override;
    int validate_tenant_name(const std::string& user_id) override;

    int remove_object(const DoutPrefixProvider* dpp, Bucket* bucket,
                        const rgw_obj_key& k, std::optional<uint64_t> lo_id,
                        std::optional<ceph::real_time> removal_time,
                        bool exclusive, optional_yield y) override;

    int remove_bucket(const DoutPrefixProvider* dpp, Bucket* bucket,
                        bool delete_children, optional_yield y) override;
    int truncate_bucket(const DoutPrefixProvider* dpp, Bucket* bucket,
                          uint64_t new_size, optional_yield y) override;
    int delete_bucket(const DoutPrefixProvider* dpp, Bucket* bucket,
                       RGWObjVersionTracker& objv_tracker, optional_yield y) override;

    int list_buckets(const DoutPrefixProvider* dpp, const rgw_user& u,
                      const std::string& marker, const std::string& end_marker,
                      uint64_t max, bool need_stats, BucketList& buckets,
                      optional_yield y) override;

    int list_objects(const DoutPrefixProvider* dpp, Bucket* bucket,
                      int64_t max, const std::string& marker, const std::string& prefix,
                      const std::string& delimiter,
                      std::vector<rgw_bucket_dir_entry> *result,
                      std::map<std::string, bool> *common_prefixes,
                      bool get_content_type, bool list_versions,
                      uint32_t index_offset, optional_yield y,
                      RGWBucketEnt* pcur_bucket_ent = nullptr) override;

    Object* get_object(const rgw_obj_key& key, Bucket* bucket) override;
    int obj_operate(const DoutPrefixProvider* dpp, Bucket* bucket,
                     const rgw_obj_key& k, std::optional<uint64_t> lo_id,
                     ObjStore::SystemObject::SystemObjArr& v,
                     RGWObjState *state, RGWOperationType type, ceph::real_time *pmtime,
                     std::unique_ptr<Policy> *ppolicy, std::map<std::string, bufferlist>& rmattrs,
                     const bufferlist *data, ACLOwner *owner, ceph::real_time set_mtime,
                     const std::map<std::string, std::string> *pzone_tags,
                     std::optional<ceph::real_time> *pinhale_time,
                     std::optional<ceph::real_time> *predate,
                     std::optional<std::map<std::string, std::string>> *pzone_and_set_attrs,
                     RGWObjTrace* trace, optional_yield y) override;

    int get_obj_state(const DoutPrefixProvider* dpp, Bucket* bucket,
                        const rgw_obj_key& k, std::optional<uint64_t> lo_id,
                        RGWObjState **state, optional_yield y, bool follow_manifest = true) override;

    int spring_object_copy(Bucket* bucket, Object* src, std::optional<uint64_t> lo_id,
                             std::optional<ceph::real_time> *pmtime,
                             const std::map<std::string, std::string> *pzone_tags,
                             std::optional<std::map<std::string, std::string>> *pzone_and_set_attrs,
                             RGWCopyPart** copy, optional_yield y) override;

    std::unique_ptr<Bucket> bucket_sync_for(
        const DoutPrefixProvider* dpp, std::unique_ptr<Bucket> source,
        sync_utils::PolicyEnv &env, sync_utils::DynamicCtxVec &dyn_ctx,
        optional_yield y) override;

    int list_multiparts(const DoutPrefixProvider* dpp, Bucket* bucket,
                         const std::string& prefix, std::string& marker,
                         const std::string& delimiter, const int max_keys,
                         std::vector<std::pair<rgw_obj_key, rgw_bucket_dir_entry>>* result,
                         std::map<std::string, bool>* common_prefixes, int* is_truncated,
                         optional_yield y) override;
    int abort_multipart(const DoutPrefixProvider* dpp, Bucket* bucket,
                          std::unique_ptr<MultipartUploadControl> upload, optional_yield y) override;
    int list_multiparts_usage(const DoutPrefixProvider* dpp, User* user,
                                 uint64_t* aggregated_usage, std::map<std::string, ClassIndexStatus>&) override;
    int get_policy_from_attr(const DoutPrefixProvider* dpp, CephContext* cct,
                               std::map<std::string, bufferlist>& attrs, RGWAccessControlPolicy* policy) override;

    int update_placement_target(const DoutPrefixProvider* dpp,
                                   Bucket* bucket,
                                   const RGWBucketInfo::CreateFlag flags,
                                   const std::unique_ptr<PlacementTarget>& placement) override;

    int delete_system_obj(const DoutPrefixProvider* dpp, RGWSystemObject& obj,
                            std::optional<ceph::real_time> delete_at,
                            std::string* obj_rep_zone,
                            const std::map<std::string, std::string>* pzone_tags,
                            std::optional<std::map<std::string, std::string>> pzone_and_set_attrs,
                            const bufferlist *data, RGWObjVersionTracker& objv_tracker,
                            optional_yield y) override;

    int omap_get_vals(const DoutPrefixProvider* dpp, const std::string& oid,
                        uint64_t start_epoch,
                        const std::string& marker, uint64_t count,
                        std::map<std::string, bufferlist>* vals,
                        std::map<std::string, bufferlist>* burst,
                        bool* p_truncated,
                        std::optional<rgw::encode::OmapDecodeLogFilter*> decode_filter,
                        optional_yield y) override;

    int omap_get_all(const DoutPrefixProvider* dpp, const std::string& oid,
                       uint64_t epoch, std::map<std::string, bufferlist>* vals,
                       optional_yield y) override;

    int omap_set(const DoutPrefixProvider* dpp, const std::string& oid,
                   uint64_t epoch,
                   const std::map<std::string, bufferlist>& vals,
                   std::optional<std::map<std::string, std::string>> rmattr_names,
                   optional_yield y) override;

    int omap_set_header(const DoutPrefixProvider* dpp, const std::string& oid,
                           uint64_t epoch, const std::map<std::string, bufferlist>& attrs,
                           optional_yield y) override;

    int omap_del(const DoutPrefixProvider* dpp, const std::string& oid,
                   uint64_t epoch,
                   const std::set<std::string>& keys,
                   optional_yield y) override;

    int update_omap_vals(const DoutPrefixProvider* dpp,
                          const std::string& oid, uint64_t epoch,
                          const std::map<std::string, bufferlist>& map,
                          std::map<std::string, std::string>* pzone_and_set_attrs,
                          optional_yield y) override;

    int aio_wait() override;
    uint32_t aio_get_completed(void* ctx, AioResult** done) override;

    bool is_meta_master() override;
    const RGWQuotaInfo* get_quota() override;

    int get_bi_info(const DoutPrefixProvider* dpp, Bucket* bucket,
                      RGWBucketIndexCINfo* cxinfo, optional_yield y) override;
    int set_bucket_instance_attrs(const DoutPrefixProvider* dpp, Bucket* bucket,
                                     RGWBucketInfo& info,
                                     RGWBucketInfo::CreateFlag flags,
                                     const std::map<std::string, bufferlist>& bucket_attrs,
                                     RGWObjVersionTracker& objv_tracker,
                                     optional_yield y) override;

    int log_usage(const DoutPrefixProvider* dpp, const rgw_usage_log_entry& entry,
                    optional_yield y) override;
    int log_op(const DoutPrefixProvider* dpp, const std::string& op_name,
                 const bufferlist& op_bytes, const bufferlist& op_attrs,
                 std::map<std::string, bufferlist> *pout) override;

    int ref(const DoutPrefixProvider* dpp, RGWSystemObject& obj,
              RGWObjVersionTracker& objv_tracker, optional_yield y) override;
    int delete_ref(const DoutPrefixProvider* dpp, RGWSystemObject& obj,
                     RGWObjVersionTracker& objv_tracker, optional_yield y) override;
    int update_sys_object(const DoutPrefixProvider* dpp, RGWSystemObject& obj,
                            RGWObjVersionTracker& objv_tracker, const std::string& category,
                            const bufferlist& data, bool exclusive,
                            std::map<std::string, std::string>* pzone_and_set_attrs,
                            RGWObjVersionTracker& read_version,
                            real_time* set_mtime, optional_yield y) override;
    int get_system_object(const DoutPrefixProvider* dpp, RGWSystemObject& obj,
                             const std::string& category, bufferlist* data,
                             RGWObjVersionTracker& objv_tracker, real_time* pmtime,
                             std::map<std::string, bufferlist>* pattrs,
                             std::map<std::string, std::string>* pzone_and_set_attrs,
                             optional_yield y) override;
    int delete_system_obj(const DoutPrefixProvider* dpp, RGWSystemObject& obj,
                            RGWObjVersionTracker& objv_tracker, bool exclusive,
                            std::map<std::string, std::string>* pzone_and_set_attrs,
                            optional_yield y) override;

    int get_compat_threshold() override;

    std::unique_ptr<Writer> get_append_writer(
        const DoutPrefixProvider* dpp, std::optional<ZoneID> zone_id,
        std::optional<TenantUID> tenant_uid, Bucket* bucket, Object* obj,
        const ACLOwner& owner, const std::optional<uint64_t>& position,
        const std::optional<char>& delimiter, const uint64_t& prealloc_size,
        const std::string& unique_tag, CephContext* cct) override;

    std::unique_ptr<Writer> get_atomic_writer(
        const DoutPrefixProvider* dpp, std::optional<ZoneID> zone_id,
        std::optional<TenantUID> tenant_uid, Bucket* bucket, Object* obj,
        const ACLOwner& owner, uint64_t clo_size,
        const std::string& unique_tag, CephContext* cct) override;

    std::unique_ptr<Role> get_role(
        const std::string name,
        const std::optional<std::string> tenant_name = std::nullopt,
        const std::optional<std::string> path = std::nullopt,
        const std::optional<std::string> trust_arn = std::nullopt,
        const std::optional<std::string> description = std::nullopt) override;

    std::unique_ptr<Role> get_role(const RGWRoleInfo& info) override;
    std::vector<std::string> get_role_names(const std::string& tenant_name) override;

    std::unique_ptr<OIDCProvider> get_oidc_provider(
        const std::string resource_prefix, const std::string& tenant_name) override;

    int get_groups(const DoutPrefixProvider* dpp, Groups* groups,
                     optional_yield y) override;

    int write_zero_tag(const DoutPrefixProvider* dpp, const std::string& tenant) override;
    int delete_zero_tags(const DoutPrefixProvider* dpp, const std::string& tenant, optional_yield y) override;

    std::unique_ptr<Topic> get_topic(const std::string& name, const std::string& tenant_name) override;

    int list_topics(const DoutPrefixProvider* dpp, std::vector<std::string>& topics,
                      optional_yield y) override;
    int delete_topic(const DoutPrefixProvider* dpp, const std::string& name,
                       const std::string& tenant_name, const RGWObjVersionTracker& objv_tracker,
                       optional_yield y) override;

    std::unique_ptr<User> get_factory() const override;

    std::unique_ptr<lua::Background> get_lua_background() override;
    void flush_lua_background(std::string config) override;

    RGWWatchType* get_watcher() override;
    void schedule_context(const DoutPrefixProvider* dpp, std::any& opaque) override;

    int renew_leases(const DoutPrefixProvider* dpp,
                       std::optional<rgw::sal::ConfigStore::LeaseInfo>) override;
    std::set<std::string> get_service_names() override;

    void adjust_olh_pivot(std::unique_ptr<Object> object, uint64_t* written) override;

    bool process_new_bucket_instance_config(
        const DoutPrefixProvider* dpp, RGWBucketInfo& info,
        RGWBucketInfo::CreateFlag flags,
        std::map<std::string, bufferlist>& bucket_attrs) override;
};

/**
 * @brief C SAL Driver 工厂函数
 *
 * 创建 C SAL Driver 实例，供 DriverManager 调用
 */
Driver* newSalCDriver(CephContext* cct, const std::string& type);

} // namespace rgw::sal

#endif /* WITH_RGW_SAL_C */
