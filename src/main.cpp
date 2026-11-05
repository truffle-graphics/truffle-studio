#include "truffle/gui/dock/dock.hpp"
#include "truffle/rhi/metal_backend.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <iostream>

int main(int /*argc*/, char** /*argv*/) {
    SDL_Log("Truffle Studio %s", SDL_GetRevision());
    std::cout << "Truffle Studio scaffold: SDL3 and Truffle linked\n";
    return 0;
}
