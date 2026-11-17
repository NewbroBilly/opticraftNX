#pragma once
#include "gl_compat_core.hpp"
#include <cstddef>
#include <cstdint>
namespace opti::gl {
constexpr std::uint32_t GL_VERTEX_ARRAY_=0x8074, GL_NORMAL_ARRAY_=0x8075,
 GL_COLOR_ARRAY_=0x8076, GL_TEXTURE_COORD_ARRAY_=0x8078;
constexpr std::uint32_t GL_BYTE_=0x1400, GL_UNSIGNED_BYTE_ARRAY_=0x1401, GL_SHORT_=0x1402,
 GL_UNSIGNED_SHORT_=0x1403, GL_INT_=0x1404, GL_UNSIGNED_INT_=0x1405,
 GL_FLOAT_=0x1406, GL_DOUBLE_=0x140A;
constexpr std::uint32_t GL_V2F_=0x2A20, GL_V3F_=0x2A21, GL_C4UB_V2F_=0x2A22,
 GL_C4UB_V3F_=0x2A23, GL_C3F_V3F_=0x2A24, GL_N3F_V3F_=0x2A25,
 GL_C4F_N3F_V3F_=0x2A26, GL_T2F_V3F_=0x2A27, GL_T4F_V4F_=0x2A28,
 GL_T2F_C4UB_V3F_=0x2A29, GL_T2F_C3F_V3F_=0x2A2A,
 GL_T2F_N3F_V3F_=0x2A2B, GL_T2F_C4F_N3F_V3F_=0x2A2C;
constexpr std::uint32_t GL_POINTS_=0, GL_LINES_=1, GL_LINE_LOOP_=2, GL_LINE_STRIP_=3,
 GL_TRIANGLES_=4, GL_TRIANGLE_STRIP_=5, GL_TRIANGLE_FAN_=6, GL_QUADS_=7;
enum class ArrayScalar : std::uint32_t { Byte=GL_BYTE_, UByte=GL_UNSIGNED_BYTE_ARRAY_, Short=GL_SHORT_,
 UShort=GL_UNSIGNED_SHORT_, Int=GL_INT_, UInt=GL_UNSIGNED_INT_, Float=GL_FLOAT_, Double=GL_DOUBLE_ };
class ClientArrays {
public:
 bool enable(std::uint32_t array,bool value);
 bool pointer(std::uint32_t array,int size,ArrayScalar type,int stride,const void* data);
 bool vertex_pointer(int n,ArrayScalar t,int s,const void* p){return pointer(GL_VERTEX_ARRAY_,n,t,s,p);}
 bool color_pointer(int n,ArrayScalar t,int s,const void* p){return pointer(GL_COLOR_ARRAY_,n,t,s,p);}
 bool texcoord_pointer(int n,ArrayScalar t,int s,const void* p){return pointer(GL_TEXTURE_COORD_ARRAY_,n,t,s,p);}
 bool normal_pointer(ArrayScalar t,int s,const void* p){return pointer(GL_NORMAL_ARRAY_,3,t,s,p);}
 bool interleaved_arrays(std::uint32_t format,int stride,const void* data);
 bool array_element(std::size_t index,Vertex& out) const;
 bool draw_arrays(std::uint32_t mode,int first,int count,DrawBatch& out) const;
 bool draw_elements(std::uint32_t mode,int count,ArrayScalar index_type,const void* indices,DrawBatch& out) const;
private:
 struct Array { bool enabled=false; int size=4,stride=0; ArrayScalar type=ArrayScalar::Float; const std::uint8_t* data=nullptr; };
 Array vertex_{},color_{},texcoord_{},normal_{};
 static std::size_t scalar_size(ArrayScalar);
 static bool mode_from_gl(std::uint32_t,Primitive&);
 static bool read_scalar(const Array&,std::size_t,int,float&,bool normalize=false);
 static bool read_vector(const Array&,std::size_t,Vec4&,bool);
 bool build(const std::uint32_t*,std::size_t,Primitive,DrawBatch&) const;
};
}
