#pragma once

namespace studio {

struct AppOptions {
    // Checks the blank native client and its lifecycle, then exits.
    bool smoke = false;
};

int run(const AppOptions& options);

} // namespace studio
