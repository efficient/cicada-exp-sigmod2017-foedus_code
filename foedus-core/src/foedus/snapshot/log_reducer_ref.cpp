/*
 * Copyright (c) 2014, Hewlett-Packard Development Company, LP.
 * The license and distribution terms for this file are placed in LICENSE.txt.
 */
#include "foedus/snapshot/log_reducer_ref.hpp"

#include <glog/logging.h>

#include <cstring>
#include <string>

#include "foedus/debugging/rdtsc_watch.hpp"
#include "foedus/debugging/stop_watch.hpp"
#include "foedus/snapshot/log_reducer_impl.hpp"
#include "foedus/soc/soc_manager.hpp"

namespace foedus {
namespace snapshot {

LogReducerRef::LogReducerRef(Engine* engine, uint16_t node) {
  engine_ = engine;
  soc::NodeMemoryAnchors* anchors
    = engine_->get_soc_manager()->get_shared_memory_repo()->get_node_memory_anchors(node);
  control_block_ = anchors->log_reducer_memory_;
  buffers_[0] = anchors->log_reducer_buffers_[0];
  buffers_[1] = anchors->log_reducer_buffers_[1];
  root_info_pages_ = anchors->log_reducer_root_info_pages_;
}

uint16_t    LogReducerRef::get_id() const {
  return control_block_->id_;
}
std::string LogReducerRef::to_string() const {
  return std::string("LogReducer-") + std::to_string(get_id());
}

void LogReducerRef::clear() {
  control_block_->clear();
}

uint32_t LogReducerRef::get_total_storage_count() const {
  return control_block_->total_storage_count_;
}

uint32_t    LogReducerRef::get_current_buffer_index_atomic() const {
  return control_block_->current_buffer_;
}

uint64_t    LogReducerRef::get_buffer_size() const {
  // the value is in total of two buffers. (<< 20) / 2 == << 19
  return static_cast<uint64_t>(engine_->get_options().snapshot_.log_reducer_buffer_mb_) << 19;
}


void*       LogReducerRef::get_buffer(uint32_t index) const {
  soc::NodeMemoryAnchors* anchors
    = engine_->get_soc_manager()->get_shared_memory_repo()->get_node_memory_anchors(get_id());
  return anchors->log_reducer_buffers_[index % 2];
}

void        LogReducerRef::append_log_chunk(
  storage::StorageId storage_id,
  const char* send_buffer,
  uint32_t log_count,
  uint64_t send_buffer_size) {
  DVLOG(1) << "Appending a block of " << send_buffer_size << " bytes (" << log_count
    << " entries) to " << to_string() << "'s buffer for storage-" << storage_id;
  debugging::RdtscWatch stop_watch;

  const uint64_t required_size = send_buffer_size + sizeof(BlockHeader);
  uint32_t buffer_index = 0;
  uint64_t begin_position = 0;
  while (true) {
    buffer_index = get_current_buffer_index_atomic();
    std::atomic<uint64_t>* status_address = control_block_->get_buffer_status_address(buffer_index);

    // If even the current buffer is marked as no more writers, the reducer is getting behind.
    // Mappers have to wait, potentially for a long time. So, let's just sleep.
    ReducerBufferStatus cur_status = control_block_->get_buffer_status_atomic(buffer_index);
    if (cur_status.components.flags_ & kFlagNoMoreWriters) {
      VLOG(0) << "Both buffers full in" << to_string() << ". I'll sleep for a while..";
      while (get_current_buffer_index_atomic() == buffer_index) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      VLOG(0) << "Buffer switched in" << to_string() << " after sleep. Let's resume.";
      continue;
    }

    // the buffer is now full. let's mark this buffer full and
    // then wake up reducer to do switch.
    uint64_t buffer_size = get_buffer_size();
    if (cur_status.components.tail_position_ + required_size > buffer_size) {
      ReducerBufferStatus new_status = cur_status;
      new_status.components.flags_ |= kFlagNoMoreWriters;
      if (!status_address->compare_exchange_strong(cur_status.word, new_status.word)) {
        // if CAS fails, someone else might have already done it. retry
        continue;
      }

      // Buffer switch won't be that often, so we simply wait without waking up the reducer.
      continue;
    }

    // okay, "looks like" we can append our log. make it sure with atomic CAS
    ReducerBufferStatus new_status = cur_status;
    ++new_status.components.active_writers_;
    new_status.components.tail_position_ += to_buffer_position(required_size);
    if (!status_address->compare_exchange_strong(cur_status.word, new_status.word)) {
      // someone else did something. retry
      continue;
    }

    // okay, we atomically reserved the space.
    begin_position = from_buffer_position(cur_status.components.tail_position_);
    break;
  }

  // now start copying. this might take a few tens of microseconds if it's 1MB and on another
  // NUMA node.
  void* buffer = get_buffer(buffer_index);
  debugging::RdtscWatch copy_watch;
  char* destination = reinterpret_cast<char*>(buffer) + begin_position;
  BlockHeader header;
  header.storage_id_ = storage_id;
  header.log_count_ = log_count;
  header.block_length_ = to_buffer_position(required_size);
  header.magic_word_ = kBlockHeaderMagicWord;
  std::memcpy(destination, &header, sizeof(BlockHeader));
  std::memcpy(destination + sizeof(BlockHeader), send_buffer, send_buffer_size);
  copy_watch.stop();
  DVLOG(1) << "memcpy of " << send_buffer_size << " bytes took "
    << copy_watch.elapsed() << " cycles";

  // done, let's decrement the active_writers_ to declare we are done.
  while (true) {
    std::atomic<uint64_t>* status_address = control_block_->get_buffer_status_address(buffer_index);
    ReducerBufferStatus cur_status = control_block_->get_buffer_status_atomic(buffer_index);
    ReducerBufferStatus new_status = cur_status;
    ASSERT_ND(new_status.components.active_writers_ > 0);
    --new_status.components.active_writers_;
    if (!status_address->compare_exchange_strong(cur_status.word, new_status.word)) {
      // if CAS fails, someone else might have already done it. retry
      continue;
    }

    // okay, decremented. let's exit.

    // Disabled. for now the reducer does spin. so no need for wakeup
    // if (new_status.components.active_writers_ == 0
    //   && (new_status.components.flags_ & kFlagNoMoreWriters)) {
    //   // if this was the last writer and the buffer was already closed for new writers,
    //   // the reducer might be waiting for us. let's wake her up
    //   thread_.wakeup();
    // }
    break;
  }

  stop_watch.stop();
  DVLOG(1) << "Completed appending a block of " << send_buffer_size << " bytes to " << to_string()
    << "'s buffer for storage-" << storage_id << " in " << stop_watch.elapsed() << " cycles";
}

}  // namespace snapshot
}  // namespace foedus
