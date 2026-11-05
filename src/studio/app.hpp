#pragma once

namespace studio {

struct AppOptions {
    // Runs a scripted, self-checking session and exits; for automated tests.
    bool smoke = false;
};

int run(const AppOptions& options);

} // namespace studio
