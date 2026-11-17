#include "framebuffer_blit.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
int main() {
    constexpr std::size_t stride=24;
    std::uint8_t dst[stride*3];
    std::memset(dst,0xCD,sizeof(dst));
    const std::uint32_t src[]={0xFF0000FFu,0xFF00FF00u,0xFFFF0000u,0xFFFFFFFFu};
    assert(opti::blit_rgba8(dst,stride,4,3,src,2,2));
    const auto* row0=reinterpret_cast<const std::uint32_t*>(dst);
    const auto* row1=reinterpret_cast<const std::uint32_t*>(dst+stride);
    const auto* row2=reinterpret_cast<const std::uint32_t*>(dst+2*stride);
    assert(row0[0]==src[0] && row0[1]==src[1]);
    assert(row1[0]==src[2] && row1[1]==src[3]);
    assert(row0[2]==0xFF000000u && row1[3]==0xFF000000u && row2[0]==0xFF000000u);
    // Padding bytes are not touched.
    assert(dst[16]==0xCD && dst[17]==0xCD && dst[23]==0xCD);
    assert(!opti::blit_rgba8(dst,stride,7,3,src,2,2)); // stride too small
    assert(!opti::blit_rgba8(dst,stride,4,3,nullptr,2,2));
    std::cout<<"framebuffer_blit_test: PASS\\n";
}
