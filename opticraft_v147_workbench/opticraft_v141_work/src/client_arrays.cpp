#include "client_arrays.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>
namespace opti::gl {
std::size_t ClientArrays::scalar_size(ArrayScalar t) {
 switch(t){case ArrayScalar::Byte:case ArrayScalar::UByte:return 1;
 case ArrayScalar::Short:case ArrayScalar::UShort:return 2;
 case ArrayScalar::Int:case ArrayScalar::UInt:case ArrayScalar::Float:return 4;
 case ArrayScalar::Double:return 8;} return 0;
}
bool ClientArrays::mode_from_gl(std::uint32_t m,Primitive& p) {
 switch(m){case GL_POINTS_:p=POINTS;return true;case GL_LINES_:p=LINES;return true;
 case GL_LINE_LOOP_:p=LINE_LOOP;return true;case GL_LINE_STRIP_:p=LINE_STRIP;return true;
 case GL_TRIANGLES_:p=TRIANGLES;return true;case GL_TRIANGLE_STRIP_:p=TRIANGLE_STRIP;return true;
 case GL_TRIANGLE_FAN_:p=TRIANGLE_FAN;return true;case GL_QUADS_:p=QUADS;return true;default:return false;}
}
bool ClientArrays::enable(std::uint32_t a,bool v) {
 Array* p=nullptr;switch(a){case GL_VERTEX_ARRAY_:p=&vertex_;break;case GL_COLOR_ARRAY_:p=&color_;break;
 case GL_TEXTURE_COORD_ARRAY_:p=&texcoord_;break;case GL_NORMAL_ARRAY_:p=&normal_;break;default:return false;}
 p->enabled=v;return true;
}
bool ClientArrays::pointer(std::uint32_t a,int n,ArrayScalar t,int stride,const void* data) {
 Array* p=nullptr;
 switch(a){case GL_VERTEX_ARRAY_:p=&vertex_;if(n<2||n>4)return false;break;
 case GL_COLOR_ARRAY_:p=&color_;if(n!=3&&n!=4)return false;break;
 case GL_TEXTURE_COORD_ARRAY_:p=&texcoord_;if(n<1||n>4)return false;break;
 case GL_NORMAL_ARRAY_:p=&normal_;n=3;break;default:return false;}
 if(stride<0||!scalar_size(t))return false;
 p->size=n;p->type=t;p->stride=stride;p->data=static_cast<const std::uint8_t*>(data);return true;
}
bool ClientArrays::read_scalar(const Array& a,std::size_t vertex,int component,float& out,bool normalize) {
 if(!a.data||component<0||component>=a.size)return false;
 const std::size_t sz=scalar_size(a.type);
 const std::size_t step=a.stride?static_cast<std::size_t>(a.stride):static_cast<std::size_t>(a.size)*sz;
 if(vertex && step>std::numeric_limits<std::size_t>::max()/vertex)return false;
 const std::size_t base=vertex*step;
 const std::size_t offset=static_cast<std::size_t>(component)*sz;
 if(base>std::numeric_limits<std::size_t>::max()-offset)return false;
 const auto* p=a.data+base+offset;
 switch(a.type){
 case ArrayScalar::Byte:{std::int8_t v;std::memcpy(&v,p,1);out=normalize?std::max(-1.0f,float(v)/127.0f):float(v);return true;}
 case ArrayScalar::UByte:{std::uint8_t v;std::memcpy(&v,p,1);out=normalize?float(v)/255.0f:float(v);return true;}
 case ArrayScalar::Short:{std::int16_t v;std::memcpy(&v,p,2);out=normalize?std::max(-1.0f,float(v)/32767.0f):float(v);return true;}
 case ArrayScalar::UShort:{std::uint16_t v;std::memcpy(&v,p,2);out=normalize?float(v)/65535.0f:float(v);return true;}
 case ArrayScalar::Int:{std::int32_t v;std::memcpy(&v,p,4);out=normalize?std::max(-1.0f,float(double(v)/2147483647.0)):float(v);return true;}
 case ArrayScalar::UInt:{std::uint32_t v;std::memcpy(&v,p,4);out=normalize?float(double(v)/4294967295.0):float(v);return true;}
 case ArrayScalar::Float:std::memcpy(&out,p,4);return true;
 case ArrayScalar::Double:{double v;std::memcpy(&v,p,8);out=float(v);return true;}}
 return false;
}
bool ClientArrays::read_vector(const Array& a,std::size_t id,Vec4& out,bool color) {
 float v[4]={0,0,0,1};
 for(int c=0;c<a.size;c++)if(!read_scalar(a,id,c,v[c],color))return false;
 if(!color && a.size==2)v[2]=0;
 if(color && a.size==3)v[3]=1;
 out={v[0],v[1],v[2],v[3]};return true;
}
bool ClientArrays::build(const std::uint32_t* ids,std::size_t n,Primitive mode,DrawBatch& out) const {
 if(!vertex_.enabled||n>1000000)return false;
 DrawBatch b;b.source_mode=mode;b.vertices.resize(n);
 for(std::size_t i=0;i<n;i++){
  const std::size_t id=ids[i];auto& v=b.vertices[i];
  if(!read_vector(vertex_,id,v.position,false))return false;
  if(vertex_.size<4)v.position.w=1;
  v.color={1,1,1,1};v.texcoord={0,0,0,1};v.normal={0,0,1,0};
  if(color_.enabled&&!read_vector(color_,id,v.color,true))return false;
  if(texcoord_.enabled&&!read_vector(texcoord_,id,v.texcoord,false))return false;
  if(normal_.enabled){float nrm[3];for(int c=0;c<3;c++)if(!read_scalar(normal_,id,c,nrm[c]))return false;v.normal={nrm[0],nrm[1],nrm[2],0};}
 }
 auto tri=[&](std::uint32_t a,std::uint32_t c,std::uint32_t d){b.indices.insert(b.indices.end(),{a,c,d});};
 const auto count=static_cast<std::uint32_t>(n);
 switch(mode){
 case POINTS:break;
 case LINES:for(std::uint32_t i=0;i+1<count;i+=2)b.indices.insert(b.indices.end(),{i,i+1});break;
 case LINE_STRIP:for(std::uint32_t i=0;i+1<count;i++)b.indices.insert(b.indices.end(),{i,i+1});break;
 case LINE_LOOP:if(count>1)for(std::uint32_t i=0;i<count;i++)b.indices.insert(b.indices.end(),{i,(i+1)%count});break;
 case TRIANGLES:for(std::uint32_t i=0;i+2<count;i+=3)tri(i,i+1,i+2);break;
 case TRIANGLE_STRIP:for(std::uint32_t i=0;i+2<count;i++)(i&1)?tri(i+1,i,i+2):tri(i,i+1,i+2);break;
 case TRIANGLE_FAN:for(std::uint32_t i=1;i+1<count;i++)tri(0,i,i+1);break;
 case QUADS:for(std::uint32_t i=0;i+3<count;i+=4){tri(i,i+1,i+2);tri(i,i+2,i+3);}break;
 }
 out=std::move(b);return true;
}
bool ClientArrays::interleaved_arrays(std::uint32_t format,int stride,const void* data) {
 if(stride<0||!data)return false;
 struct Field { int size; ArrayScalar type; std::size_t offset; bool present; };
 Field v{0,ArrayScalar::Float,0,false},c{0,ArrayScalar::Float,0,false},
       t{0,ArrayScalar::Float,0,false},n{0,ArrayScalar::Float,0,false};
 std::size_t total=0;
 const auto f32=sizeof(float),u8=sizeof(std::uint8_t);
 switch(format) {
 case GL_V2F_:v={2,ArrayScalar::Float,0,true};total=2*f32;break;
 case GL_V3F_:v={3,ArrayScalar::Float,0,true};total=3*f32;break;
 case GL_C4UB_V2F_:c={4,ArrayScalar::UByte,0,true};v={2,ArrayScalar::Float,4*u8,true};total=4*u8+2*f32;break;
 case GL_C4UB_V3F_:c={4,ArrayScalar::UByte,0,true};v={3,ArrayScalar::Float,4*u8,true};total=4*u8+3*f32;break;
 case GL_C3F_V3F_:c={3,ArrayScalar::Float,0,true};v={3,ArrayScalar::Float,3*f32,true};total=6*f32;break;
 case GL_N3F_V3F_:n={3,ArrayScalar::Float,0,true};v={3,ArrayScalar::Float,3*f32,true};total=6*f32;break;
 case GL_C4F_N3F_V3F_:c={4,ArrayScalar::Float,0,true};n={3,ArrayScalar::Float,4*f32,true};v={3,ArrayScalar::Float,7*f32,true};total=10*f32;break;
 case GL_T2F_V3F_:t={2,ArrayScalar::Float,0,true};v={3,ArrayScalar::Float,2*f32,true};total=5*f32;break;
 case GL_T4F_V4F_:t={4,ArrayScalar::Float,0,true};v={4,ArrayScalar::Float,4*f32,true};total=8*f32;break;
 case GL_T2F_C4UB_V3F_:t={2,ArrayScalar::Float,0,true};c={4,ArrayScalar::UByte,2*f32,true};v={3,ArrayScalar::Float,2*f32+4*u8,true};total=2*f32+4*u8+3*f32;break;
 case GL_T2F_C3F_V3F_:t={2,ArrayScalar::Float,0,true};c={3,ArrayScalar::Float,2*f32,true};v={3,ArrayScalar::Float,5*f32,true};total=8*f32;break;
 case GL_T2F_N3F_V3F_:t={2,ArrayScalar::Float,0,true};n={3,ArrayScalar::Float,2*f32,true};v={3,ArrayScalar::Float,5*f32,true};total=8*f32;break;
 case GL_T2F_C4F_N3F_V3F_:t={2,ArrayScalar::Float,0,true};c={4,ArrayScalar::Float,2*f32,true};n={3,ArrayScalar::Float,6*f32,true};v={3,ArrayScalar::Float,9*f32,true};total=12*f32;break;
 default:return false;
 }
 const int actual_stride=stride?stride:static_cast<int>(total);
 const auto* base=static_cast<const std::uint8_t*>(data);
 auto set=[&](std::uint32_t array,const Field& field) {
  if(field.present) {
   if(!pointer(array,field.size,field.type,actual_stride,base+field.offset))return false;
   return enable(array,true);
  }
  return enable(array,false);
 };
 return set(GL_VERTEX_ARRAY_,v)&&set(GL_COLOR_ARRAY_,c)&&
        set(GL_TEXTURE_COORD_ARRAY_,t)&&set(GL_NORMAL_ARRAY_,n);
}
bool ClientArrays::array_element(std::size_t index,Vertex& out) const {
 if(!vertex_.enabled||!read_vector(vertex_,index,out.position,false))return false;
 if(vertex_.size<4)out.position.w=1.0f;
 out.color={1,1,1,1};out.texcoord={0,0,0,1};out.normal={0,0,1,0};
 if(color_.enabled&&!read_vector(color_,index,out.color,true))return false;
 if(texcoord_.enabled&&!read_vector(texcoord_,index,out.texcoord,false))return false;
 if(normal_.enabled) {
  float xyz[3];
  for(int i=0;i<3;i++)if(!read_scalar(normal_,index,i,xyz[i]))return false;
  out.normal={xyz[0],xyz[1],xyz[2],0};
 }
 return true;
}
bool ClientArrays::draw_arrays(std::uint32_t mode,int first,int count,DrawBatch& out) const {
 Primitive p;if(!mode_from_gl(mode,p)||first<0||count<0||count>1000000)return false;
 std::vector<std::uint32_t> ids(static_cast<std::size_t>(count));
 for(int i=0;i<count;i++)ids[static_cast<std::size_t>(i)]=static_cast<std::uint32_t>(first+i);
 return build(ids.data(),ids.size(),p,out);
}
bool ClientArrays::draw_elements(std::uint32_t mode,int count,ArrayScalar t,const void* indices,DrawBatch& out) const {
 Primitive p;if(!mode_from_gl(mode,p)||count<0||count>1000000||(!indices&&count))return false;
 std::size_t sz=0;switch(t){case ArrayScalar::UByte:sz=1;break;case ArrayScalar::UShort:sz=2;break;case ArrayScalar::UInt:sz=4;break;default:return false;}
 std::vector<std::uint32_t> ids(static_cast<std::size_t>(count));const auto* src=static_cast<const std::uint8_t*>(indices);
 for(int i=0;i<count;i++){std::uint32_t v=0;std::memcpy(&v,src+static_cast<std::size_t>(i)*sz,sz);ids[static_cast<std::size_t>(i)]=v;}
 return build(ids.data(),ids.size(),p,out);
}
}
