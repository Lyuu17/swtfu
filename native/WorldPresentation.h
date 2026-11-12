#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

// Restricted adapter for validated B96D60 matrices and command flag constants.
// Caller owns readable command/instance/descriptor storage for the whole call
// and supplies the validated original, never a hooked entry. No live hook uses
// this adapter yet. A false return means no native call and no state changes.
namespace tfu {
using WorldShaderCall = void (__thiscall *)(void *,void *,void *,void *,void *);
inline bool InvokePrivateWorld(void *command,const float *world,void *device,
    void *descriptor,void *parameters,void *extras,WorldShaderCall original) {
  static_assert(sizeof(void *)==4,"Native world adapter is x86 only");
  if(!command||!world||!device||!descriptor||!parameters||!extras||!original)return false;
  auto source=static_cast<const uint32_t *>(command);
  auto instance=reinterpret_cast<const unsigned char *>(source[0]);
  auto shader=static_cast<const uint32_t *>(descriptor);
  auto list=static_cast<const uint32_t *>(parameters);
  if(!instance||(source[0x1C/4]&4)||(instance[0xEC]&0x10))return false;
  // B42E00 forwards this list and descriptor shader ID to B42720 without
  // reading the command/instance. Keep the original list and its native path.
  if(list[1]!=list[2]&&(list[1]<0x10000||list[2]<list[1]||
      (list[1]&3)||(list[2]&3)))return false;
  // +24 material and +88 geometry/cache paths remain unvalidated. Camera
  // groups read only the original camera service; preserve complete groups.
  if(shader[0x24/4]!=UINT32_MAX)return false;
  const unsigned starts[]={0x34,0x44,0x50,0x58},counts[]={4,3,2,1};
  for(unsigned group=0;group<4;++group){const bool enabled=shader[starts[group]/4]!=UINT32_MAX;
    for(unsigned i=1;i<counts[group];++i)
      if((shader[starts[group]/4+i]!=UINT32_MAX)!=enabled)return false;}
  // +8C uploads command+80 bit0 as float4 zero/one. The private command
  // preserves this flag; unlike +88 it has no geometry/cache dependency.
  if(shader[0x88/4]!=UINT32_MAX)return false;
  for(unsigned i=0;i<16;++i)if(!std::isfinite(world[i]))return false;
  if(world[3]!=0||world[7]!=0||world[11]!=0||world[15]!=1)return false;
  const double determinant=double(world[0])*(double(world[5])*world[10]-double(world[6])*world[9])
    -double(world[1])*(double(world[4])*world[10]-double(world[6])*world[8])
    +double(world[2])*(double(world[4])*world[9]-double(world[5])*world[8]);
  if(!std::isfinite(determinant)||std::fabs(determinant)<1e-8)return false;
  __declspec(align(16)) unsigned char privateInstance[0x140];
  __declspec(align(16)) uint32_t privateCommand[0x84/4];
  __declspec(align(16)) float privateCombined[16]={};
  std::memcpy(privateInstance,instance,sizeof(privateInstance));
  std::memcpy(privateCommand,command,sizeof(privateCommand));
  std::memcpy(privateInstance+0x60,world,64);
  privateCommand[0]=reinterpret_cast<uint32_t>(privateInstance);
  privateCommand[1]=reinterpret_cast<uint32_t>(privateCombined);
  *reinterpret_cast<uint32_t *>(privateInstance+0x134)=reinterpret_cast<uint32_t>(privateCommand);
  original(privateCommand,device,descriptor,parameters,extras);
  return true;
}
}
