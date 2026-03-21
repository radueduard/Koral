

#include <iostream>

#include "context.h"
#include "project/generate.h"

namespace gfx
{
    class Engine
    {
    public:
        static void Run(const std::filesystem::path& scenePath);
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

int main(const int argc, char **argv)
{
    // gfx::ProjectManager::generate("F:/GFX_PROJECTS", "Hub");
    // return EXIT_SUCCESS;

    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <scene_library_name>" << std::endl;
    }
#ifdef _WIN32
    enableANSI();
#endif
    try {
        gfx::Engine::Run(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
