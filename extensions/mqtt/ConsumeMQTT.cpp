/**
 * @file ConsumeMQTT.cpp
 * ConsumeMQTT class implementation
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "ConsumeMQTT.h"
#include <stdio.h>
#include <algorithm>
#include <memory>
#include <string>
#include <map>
#include <set>
#include "utils/TimeUtil.h"
#include "utils/StringUtils.h"
#include "core/ProcessContext.h"
#include "core/ProcessSession.h"

namespace org {
namespace apache {
namespace nifi {
namespace minifi {
namespace processors {

core::Property ConsumeMQTT::MaxQueueSize("Max Flow Segment Size", "Maximum flow content payload segment size for the MQTT record", "");

void ConsumeMQTT::initialize() {
  // Set the supported properties
  std::set<core::Property> properties;
  properties.insert(BrokerURL);
  properties.insert(CleanSession);
  properties.insert(ClientID);
  properties.insert(UserName);
  properties.insert(PassWord);
  properties.insert(KeepLiveInterval);
  properties.insert(ConnectionTimeOut);
  properties.insert(QOS);
  properties.insert(Topic);
  properties.insert(MaxQueueSize);
  setSupportedProperties(properties);
  // Set the supported relationships
  std::set<core::Relationship> relationships;
  relationships.insert(Success);
  setSupportedRelationships(relationships);
}

bool ConsumeMQTT::enqueueReceiveMQTTMsg(MQTTClient_message *message) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.size() >= maxQueueSize_) {
    logger_->log_debug("MQTT queue full");
    return false;
  } else {
    queue_.push_back(message);
    logger_->log_debug("enqueue MQTT message length %d", message->payloadlen);
    return true;
  }
}

void ConsumeMQTT::onSchedule(core::ProcessContext *context, core::ProcessSessionFactory *sessionFactory) {
  AbstractMQTTProcessor::onSchedule(context, sessionFactory);
  std::string value;
  int64_t valInt;
  value = "";
  if (context->getProperty(MaxQueueSize.getName(), value) && !value.empty() && core::Property::StringToInt(value, valInt)) {
    maxQueueSize_ = valInt;
    logger_->log_info("ConsumeMQTT: max queue size [%d]", maxQueueSize_);
  }
}

void ConsumeMQTT::onTrigger(const std::shared_ptr<core::ProcessContext> &context, const std::shared_ptr<core::ProcessSession> &session) {
  // reconnect if necessary
  reconnect();
  std::deque<MQTTClient_message *> msg_queue;
  getReceivedMQTTMsg(msg_queue);
  while (!msg_queue.empty()) {
    MQTTClient_message *message = msg_queue.front();
    std::shared_ptr<core::FlowFile> processFlowFile = session->create();
    ConsumeMQTT::WriteCallback callback(message);
    session->write(processFlowFile, &callback);
    if (callback.status_ < 0) {
      logger_->log_error("ConsumeMQTT fail for the flow with UUID %s", processFlowFile->getUUIDStr());
      session->remove(processFlowFile);
    } else {
      session->putAttribute(processFlowFile, MQTT_BROKER_ATTRIBUTE, uri_.c_str());
      session->putAttribute(processFlowFile, MQTT_TOPIC_ATTRIBUTE, topic_.c_str());
      logger_->log_debug("ConsumeMQTT processing success for the flow with UUID %s topic %s", processFlowFile->getUUIDStr(), topic_);
      session->transfer(processFlowFile, Success);
    }
    MQTTClient_freeMessage(&message);
    msg_queue.pop_front();
  }
}

} /* namespace processors */
} /* namespace minifi */
} /* namespace nifi */
} /* namespace apache */
} /* namespace org */
