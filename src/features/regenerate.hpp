#pragma once

#include "kernel/kernel.hpp"
#include "model/document.hpp"

#include <string>
#include <vector>

// Turns the document's features into solids. Depends on the model and the
// kernel interface only; no Truffle.
namespace studio::features {

// The kernel plane of a sketch plane: XY, XZ (sketch y is Z) or YZ (sketch
// x is Y, y is Z), all through the origin.
kernel::Plane sketch_plane(model::SketchPlane plane) noexcept;

// The sketch's closed loops as profiles: a loop inside another becomes a
// hole of the innermost loop around it, and a loop inside a hole starts a
// new profile. Open chains are ignored.
std::vector<kernel::Profile> sketch_profiles(const model::Sketch& sketch);

struct FeatureResult {
    model::ObjectId feature = 0;
    std::string error; // empty when the feature built
    kernel::Solid solid;
};

// The rebuilt part: one result per extrude in document order, and the
// triangles of every solid that built.
struct Regeneration {
    std::vector<FeatureResult> features;
    kernel::Mesh mesh;

    const FeatureResult* result(model::ObjectId feature) const noexcept;
    double volume() const;
};

// Rebuilds every feature. Never throws for bad input: failures are
// recorded on the feature.
Regeneration regenerate(const model::DocumentState& state, double tolerance = 0.05);

} // namespace studio::features
