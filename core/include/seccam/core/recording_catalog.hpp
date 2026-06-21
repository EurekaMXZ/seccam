#pragma once

#include <cstdint>
#include <vector>

#include "seccam/core/types.hpp"

namespace seccam::core {

class RecordingCatalog {
 public:
  std::vector<RecordingFile> list(const RuntimeConfig &config, std::uint32_t limit,
                                  std::uint64_t newer_than_ms) const;
};

}  // namespace seccam::core
