

#include <iostream>
#include <string>

#include "context.h"
#include "projectConfig.h"

namespace kor
{
    class Engine
    {
    public:
        static int Run(int argc, char** argv);
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

namespace
{
    void printUsage(const char* program)
    {
        std::cout
            << "Usage: " << program << " <scene_library> [options]\n\n"
            << "Runs a Koral scene or job library. Settings come from the library's own\n"
            << "CreateProjectConfig(), then from its koral.json, then from the options below —\n"
            << "each layer overriding the one before it.\n\n"
            << "Options:\n"
            << kor::ProjectConfig::usage()
            << "  --help              Show this message\n";
    }
}

int main(const int argc, char **argv)
{
#ifdef _WIN32
    enableANSI();
#endif

    for (int i = 1; i < argc; ++i) {
        if (const std::string arg = argv[i]; arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return EXIT_SUCCESS;
        }
    }

    // Guarding here rather than warning and carrying on: Engine::Run's first act is to read
    // argv[1] as the scene library, and there is nothing to run without one.
    if (argc < 2) {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    return kor::Engine::Run(argc, argv);
}
