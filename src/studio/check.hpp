#pragma once

#include "truffle/base/status.hpp"

#include <stdexcept>
#include <utility>

namespace studio {

// Studio treats a failed Truffle call during setup or a frame as fatal.
inline void need(const truffle::base::Status& status) {
    if (!status.ok()) { throw std::runtime_error(status.message); }
}

template <typename T> T need(truffle::base::Result<T> result) {
    if (!result.ok()) { throw std::runtime_error(result.status().message); }
    return std::move(result).value();
}

} // namespace studio
