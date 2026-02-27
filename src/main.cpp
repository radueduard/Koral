#include <iostream>

#include <GL/glew.h>
#include "context.h"
#include "io/manager.h"

#include "io/window.h"

using gfx::io::Window;

namespace gfx
{
    class Engine
    {
        friend int ::main();
        static void Run();
    };
}

int main() {
    try {
        gfx::io::Manager manager;
        gfx::Engine::Run();
    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
