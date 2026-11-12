#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

namespace tfu {
// Raw successful 20-byte mouse snapshots, before mapping or response curves.
// Caller serializes access and supplies a fresh epoch on device/focus changes.
// This component neither polls devices nor writes native input storage.
struct RawMouseSnapshot {
  int32_t x, y, wheel;
  uint8_t buttons[8];
};
static_assert(sizeof(RawMouseSnapshot)==20,"Native raw mouse snapshot size");
struct MouseButtonEdge {
  uint32_t poll;
  uint8_t button, down;
};
class RawMouseRetention {
public:
  static constexpr uint32_t Capacity=256;
  struct Batch {
    int64_t x=0,y=0,wheel=0;
    uint32_t polls=0,edgeCount=0;
    uint8_t buttons[8]={};
    std::array<MouseButtonEdge,Capacity*8> edges{};
  };
private:
  uintptr_t device_=0;
  uint64_t epoch_=0,lastEpoch_=0;
  uint32_t count_=0;
  uint8_t held_[8]={};
  std::array<RawMouseSnapshot,Capacity> pending_{};
  bool Matches(uintptr_t device,uint64_t epoch) const {
    return device&&epoch&&device==device_&&epoch==epoch_;
  }
public:
  bool Begin(uintptr_t device,uint64_t epoch,const uint8_t (&buttons)[8]) {
    if(!device||!epoch||epoch<=lastEpoch_)return false;
    device_=device;epoch_=lastEpoch_=epoch;count_=0;
    std::memcpy(held_,buttons,8);return true;
  }
  void Retire(){device_=0;epoch_=0;count_=0;std::memset(held_,0,8);}
  bool Append(uintptr_t device,uint64_t epoch,const RawMouseSnapshot &snapshot) {
    // Failure preserves every queued snapshot; the caller must drain/fallback.
    if(!Matches(device,epoch)||count_==Capacity)return false;
    pending_[count_++]=snapshot;return true;
  }
private:
  Batch BuildBatch() const {
    Batch next;std::memcpy(next.buttons,held_,8);next.polls=count_;
    for(uint32_t poll=0;poll<count_;++poll){
      const auto &snapshot=pending_[poll];
      next.x+=snapshot.x;next.y+=snapshot.y;next.wheel+=snapshot.wheel;
      for(uint8_t button=0;button<8;++button){
        const bool down=(snapshot.buttons[button]&0x80)!=0;
        if(down!=((next.buttons[button]&0x80)!=0)){
          auto &edge=next.edges[next.edgeCount++];
          edge.poll=poll;edge.button=button;edge.down=down?1:0;
        }
        next.buttons[button]=snapshot.buttons[button];
      }
    }
    return next;
  }
  void Commit(const Batch &next){std::memcpy(held_,next.buttons,8);count_=0;}
public:
  bool Drain(uintptr_t device,uint64_t epoch,Batch *output) {
    if(!output||!Matches(device,epoch))return false;
    const Batch next=BuildBatch();*output=next;Commit(next);return true;
  }
  bool DrainForNative(uintptr_t device,uint64_t epoch,Batch *output,
                      RawMouseSnapshot *snapshot) {
    if(!output||!snapshot||!Matches(device,epoch))return false;
    const Batch next=BuildBatch();
    const int64_t low=std::numeric_limits<int32_t>::min();
    const int64_t high=std::numeric_limits<int32_t>::max();
    if(next.x<low||next.x>high||next.y<low||next.y>high||
       next.wheel<low||next.wheel>high)return false;
    // Failure above preserves queue and both outputs for caller fallback.
    RawMouseSnapshot native={static_cast<int32_t>(next.x),
      static_cast<int32_t>(next.y),static_cast<int32_t>(next.wheel),{}};
    std::memcpy(native.buttons,next.buttons,8);
    *snapshot=native;*output=next;Commit(next);return true;
  }
  uint32_t Pending() const{return count_;}
};
}
