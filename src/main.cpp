

#include <iostream>
#include "io/manager.h"

using gfx::io::Window;

namespace gfx
{
    class Engine
    {
        friend int ::main(int argc, char **argv);
        static void Run(std::filesystem::path scenePath);
    };
}

#ifdef _WIN32
#include <windows.h>
void enableANSI() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    DWORD dwMode = 0;

    if (hOut != INVALID_HANDLE_VALUE) {
        GetConsoleMode(hOut, &dwMode);
        SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    if (hErr != INVALID_HANDLE_VALUE) {
        GetConsoleMode(hErr, &dwMode);
        SetConsoleMode(hErr, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}
#endif

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <scene_library_name>" << std::endl;
    }
#ifdef _WIN32
    enableANSI();
#endif
    try {
        gfx::io::Manager manager;
        gfx::Engine::Run(gfx::scenePath(argv[1]));
    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
