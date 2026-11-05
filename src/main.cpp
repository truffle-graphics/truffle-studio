#include "studio/app.hpp"

#include <SDL3/SDL_main.h>

#include <exception>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    try {
        return studio::run({.smoke = argc == 2 && std::string_view(argv[1]) == "--smoke"});
    } catch (const std::exception& error) {
        std::cerr << "Truffle Studio: " << error.what() << '\n';
        return 1;
    }
}
