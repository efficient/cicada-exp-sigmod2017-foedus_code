/*
 * Copyright (c) 2014, Hewlett-Packard Development Company, LP.
 * The license and distribution terms for this file are placed in LICENSE.txt.
 */
#include <foedus/storage/array/array_storage.hpp>
#include <foedus/storage/array/array_storage_pimpl.hpp>
namespace foedus {
namespace storage {
namespace array {
ErrorStack ArrayStoragePimpl::initialize_once() {
    return RET_OK;
}

ErrorStack ArrayStoragePimpl::uninitialize_once() {
    return RET_OK;
}
ErrorStack ArrayStoragePimpl::get_record(ArrayOffset offset, void *payload) {
    return RET_OK;
}
ErrorStack ArrayStoragePimpl::get_record_part(ArrayOffset offset, void *payload,
                            uint16_t payload_offset, uint16_t payload_count) {
    return RET_OK;
}

}  // namespace array
}  // namespace storage
}  // namespace foedus
