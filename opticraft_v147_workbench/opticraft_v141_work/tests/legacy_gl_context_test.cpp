#include "legacy_gl_context.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
using namespace opti::gl;
int main() {
    LegacyGLContext gl(8,8);
    gl.core().viewport(0,0,8,8);
    gl.core().clear_color(0,0,0,1);
    gl.clear();

    // Original-style immediate mode path goes through the shared software renderer.
    gl.begin(TRIANGLES);
    gl.color(1,0,0,1);gl.texcoord(0,0);gl.vertex(-1,-1,0);
    gl.color(1,0,0,1);gl.texcoord(1,0);gl.vertex( 1,-1,0);
    gl.color(1,0,0,1);gl.texcoord(0,1);gl.vertex(-1, 1,0);
    assert(gl.end());
    const auto red=gl.renderer().color_buffer()[1*8+1];
    assert((red&255u)>240u && ((red>>16)&255u)<10u);

    // Texture API and immediate-mode UVs share the same context and object store.
    auto ids=gl.texture_api().gen(1);
    assert(ids.size()==1 && gl.texture_api().bind(GL_TEXTURE_2D_,ids[0]));
    const std::uint8_t texel[]={0,255,0,255};
    assert(gl.texture_api().image_2d(GL_TEXTURE_2D_,0,4,1,1,0,GL_RGBA_,GL_UNSIGNED_BYTE_,texel,sizeof(texel)));
    gl.core().enable(0x0DE1,true);
    gl.clear();
    gl.begin(TRIANGLES);
    gl.color(1,1,1,1);gl.texcoord(0,0);gl.vertex(-1,-1,0);
    gl.color(1,1,1,1);gl.texcoord(1,0);gl.vertex( 1,-1,0);
    gl.color(1,1,1,1);gl.texcoord(0,1);gl.vertex(-1, 1,0);
    assert(gl.end());
    const auto green=gl.renderer().color_buffer()[1*8+1];
    assert(((green>>8)&255u)>240u && (green&255u)<10u);

    // Client-array path lands in the exact same framebuffer and state.
    const float verts[]={-1,-1,0, 1,-1,0, -1,1,0};
    assert(gl.arrays().vertex_pointer(3,ArrayScalar::Float,0,verts));
    assert(gl.arrays().enable(GL_VERTEX_ARRAY_,true));
    gl.core().enable(0x0DE1,false);
    gl.clear();
    assert(gl.draw_arrays(GL_TRIANGLES_,0,3));
    const auto drawn=gl.renderer().color_buffer()[1*8+1];
    assert(drawn==0xFFFFFFFFu);
    const std::uint16_t idx[]={0,1,2};
    gl.clear();
    assert(gl.draw_elements(GL_TRIANGLES_,3,ArrayScalar::UShort,idx));
    assert(gl.renderer().color_buffer()[1*8+1]==0xFFFFFFFFu);
    assert(!gl.draw_arrays(GL_TRIANGLES_,-1,3));
    std::cout<<"legacy_gl_context_test: PASS\\n";
}
