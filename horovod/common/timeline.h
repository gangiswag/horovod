// Copyright 2019 Uber Technologies, Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// =============================================================================

#ifndef HOROVOD_TIMELINE_H
#define HOROVOD_TIMELINE_H

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <boost/lockfree/spsc_queue.hpp>

#include "common.h"
#include "message.h"

namespace horovod {
namespace common {

enum TimelineRecordType { EVENT, MARKER };

struct TimelineRecord {
  TimelineRecordType type;
  std::string tensor_name;
  char phase;
  std::string op_name;
  std::string args;
  std::string marker_name;
  long ts_micros;
};

class TimelineWriter {
public:
  void Initialize(std::string file_name,
                  std::chrono::steady_clock::time_point start_time_);
  void Shutdown();
  inline bool IsHealthy() const { return healthy_; }
  inline bool Active() const { return active_; }
  void EnqueueWriteEvent(const std::string& tensor_name, char phase,
                         const std::string& op_name, const std::string& args,
                         long ts_micros);
  void EnqueueWriteMarker(const std::string& name, long ts_micros);
  void SetPendingTimelineFile(std::string filename);
  TimelineWriter();

private:
  void DoWriteEvent(const TimelineRecord& r);
  void DoWriteMarker(const TimelineRecord& r);
  void WriterLoop();
  void WriteAtFileStart();
  std::string PendingTimelineFile();
  void SetTimelineFile(std::string filename);

  // Are we healthy?  Queue no longer accepts new work items and stops draining
  // immediately when false.
  std::atomic_bool healthy_{false};

  // Similar to healthy, but allows queue to be drained before closing when set
  // to false.
  std::atomic_bool active_{false};

  bool pending_status_;

  // Timeline file.
  std::ofstream file_;

  // Timeline record queue.
  boost::lockfree::spsc_queue<TimelineRecord,
                              boost::lockfree::capacity<1048576>>
      record_queue_;

  // Mapping of tensor names to indexes. It is used to reduce size of the
  // timeline file.
  std::unordered_map<std::string, int> tensor_table_;

  std::thread writer_thread_;
  std::string cur_filename_;
  std::string new_pending_filename_;
  bool is_new_file_;
  // stores actual wall clock when horovod timeline was initialized
  long long start_time_since_epoch_utc_micros_;
  // mutex that protects timeline writer state
  std::recursive_mutex writer_mutex_;
};

enum TimelineState { UNKNOWN, NEGOTIATING, TOP_LEVEL, ACTIVITY };

// Writes timeline in Chrome Tracing format. Timeline spec is from:
// https://github.com/catapult-project/catapult/tree/master/tracing
class Timeline {
public:
  void Initialize(std::string file_name, unsigned int horovod_size);
  void Shutdown();
  inline bool Initialized() const { return initialized_; }
  void NegotiateStart(const std::string& tensor_name,
                      Request::RequestType request_type);
  void NegotiateRankReady(const std::string& tensor_name, int rank);
  void NegotiateEnd(const std::string& tensor_name);
  void Start(const std::string& tensor_name,
             const Response::ResponseType response_type);
  void ActivityStartAll(const std::vector<TensorTableEntry>& entries,
                        const std::string& activity);
  void ActivityStart(const std::string& tensor_name,
                     const std::string& activity);
  void ActivityEndAll(const std::vector<TensorTableEntry>& entries);
  void ActivityEnd(const std::string& tensor_name);
  void End(const std::string& tensor_name, std::shared_ptr<Tensor> tensor);
  void MarkCycleStart();
  void SetPendingTimelineFile(std::string filename);

private:
  long TimeSinceStartMicros() const;
  void WriteEvent(const std::string& tensor_name, char phase,
                  const std::string& op_name = "",
                  const std::string& args = "");
  void WriteMarker(const std::string& name);

  // Boolean flag indicating whether Timeline was initialized (and thus should
  // be recorded).
  bool initialized_ = false;

  // Timeline writer.
  TimelineWriter writer_;

  // Time point when Horovod was started.
  std::chrono::steady_clock::time_point start_time_;

  // A mutex that guards timeline state from concurrent access.
  std::recursive_mutex mutex_;

  // Current state of each tensor in the timeline.
  std::unordered_map<std::string, TimelineState> tensor_states_;

  // Map of ranks to their string representations.
  // std::to_string() is very slow.
  std::vector<std::string> rank_strings_;
};

} // namespace common
} // namespace horovod

#endif // HOROVOD_TIMELINE_H
