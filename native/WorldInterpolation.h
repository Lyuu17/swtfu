#pragma once
#include <cmath>
#include <cstring>

namespace tfu {
namespace world_detail {
struct Transform {double position[3],scale[3],q[4];};
inline bool Decompose(const float *m,Transform &t) {
  if(!m)return false;
  for(unsigned i=0;i<16;++i)if(!std::isfinite(m[i]))return false;
  if(m[3]!=0||m[7]!=0||m[11]!=0||m[15]!=1)return false;
  double row[3][3];
  for(unsigned r=0;r<3;++r){
    double norm=0;for(unsigned c=0;c<3;++c)norm+=double(m[r*4+c])*m[r*4+c];
    t.scale[r]=std::sqrt(norm);if(t.scale[r]<1e-8)return false;
    for(unsigned c=0;c<3;++c)row[r][c]=m[r*4+c]/t.scale[r];
    t.position[r]=m[12+r];
  }
  for(unsigned r=0;r<3;++r)for(unsigned c=r+1;c<3;++c){
    double dot=0;for(unsigned k=0;k<3;++k)dot+=row[r][k]*row[c][k];
    if(std::fabs(dot)>1e-4)return false;
  }
  const double det=row[0][0]*(row[1][1]*row[2][2]-row[1][2]*row[2][1])
    -row[0][1]*(row[1][0]*row[2][2]-row[1][2]*row[2][0])
    +row[0][2]*(row[1][0]*row[2][1]-row[1][1]*row[2][0]);
  if(det<0.9999||det>1.0001)return false;
  // Convert the transpose to a column-vector rotation quaternion.
  double a[3][3];for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)a[r][c]=row[c][r];
  const double trace=a[0][0]+a[1][1]+a[2][2];
  if(trace>0){const double s=2*std::sqrt(trace+1);t.q[3]=s/4;t.q[0]=(a[2][1]-a[1][2])/s;t.q[1]=(a[0][2]-a[2][0])/s;t.q[2]=(a[1][0]-a[0][1])/s;}
  else {
    unsigned i=0;if(a[1][1]>a[i][i])i=1;if(a[2][2]>a[i][i])i=2;
    const unsigned j=(i+1)%3,k=(i+2)%3;const double s=2*std::sqrt(1+a[i][i]-a[j][j]-a[k][k]);
    if(s<1e-12)return false;
    t.q[i]=s/4;t.q[j]=(a[j][i]+a[i][j])/s;t.q[k]=(a[k][i]+a[i][k])/s;t.q[3]=(a[k][j]-a[j][k])/s;
  }
  double norm=0;for(double v:t.q)norm+=v*v;
  if(!std::isfinite(norm)||norm<1e-20)return false;
  for(double &v:t.q)v/=std::sqrt(norm);
  return true;
}
}
// Completed stable snapshots only. No history/lifetime synchronization here.
// Rejects shear, reflections, degenerate/projective matrices. Output commits
// atomically after validation; unsupported input leaves it byte-identical.
inline bool InterpolateWorld(const float *previous,const float *current,float alpha,float *output) {
  if(!output||!std::isfinite(alpha)||alpha<0||alpha>1)return false;
  world_detail::Transform a,b;if(!world_detail::Decompose(previous,a)||!world_detail::Decompose(current,b))return false;
  if(alpha==0||alpha==1){std::memmove(output,alpha==0?previous:current,64);return true;}
  double dot=0;for(unsigned i=0;i<4;++i)dot+=a.q[i]*b.q[i];
  if(dot<0){dot=-dot;for(double &v:b.q)v=-v;}if(dot>1)dot=1;
  double wa=1-double(alpha),wb=alpha;
  if(dot<0.9995){const double angle=std::acos(dot),denominator=std::sin(angle);wa=std::sin((1-double(alpha))*angle)/denominator;wb=std::sin(double(alpha)*angle)/denominator;}
  double q[4],norm=0;for(unsigned i=0;i<4;++i){q[i]=wa*a.q[i]+wb*b.q[i];norm+=q[i]*q[i];}
  if(!std::isfinite(norm)||norm<1e-20)return false;
  for(double &v:q)v/=std::sqrt(norm);
  const double x=q[0],y=q[1],z=q[2],w=q[3];
  const double rows[3][3]={{1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w)},
    {2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w)},
    {2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y)}};
  float next[16]={};next[15]=1;
  for(unsigned r=0;r<3;++r){
    const double scale=(1-double(alpha))*a.scale[r]+double(alpha)*b.scale[r];
    for(unsigned c=0;c<3;++c)next[r*4+c]=static_cast<float>(scale*rows[r][c]);
    next[12+r]=static_cast<float>((1-double(alpha))*a.position[r]+double(alpha)*b.position[r]);
  }
  for(float v:next)if(!std::isfinite(v))return false;
  std::memcpy(output,next,64);return true;
}
}
