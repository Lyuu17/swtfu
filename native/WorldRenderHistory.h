#pragma once
#include <cstdint>
#include <cstring>
#include "WorldInterpolation.h"

namespace tfu {
// Owned by a lifecycle record. Caller serializes access and captures only stable
// completed simulation transforms. Step IDs belong to simulation, not rendering.
class WorldRenderHistory {
  uint64_t generation_=0,step_=0;
  bool ready_=false;
  float previous_[16]={},current_[16]={};
public:
  bool Begin(uint64_t generation){
    if(!generation)return false;
    generation_=generation;step_=0;ready_=false;return true;
  }
  void Retire(){generation_=step_=0;ready_=false;}
  bool Capture(uint64_t generation,uint64_t step,const float *matrix,bool discontinuity=false){
    if(!generation||generation!=generation_||!step||(ready_&&step<step_))return false;
    world_detail::Transform validated;if(!world_detail::Decompose(matrix,validated))return false;
    if(ready_&&step==step_&&!discontinuity)return std::memcmp(current_,matrix,64)==0;
    if(!ready_||discontinuity||step-step_!=1)std::memcpy(previous_,matrix,64);
    else std::memcpy(previous_,current_,64);
    std::memcpy(current_,matrix,64);step_=step;ready_=true;return true;
  }
  bool Sample(uint64_t generation,float alpha,float *output)const{
    return generation&&generation==generation_&&ready_&&InterpolateWorld(previous_,current_,alpha,output);
  }
};
}
