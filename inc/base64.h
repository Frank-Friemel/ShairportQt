#pragma once

#include <string>
#include <vector>
#include <stdint.h>

namespace Base64
{

std::string Encode(const std::vector<uint8_t>& input);
std::vector<uint8_t> Decode(const std::string& input);

}
