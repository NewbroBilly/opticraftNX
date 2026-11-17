#include "gl_texture_api.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace opti::gl;
int main(){
 TextureStore store;TextureApi gl(store);
 auto ids=gl.gen(2);assert(ids.size()==2&&ids[0]&&ids[1]!=ids[0]);
 assert(gl.is_texture(ids[0]));assert(gl.bind(GL_TEXTURE_2D_,ids[0]));
 assert(gl.pixel_store(GL_UNPACK_ALIGNMENT_,1));
 const std::uint8_t tex[]={255,0,0, 0,255,0, 0,0,255, 255,255,255};
 assert(gl.image_2d(GL_TEXTURE_2D_,0,4,2,2,0,GL_RGB_,GL_UNSIGNED_BYTE_,tex,sizeof(tex)));
 assert(store.get(ids[0])->complete());
 assert(gl.parameter_i(GL_TEXTURE_2D_,GL_TEXTURE_MIN_FILTER_,GL_NEAREST_));
 assert(gl.parameter_i(GL_TEXTURE_2D_,GL_TEXTURE_MAG_FILTER_,GL_NEAREST_));
 assert(gl.parameter_i(GL_TEXTURE_2D_,GL_TEXTURE_WRAP_S_,GL_REPEAT_));
 const std::uint8_t patch[]={0,255,255};
 assert(gl.sub_image_2d(GL_TEXTURE_2D_,0,1,0,1,1,GL_RGB_,GL_UNSIGNED_BYTE_,patch,sizeof(patch)));
 auto c=store.sample(ids[0],0.75f,0.25f);
 assert((c&255)==0&&((c>>8)&255)==255&&((c>>16)&255)==255);
 assert(!gl.pixel_store(GL_UNPACK_ALIGNMENT_,3));
 assert(!gl.parameter_i(GL_TEXTURE_2D_,GL_TEXTURE_MIN_FILTER_,0x2703));
 assert(!gl.image_2d(GL_TEXTURE_2D_,1,4,2,2,0,GL_RGB_,GL_UNSIGNED_BYTE_,tex,sizeof(tex)));
 assert(gl.bind(GL_TEXTURE_2D_,ids[1]));assert(gl.bound_2d()==ids[1]);
 gl.erase(ids.data(),ids.size());assert(gl.bound_2d()==0&&!gl.is_texture(ids[0]));
 assert(!gl.bind(0x0DE0,ids[0]));
 std::cout<<"gl_texture_api_test: PASS\n";
}
