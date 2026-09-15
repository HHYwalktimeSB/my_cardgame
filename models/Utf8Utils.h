#pragma once

#include <cstddef>
#include <string_view>

namespace card_game
{
inline std::size_t utf8Length(std::string_view value) noexcept
{
    std::size_t length = 0;
    for(const unsigned char byte : value)
    {
        if((byte & 0xc0) != 0x80)++length;
    }
    return length;
}
}  // namespace card_game
