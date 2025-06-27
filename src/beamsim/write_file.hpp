#pragma once

#include <fstream>
#include <string_view>

namespace beamsim {
  inline void writeFile(std::string path, std::string_view data) {
    std::ofstream file{path, std::ios::binary};
    file.write(data.data(), data.size());
    if (not file.good()) {
      abort();
    }
  }
}  // namespace beamsim
