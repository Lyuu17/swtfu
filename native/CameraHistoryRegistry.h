#pragma once
#include <windows.h>
#include <map>
#include <limits>
#include "CameraRenderHistory.h"

namespace tfu {
// Owner must keep the registry alive throughout calls. Camera keys are opaque;
// retire before native storage is released. Caller supplies a fresh nonzero
// module/registry epoch on recreation. Native math runs without this lock.
class CameraHistoryRegistry {
public:
  struct Token {uintptr_t owner=0;uint64_t generation=0,epoch=0;};
private:
  struct Record {uint64_t generation=0,revision=0;CameraRenderHistory history;};
  SRWLOCK mutex_=SRWLOCK_INIT;
  struct Lock {SRWLOCK *mutex;explicit Lock(SRWLOCK *value):mutex(value){AcquireSRWLockExclusive(mutex);}~Lock(){ReleaseSRWLockExclusive(mutex);}};
  std::map<uintptr_t,Record> records_;
  uint64_t serial_=0;
  const uint64_t epoch_;
  auto Find(Token token)->std::map<uintptr_t,Record>::iterator {
    auto found=records_.find(token.owner);
    if(!epoch_||token.epoch!=epoch_||!token.owner||!token.generation||found==records_.end()||found->second.generation!=token.generation)return records_.end();
    return found;
  }
public:
  explicit CameraHistoryRegistry(uint64_t epoch):epoch_(epoch){}
  CameraHistoryRegistry(const CameraHistoryRegistry &)=delete;
  CameraHistoryRegistry &operator=(const CameraHistoryRegistry &)=delete;
  Token Begin(uintptr_t owner){
    Lock lock(&mutex_);if(!epoch_||!owner||serial_==(std::numeric_limits<uint64_t>::max)())return {};
    Record next;next.generation=++serial_;next.history.Begin(next.generation);
    records_[owner]=next;return {owner,next.generation,epoch_};
  }
  bool Retire(Token token){Lock lock(&mutex_);auto found=Find(token);if(found==records_.end())return false;records_.erase(found);return true;}
  Token Current(uintptr_t owner){Lock lock(&mutex_);auto found=records_.find(owner);return found==records_.end()?Token{}:Token{owner,found->second.generation,epoch_};}
  bool Capture(Token token,uint64_t step,const void *state,bool cut=false){
    Lock lock(&mutex_);auto found=Find(token);if(found==records_.end()||found->second.revision==(std::numeric_limits<uint64_t>::max)())return false;
    if(!found->second.history.Capture(token.generation,step,state,cut))return false;
    ++found->second.revision;return true;
  }
  bool Present(Token token,float alpha,const void *observed,void *output,CameraRenderHistory::Setter original){
    if(!observed||!output||!original)return false;
    const uintptr_t source=reinterpret_cast<uintptr_t>(observed),destination=reinterpret_cast<uintptr_t>(output);
    if((source<=destination?destination-source:source-destination)<0x220)return false;
    CameraRenderHistory snapshot;uint64_t revision=0;
    {Lock lock(&mutex_);auto found=Find(token);if(found==records_.end())return false;snapshot=found->second.history;revision=found->second.revision;}
    __declspec(align(16)) unsigned char privateState[0x220];
    if(!snapshot.BuildMatched(token.generation,alpha,observed,privateState,original))return false;
    Lock lock(&mutex_);auto found=Find(token);
    if(found==records_.end()||found->second.revision!=revision)return false;
    std::memcpy(output,privateState,sizeof(privateState));return true;
  }
};
}
