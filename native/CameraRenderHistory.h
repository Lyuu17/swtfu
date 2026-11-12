#pragma once
#include "WorldRenderHistory.h"

namespace tfu {
// Supported rigid view matrices and fixed projection only. Caller owns camera
// identity/completion synchronization. No game pointers or live hooks retained.
class CameraRenderHistory {
public:
  using Setter=void (__thiscall *)(void *,const float *);
private:
  WorldRenderHistory poses_;
  uint64_t generation_=0,step_=0;
  bool ready_=false;
  unsigned char current_[0x220]={};
  float previousView_[16]={},currentView_[16]={};
  static bool InvertRigid(const float *matrix,float *inverse){
    world_detail::Transform pose;if(!world_detail::Decompose(matrix,pose))return false;
    for(double scale:pose.scale)if(std::fabs(scale-1)>1e-4)return false;
    float result[16]={};result[15]=1;
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)result[r*4+c]=matrix[c*4+r];
    for(unsigned c=0;c<3;++c){double value=0;for(unsigned r=0;r<3;++r)value-=double(matrix[12+r])*result[r*4+c];result[12+c]=static_cast<float>(value);}
    for(float v:result)if(!std::isfinite(v))return false;
    std::memcpy(inverse,result,64);return true;
  }
  static bool ValidProjection(const float *matrix){
    double rows[4][4];for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){const float v=matrix[r*4+c];if(!std::isfinite(v))return false;rows[r][c]=v;}
    for(unsigned column=0;column<4;++column){unsigned pivot=column;for(unsigned r=column+1;r<4;++r)if(std::fabs(rows[r][column])>std::fabs(rows[pivot][column]))pivot=r;
      if(std::fabs(rows[pivot][column])<1e-12)return false;
      for(unsigned c=0;c<4;++c){const double temp=rows[column][c];rows[column][c]=rows[pivot][c];rows[pivot][c]=temp;}
      const double divisor=rows[column][column];for(unsigned c=column;c<4;++c)rows[column][c]/=divisor;
      for(unsigned r=column+1;r<4;++r){const double scale=rows[r][column];for(unsigned c=column;c<4;++c)rows[r][c]-=scale*rows[column][c];}
    }return true;
  }
  static bool InversePair(const unsigned char *state,unsigned matrix,unsigned inverse){
    float a[16],b[16];std::memcpy(a,state+matrix,64);std::memcpy(b,state+inverse,64);
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){double value=0;for(unsigned k=0;k<4;++k)value+=double(a[r*4+k])*b[k*4+c];if(std::fabs(value-(r==c?1.:0.))>1e-3)return false;}return true;
  }
public:
  bool Begin(uint64_t generation){if(!generation)return false;generation_=generation;step_=0;ready_=false;return poses_.Begin(generation);}
  void Retire(){generation_=step_=0;ready_=false;poses_.Retire();}
  bool Capture(uint64_t generation,uint64_t step,const void *state,bool cut=false){
    if(!state||!generation||generation!=generation_||!step||(ready_&&step<step_))return false;
    const auto *bytes=static_cast<const unsigned char *>(state);
    float view[16],projection[16],world[16];std::memcpy(view,bytes,64);std::memcpy(projection,bytes+128,64);
    if(!InvertRigid(view,world)||!ValidProjection(projection))return false;
    if(ready_&&step==step_&&!cut)return std::memcmp(current_,bytes,sizeof(current_))==0;
    const bool reset=!ready_||cut||step-step_!=1||std::memcmp(current_+128,bytes+128,64)!=0;
    if(!poses_.Capture(generation,step,world,reset))return false;
    std::memcpy(previousView_,reset?view:currentView_,64);std::memcpy(currentView_,view,64);
    std::memcpy(current_,bytes,sizeof(current_));step_=step;ready_=true;return true;
  }
  bool Build(uint64_t generation,float alpha,void *output,Setter original)const{
    if(!output||!original||!ready_||generation!=generation_)return false;
    __declspec(align(16)) float world[16],view[16];
    if(!poses_.Sample(generation,alpha,world))return false;
    if(alpha==0||alpha==1)std::memcpy(view,alpha==0?previousView_:currentView_,64);
    else if(!InvertRigid(world,view))return false;
    __declspec(align(16)) unsigned char privateState[0x220];std::memcpy(privateState,current_,sizeof(privateState));
    original(privateState,view);
    if(std::memcmp(privateState,view,64)!=0)return false;
    for(unsigned offset=0;offset<sizeof(privateState);offset+=4){if(offset>=352&&offset<416)continue;float value;std::memcpy(&value,privateState+offset,4);if(!std::isfinite(value))return false;}
    if(!InversePair(privateState,0,64)||!InversePair(privateState,128,192)||!InversePair(privateState,416,480))return false;
    for(unsigned plane=0;plane<6;++plane){float values[4];std::memcpy(values,privateState+256+plane*16,16);const double norm=double(values[0])*values[0]+double(values[1])*values[1]+double(values[2])*values[2];if(std::fabs(norm-1)>1e-3)return false;}
    std::memcpy(output,privateState,sizeof(privateState));return true;
  }
  bool BuildMatched(uint64_t generation,float alpha,const void *observed,
                    void *output,Setter original)const{
    if(!ready_||!generation||generation!=generation_||!observed||!output)return false;
    const uintptr_t source=reinterpret_cast<uintptr_t>(observed),destination=reinterpret_cast<uintptr_t>(output);
    if((source<=destination?destination-source:source-destination)<sizeof(current_))return false;
    if(std::memcmp(current_,observed,sizeof(current_))!=0)return false;
    return Build(generation,alpha,output,original);
  }
};
}
