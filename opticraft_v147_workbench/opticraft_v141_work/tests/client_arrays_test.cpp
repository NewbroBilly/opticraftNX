#include "client_arrays.hpp"
#include "gl_compat_core.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
using namespace opti::gl;
int main(){
 ClientArrays arrays;
 const float interleaved[]={
  -1,-1,0, 1,0,0, 0,0,
   1,-1,0, 0,1,0, 1,0,
  -1, 1,0, 0,0,1, 0,1,
   1, 1,0, 1,1,1, 1,1
 };
 constexpr int stride=8*int(sizeof(float));
 assert(arrays.vertex_pointer(3,ArrayScalar::Float,stride,interleaved));
 assert(arrays.color_pointer(3,ArrayScalar::Float,stride,interleaved+3));
 assert(arrays.texcoord_pointer(2,ArrayScalar::Float,stride,interleaved+6));
 assert(arrays.enable(GL_VERTEX_ARRAY_,true));
 assert(arrays.enable(GL_COLOR_ARRAY_,true));
 assert(arrays.enable(GL_TEXTURE_COORD_ARRAY_,true));
 DrawBatch b;
 assert(arrays.draw_arrays(GL_TRIANGLE_STRIP_,0,4,b));
 assert(b.vertices.size()==4 && b.indices.size()==6);
 assert(std::fabs(b.vertices[1].position.x-1)<1e-6f);
 assert(std::fabs(b.vertices[2].color.z-1)<1e-6f);
 assert(std::fabs(b.vertices[3].texcoord.y-1)<1e-6f);
 const std::uint16_t ix[]={0,1,2,2,1,3};
 assert(arrays.draw_elements(GL_TRIANGLES_,6,ArrayScalar::UShort,ix,b));
 assert(b.indices.size()==6 && b.indices[0]==0 && b.indices[3]==3 && b.indices[5]==5);
 CompatCore core;core.matrix_mode(0x1701);core.ortho(-1,1,-1,1,-1,1);core.transform(b);
 assert(std::fabs(b.vertices[0].clip_position.x+1)<1e-5f);
 assert(!arrays.draw_elements(GL_TRIANGLES_,3,ArrayScalar::Float,ix,b));
 assert(!arrays.draw_arrays(0xDEAD,0,3,b));
 assert(!arrays.draw_arrays(GL_TRIANGLES_, -1,3,b));
 ClientArrays disabled;
 assert(disabled.draw_arrays(GL_TRIANGLES_,0,3,b)==false);
 // Unsigned-byte color conversion and default alpha.
 const std::uint8_t colors[]={255,128,0, 128,64,32, 0,255,255};
 assert(arrays.color_pointer(3,ArrayScalar::UByte,0,colors));
 assert(arrays.draw_arrays(GL_TRIANGLES_,0,3,b));
 assert(b.vertices[0].color.x>0.99f && b.vertices[0].color.w==1.0f);

 // Integer vertex arrays are numeric coordinates, not normalized colors.
 const std::int16_t short_xyz[]={-1,-1,0, 1,-1,0, -1,1,0};
 assert(arrays.vertex_pointer(3,ArrayScalar::Short,0,short_xyz));
 assert(arrays.enable(GL_COLOR_ARRAY_,false));
 assert(arrays.draw_arrays(GL_TRIANGLES_,0,3,b));
 assert(std::fabs(b.vertices[0].position.x+1.0f)<1e-6f);

 // Integer color arrays are normalized; signed minimum maps to exactly -1.
 const std::int8_t signed_rgb[]={-128,0,127, 0,127,-128, 127,-128,0};
 assert(arrays.color_pointer(3,ArrayScalar::Byte,0,signed_rgb));
 assert(arrays.enable(GL_COLOR_ARRAY_,true));
 assert(arrays.draw_arrays(GL_TRIANGLES_,0,3,b));
 assert(std::fabs(b.vertices[0].color.x+1.0f)<1e-6f);
 assert(std::fabs(b.vertices[0].color.z-1.0f)<1e-6f);
 std::cout<<"client_arrays_test: PASS\\n";
}
