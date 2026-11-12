#pragma once
#include <windows.h>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <utility>
#include <vector>
namespace tfu {
// Callback ownership belongs to the retaining router. Bind BEFORE native
// command visibility; invoke exactly once for the same queue epoch/slot/word.
// This registry neither retains clock leases nor publishes native commands.
// Storage outlives native producers/callbacks; no destructor cancels work.
class QueueClockBindings {
public:
  using Callback=void (__cdecl *)(void *,int);
  struct Token {uintptr_t address=0;uint64_t epoch=0;};
  struct Publication {Token queue;uint64_t id=0;};
  struct Snapshot {unsigned queues,bindings,pending,running,finished,faulted,retiring,producers,publicationFaults;};
private:
  enum class Phase {Empty,Pending,Running,Finished,Faulted};
  struct Binding {uint64_t command=0;Callback callback=nullptr;void *context=nullptr;int argument=0;Phase phase=Phase::Empty;};
  struct Queue {uint64_t epoch;bool retiring=false,failed=false;unsigned producers=0,publicationFaults=0;std::vector<Binding> bindings;Queue(uint64_t value,unsigned capacity):epoch(value),bindings(capacity){}};
  struct Producer {Token queue;DWORD creator;};
  SRWLOCK mutex_=SRWLOCK_INIT;
  struct Lock {SRWLOCK *p;explicit Lock(SRWLOCK *value):p(value){AcquireSRWLockExclusive(p);}~Lock(){ReleaseSRWLockExclusive(p);}};
  std::map<uintptr_t,Queue> queues_;
  std::map<uint64_t,Producer> producers_;
  uint64_t serial_=0,producerSerial_=0;
  auto Find(Token token)->std::map<uintptr_t,Queue>::iterator{auto q=queues_.find(token.address);return q!=queues_.end()&&q->second.epoch==token.epoch?q:queues_.end();}
  bool Start(Token token,unsigned slot,uint64_t command,Binding *output){
    Lock lock(&mutex_);auto q=Find(token);if(q==queues_.end())return false;
    if(slot>=q->second.bindings.size())return false;auto &b=q->second.bindings[slot];if(b.phase!=Phase::Pending||b.command!=command)return false;
    b.phase=Phase::Running;*output=b;return true;
  }
  void Finish(Token token,unsigned slot,bool success){
    Lock lock(&mutex_);auto q=Find(token);if(q==queues_.end())return;
    if(slot>=q->second.bindings.size())return;auto &b=q->second.bindings[slot];if(b.phase!=Phase::Running)return;
    b.phase=success?Phase::Finished:Phase::Faulted;
    b.callback=nullptr;b.context=nullptr;if(!success)q->second.failed=true;
  }
  static bool Attach(Queue &queue,unsigned slot,uint64_t command,Callback callback,void *context,int argument){
    if(slot>=queue.bindings.size()||!callback||queue.bindings[slot].phase!=Phase::Empty)return false;
    queue.bindings[slot]=Binding{command,callback,context,argument,Phase::Pending};return true;
  }
public:
  QueueClockBindings()=default;
  QueueClockBindings(const QueueClockBindings &)=delete;
  QueueClockBindings &operator=(const QueueClockBindings &)=delete;
  bool Register(uintptr_t address,Token *output){return Register(address,65536,output);}
  // Preallocate slot metadata BEFORE native queue publication/producer locks.
  bool Register(uintptr_t address,unsigned capacity,Token *output){
    if(!address||!output||!capacity||capacity>65536)return false;
    try {Lock lock(&mutex_);if(queues_.size()>=65535||queues_.count(address)||serial_==(std::numeric_limits<uint64_t>::max)())return false;
      const uint64_t epoch=++serial_;queues_.emplace(std::piecewise_construct,std::forward_as_tuple(address),std::forward_as_tuple(epoch,capacity));*output={address,epoch};return true;
    }catch(...){return false;}
  }
  bool Bind(Token token,unsigned slot,uint64_t command,Callback callback,void *context,int argument){
    Lock lock(&mutex_);auto q=Find(token);if(q==queues_.end()||q->second.retiring||q->second.failed)return false;
    return Attach(q->second,slot,command,callback,context,argument);
  }
  // Begin before entering native producer code. Allocation failure leaves the
  // output untouched and can be reported as confirmed unpublished rejection.
  bool BeginPublication(Token token,Publication *output){
    if(!output)return false;
    try {Lock lock(&mutex_);auto q=Find(token);if(q==queues_.end()||q->second.retiring||q->second.failed||q->second.producers==65535||producerSerial_==(std::numeric_limits<uint64_t>::max)())return false;
      const uint64_t id=++producerSerial_;producers_.emplace(id,Producer{token,GetCurrentThreadId()});++q->second.producers;*output={token,id};return true;
    }catch(...){return false;}
  }
  // No allocation here. A producer admitted before retirement may still bind
  // its original command, even while new producer admission is closed.
  bool BindPublication(Publication publication,unsigned slot,uint64_t command,Callback callback,void *context,int argument){
    Lock lock(&mutex_);auto p=producers_.find(publication.id);auto q=Find(publication.queue);
    if(p==producers_.end()||q==queues_.end()||p->second.creator!=GetCurrentThreadId()||p->second.queue.address!=publication.queue.address||p->second.queue.epoch!=publication.queue.epoch||q->second.failed)return false;
    return Attach(q->second,slot,command,callback,context,argument);
  }
  bool EndPublication(Publication publication,bool nativeReturned){
    Lock lock(&mutex_);auto p=producers_.find(publication.id);auto q=Find(publication.queue);
    if(p==producers_.end()||q==queues_.end()||p->second.creator!=GetCurrentThreadId()||p->second.queue.address!=publication.queue.address||p->second.queue.epoch!=publication.queue.epoch)return false;
    --q->second.producers;if(!nativeReturned){q->second.failed=true;++q->second.publicationFaults;}producers_.erase(p);return true;
  }
  bool Invoke(Token token,unsigned slot,uint64_t command){
    Binding binding={};if(!Start(token,slot,command,&binding))return false;bool success=false;
    __try {binding.callback(binding.context,binding.argument);success=true;}
    __finally {Finish(token,slot,success);}
    return true;
  }
  bool Retire(Token token){Lock lock(&mutex_);auto q=Find(token);if(q==queues_.end())return false;q->second.retiring=true;return true;}
  // Only AFTER original queue completion, producer/worker joins, outstanding
  // fault reconciliation and history/time cleanup. Callback return alone is
  // insufficient: native completion instructions may still be executing.
  bool ReconcileAfterNativeQuiescence(Token token){
    Lock lock(&mutex_);auto q=Find(token);if(q==queues_.end()||!q->second.retiring||q->second.producers)return false;
    for(const auto &b:q->second.bindings)if(b.phase==Phase::Pending||b.phase==Phase::Running)return false;
    queues_.erase(q);return true;
  }
  Snapshot Read(){
    Lock lock(&mutex_);Snapshot value={};value.queues=static_cast<unsigned>(queues_.size());
    for(const auto &q:queues_){if(q.second.retiring)++value.retiring;value.producers+=q.second.producers;value.publicationFaults+=q.second.publicationFaults;for(const auto &b:q.second.bindings){if(b.phase!=Phase::Empty)++value.bindings;switch(b.phase){case Phase::Empty:break;case Phase::Pending:++value.pending;break;case Phase::Running:++value.running;break;case Phase::Finished:++value.finished;break;case Phase::Faulted:++value.faulted;break;}}}
    return value;
  }
};
}
