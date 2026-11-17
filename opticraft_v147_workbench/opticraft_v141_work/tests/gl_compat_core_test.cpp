#include "gl_compat_core.hpp"
#include "software_renderer.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace opti::gl;
int main() {
    CompatCore gl;
    gl.matrix_mode(0x1700);
    gl.load_identity();
    gl.translate(3,4,5);
    auto m=gl.modelview();
    assert(std::fabs(m[12]-3)<1e-5 && std::fabs(m[13]-4)<1e-5 && std::fabs(m[14]-5)<1e-5);
    gl.push_matrix(); gl.scale(2,2,2); gl.pop_matrix();
    assert(std::fabs(gl.modelview()[0]-1)<1e-5);
    gl.begin(QUADS);
    gl.color(1,0,0,1); gl.texcoord(0,0); gl.vertex(0,0,0);
    gl.texcoord(1,0); gl.vertex(1,0,0);
    gl.texcoord(1,1); gl.vertex(1,1,0);
    gl.texcoord(0,1); gl.vertex(0,1,0);
    auto batch=gl.end();
    assert(batch.vertices.size()==4 && batch.indices.size()==6);
    assert(std::fabs(batch.vertices[0].clip_position.w-1.0f)<1e-5);
    gl.matrix_mode(0x1702);
    gl.load_identity();
    gl.translate(0.25f,0.5f,0.0f);
    gl.begin(TRIANGLES);
    gl.texcoord(0.25f,0.25f); gl.vertex(0,0,0);
    gl.texcoord(0.5f,0.25f); gl.vertex(1,0,0);
    gl.texcoord(0.25f,0.5f); gl.vertex(0,1,0);
    auto textured=gl.end();
    assert(std::fabs(textured.vertices[0].texcoord.x-0.5f)<1e-5);
    assert(std::fabs(textured.vertices[0].texcoord.y-0.75f)<1e-5);
    gl.begin(TRIANGLE_STRIP);
    gl.vertex(0,0,0);gl.vertex(1,0,0);gl.vertex(0,1,0);gl.vertex(1,1,0);
    auto strip=gl.end();assert(strip.indices.size()==6);
    gl.enable(0x0B71,true);assert(gl.state().depth_test);
    gl.color_mask(true,false,true,false);
    assert(gl.state().color_write_mask[0] && !gl.state().color_write_mask[1] &&
           gl.state().color_write_mask[2] && !gl.state().color_write_mask[3]);
    gl.viewport(0,0,1280,720);assert(gl.state().viewport.width==1280);
    gl.clear_color(.1f,.2f,.3f,1);assert(std::fabs(gl.state().clear_color[2]-.3f)<1e-5);
    bool underflow=false;try{gl.pop_matrix();}catch(...){underflow=true;}assert(underflow);
    std::cout<<"PASS: GL compatibility state, matrices, primitive conversion\n";
}
