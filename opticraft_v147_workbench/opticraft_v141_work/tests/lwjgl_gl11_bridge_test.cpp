#include "lwjgl_gl11_bridge.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace opti::gl;
int main(){
 LwjglGL11Bridge gl(16,16); bool presented=false;
 gl.set_present_callback([&](const std::uint32_t* p,int w,int h){presented=p&&w==16&&h==16;});
 gl.viewport(0,0,16,16); gl.clear_color(0,0,0,1); gl.clear(0x4000|0x0100);
 gl.matrix_mode(0x1701); gl.load_identity(); gl.matrix_mode(0x1700); gl.load_identity();
 gl.begin(4); gl.color4f(1,0,0,1); gl.vertex3f(-1,-1,0); gl.vertex3f(1,-1,0); gl.vertex3f(-1,1,0); gl.end();
 assert((gl.framebuffer()[3*16+2]&255u)>240u);
 gl.display_swap_buffers(); assert(presented);
 int vp[4]={}; assert(gl.get_integer(0x0BA2,vp,4)); assert(vp[2]==16&&vp[3]==16);
 auto ids=gl.gen_textures(1); assert(ids.size()==1); gl.bind_texture(0x0DE1,ids[0]);
 const std::uint8_t px[]={0,255,0,255}; gl.tex_image_2d(0x0DE1,0,4,1,1,0,0x1908,0x1401,px,sizeof(px));
 gl.enable(0x0DE1); gl.clear(0x4000);
 auto empty=gl.gen_textures(1); assert(empty.size()==1); gl.bind_texture(0x0DE1,empty[0]);
 gl.tex_image_2d(0x0DE1,0,4,2,2,0,0x1908,0x1401,nullptr,0);
 assert(gl.context().texture_store().get(empty[0])->complete());
 gl.bind_texture(0x0DE1,ids[0]);
 gl.begin(4); gl.color4f(1,1,1,1); gl.tex_coord2f(0,0); gl.vertex3f(-1,-1,0); gl.tex_coord2f(1,0); gl.vertex3f(1,-1,0); gl.tex_coord2f(0,1); gl.vertex3f(-1,1,0); gl.end();
 assert(((gl.framebuffer()[3*16+2]>>8)&255u)>240u);
 std::uint8_t read[4]{}; assert(gl.read_pixels_rgba8(2,3,1,1,read,sizeof(read))); assert(read[1]>240);
 // Reject oversized and overflowing rectangles before computing byte offsets.
 assert(!gl.read_pixels_rgba8(0,0,2147483647,1,read,sizeof(read)));
 assert(!gl.read_pixels_rgba8(2147483647,0,1,1,read,sizeof(read)));
 assert(!gl.read_pixels_rgba8(0,0,1,1,read,3));
 gl.enable(0xDEAD); assert(gl.get_error()==0x0500); assert(gl.get_error()==0);
 assert(gl.get_string(0x1F02)!=nullptr);
 std::cout<<"lwjgl_gl11_bridge_test: PASS\n";
}
