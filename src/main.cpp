

#include <iostream>

#include "context.h"

namespace kor
{
    class Engine
    {
    public:
        static void Run(int argc, char** argv);
    };
}

#ifdef _WIN32
#include <Windows.h>
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
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <scene_library_name>" << std::endl;
    }
#ifdef _WIN32
    enableANSI();
#endif
    kor::Engine::Run(argc, argv);
    return EXIT_SUCCESS;
}
