#pragma once
#include <cstddef>
#include <cstdint>
namespace opti {
// Copies tightly-packed little-endian RGBA8 pixels into a possibly padded
// linear RGBA8 destination. The destination is cleared to opaque black first.
bool blit_rgba8(std::uint8_t* destination,std::size_t destination_stride,
                int destination_width,int destination_height,
                const std::uint32_t* source,int source_width,int source_height);
}
