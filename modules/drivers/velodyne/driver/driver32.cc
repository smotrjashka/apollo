/******************************************************************************
 * Copyright 2018 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include <time.h>
#include <unistd.h>
#include <cmath>
#include <string>
#include <thread>

#include "modules/drivers/velodyne/driver/driver.h"

namespace apollo {
namespace drivers {
namespace velodyne {

Velodyne32Driver::Velodyne32Driver(const Config& config) { config_ = config; }

void Velodyne32Driver::init() {
  double packet_rate = 1808.0;                // packet frequency (Hz)
  double frequency = (config_.rpm() / 60.0);  // expected Hz rate

  // default number of packets for each scan is a single revolution
  // (fractions rounded up)
  config_.set_npackets(static_cast<int>(ceil(packet_rate / frequency)));
  AINFO << "publishing " << config_.npackets() << " packets per scan";

  // open Velodyne input device or file

  input_.reset(new SocketInput());
  positioning_input_.reset(new SocketInput());
  input_->init(config_.firing_data_port());
  positioning_input_->init(config_.positioning_data_port());

  std::thread thread(&Velodyne32Driver::poll_positioning_packet, this);
  thread.detach();
}

/** poll the device
 *
 *  @returns true unless end of file reached
 */
bool Velodyne32Driver::poll(std::shared_ptr<VelodyneScan> scan) {
  // Allocate a new shared pointer for zero-copy sharing with other nodelets.
  if (basetime_ == 0) {
    AWARN << "basetime is zero";
    usleep(100);
    return true;
  }

  int poll_result = poll_standard(scan);

  if (poll_result == SOCKET_TIMEOUT || poll_result == RECIEVE_FAIL) {
    return true;  // poll again
  }

  if (scan->firing_pkts_size() <= 0) {
    AINFO << "Get a empty scan from port: " << config_.firing_data_port();
    return true;
  }

  ADEBUG << "Publishing a full Velodyne scan.";
  scan->mutable_header()->set_timestamp_sec(cybertron::Time().Now().ToSecond());
  scan->mutable_header()->set_frame_id(config_.frame_id());
  // we use first packet gps time update gps base hour
  // in cloud nodelet, will update base time packet by packets
  uint32_t current_secs = *(reinterpret_cast<uint32_t*>(
      const_cast<char*>(scan->firing_pkts(0).data().c_str() + 1200)));
  update_gps_top_hour(current_secs);
  scan->set_basetime(basetime_);
  return true;
}

}  // namespace velodyne
}  // namespace drivers
}  // namespace apollo
