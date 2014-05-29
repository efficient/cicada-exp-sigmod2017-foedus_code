/*
 * Copyright (c) 2014, Hewlett-Packard Development Company, LP.
 * The license and distribution terms for this file are placed in LICENSE.txt.
 */
#include <foedus/storage/storage_log_types.hpp>
#include <foedus/storage/storage_manager.hpp>
#include <foedus/thread/thread.hpp>
#include <foedus/engine.hpp>
#include <glog/logging.h>
#include <ostream>
#include <string>
namespace foedus {
namespace storage {

void DropLogType::populate(StorageId storage_id) {
    ASSERT_ND(storage_id > 0);
    header_.storage_id_ = storage_id;
}
void DropLogType::apply_storage(const xct::XctId& /*xct_id*/,
                                thread::Thread* context, Storage* storage) {
    ASSERT_ND(storage);  // because we are now dropping it.
    UNUSED_ND(storage);
    ASSERT_ND(header_.storage_id_ > 0);
    LOG(INFO) << "Applying DROP STORAGE log: " << *this;
    COERCE_ERROR(context->get_engine()->get_storage_manager().remove_storage(header_.storage_id_));
    LOG(INFO) << "Applied DROP STORAGE log: " << *this;
}

void DropLogType::assert_valid() {
    assert_valid_generic();
    ASSERT_ND(header_.log_length_ == sizeof(DropLogType));
    ASSERT_ND(header_.log_type_code_ = log::get_log_code<DropLogType>());
}
std::ostream& operator<<(std::ostream& o, const DropLogType& v) {
    o << "<StorageDropLog>"
        << "<storage_id_>" << v.header_.storage_id_ << "</storage_id_>"
        << "</StorageDropLog>";
    return o;
}

}  // namespace storage
}  // namespace foedus