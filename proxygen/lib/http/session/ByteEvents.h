/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "proxygen/lib/http/session/HTTPTransaction.h"
#include "proxygen/lib/utils/Time.h"

#include <folly/IntrusiveList.h>
#include <thrift/lib/cpp/async/TAsyncTimeoutSet.h>

namespace proxygen {

class ByteEvent {
 public:
  enum EventType {
    FIRST_BYTE,
    LAST_BYTE,
    PING_REPLY_SENT,
    FIRST_HEADER_BYTE,
  };

  ByteEvent(uint64_t byteOffset, EventType eventType)
      : eventType_(eventType), eomTracked_(0), byteOffset_(byteOffset) {}
  virtual ~ByteEvent() {}
  virtual HTTPTransaction* getTransaction() { return nullptr; }
  virtual int64_t getLatency() { return -1; }

  folly::IntrusiveListHook listHook;
  EventType eventType_:3; // packed w/ byteOffset_
  size_t eomTracked_:1;
  uint64_t byteOffset_:(8*sizeof(uint64_t)-4);
};

std::ostream& operator<<(std::ostream& os, const ByteEvent& txn);

class TransactionByteEvent : public ByteEvent {
 public:
  TransactionByteEvent(uint64_t byteNo,
                       EventType eventType,
                       HTTPTransaction::CallbackGuard cg)
      : ByteEvent(byteNo, eventType), cg_(cg) {}

  HTTPTransaction* getTransaction() override {
    return &(cg_.peekTransaction());
  }

  HTTPTransaction::CallbackGuard cg_; // refcounted transaction
};

class AckTimeout
    : public apache::thrift::async::TAsyncTimeoutSet::Callback {
 public:
  /**
   * The instances of AckTimeout::Callback *MUST* outlive the AckTimeout it is
   * registered on.
   */
  class Callback {
   public:
    virtual ~Callback() {}
    virtual void ackTimeoutExpired(uint64_t byteNo) noexcept = 0;
  };

  AckTimeout(Callback* callback, uint64_t byteNo)
      : callback_(callback), byteNo_(byteNo) {}

  void timeoutExpired() noexcept {
    callback_->ackTimeoutExpired(byteNo_);
  }

 private:
  Callback* callback_;
  uint64_t byteNo_;
};

class AckByteEvent : public TransactionByteEvent {
 public:
  AckByteEvent(AckTimeout::Callback* callback,
               uint64_t byteNo,
               EventType eventType,
               HTTPTransaction::CallbackGuard cg)
      : TransactionByteEvent(byteNo, eventType, cg),
        timeout(callback, byteNo) {}

  AckTimeout timeout;
};

class PingByteEvent : public ByteEvent {
 public:
  PingByteEvent(uint64_t byteOffset, TimePoint pingRequestReceivedTime)
      : ByteEvent(byteOffset, PING_REPLY_SENT),
        pingRequestReceivedTime_(pingRequestReceivedTime) {}

  int64_t getLatency() override;

  TimePoint pingRequestReceivedTime_;
};

} // proxygen
