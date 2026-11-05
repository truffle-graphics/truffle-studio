#pragma once

#include "model/sketch.hpp"

#include <string>

namespace studio::model {

// Extrudes the closed profiles of a sketch along its plane normal. A
// negative distance extrudes the other way. Regeneration turns features
// into solids; the document stores only their parameters.
struct Extrude {
    ObjectId id = 0;
    std::string name;
    ObjectId sketch = 0;
    double distance = 10;

    bool operator==(const Extrude&) const = default;
};

} // namespace studio::model
