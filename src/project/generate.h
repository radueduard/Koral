//
// Created by radue on 3/16/2026.
//

#pragma once
#include <set>
#include <filesystem>
#include <fstream>

#include "context.h"

static void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    if(from.empty()) return;
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // Advance past the replacement
    }
}

namespace gfx
{
    class ProjectManager
    {
    public:
        static void generate(const std::filesystem::path& path, const std::string& name)
        {
            if (!std::filesystem::exists(path)) {
                throw std::runtime_error("Project path does not exist: " + path.string());
            }

            const auto projectDir = path / name;
            if (std::filesystem::exists(projectDir)) {
                throw std::runtime_error("There already is a folder named: " + name + " in the specified path");
            }

            std::filesystem::create_directory(projectDir);
            // create src, assets, shaders folders
            std::filesystem::create_directory(projectDir / "src");
            std::filesystem::create_directory(projectDir / "assets");
            std::filesystem::create_directory(projectDir / "shaders");

            // in the src folder, create ${name}.cpp, ${name}.h, export.cpp
            std::ifstream cppTemplate(gfx::templatePath("template.cpp"));
            if (!cppTemplate.is_open()) {
                throw std::runtime_error("Failed to open cpp template file");
            }
            std::string cppContent((std::istreambuf_iterator<char>(cppTemplate)), std::istreambuf_iterator<char>());
            replaceAll(cppContent, "@NAME@", name);
            std::ofstream cppFile(projectDir / "src" / (name + ".cpp"));
            cppFile << cppContent;
            cppFile.close();

            std::ifstream hTemplate(gfx::templatePath("template.h"));
            if (!hTemplate.is_open())
            {
                throw std::runtime_error("Failed to open h template file");
            }
            std::string hContent((std::istreambuf_iterator<char>(hTemplate)), std::istreambuf_iterator<char>());
            replaceAll(hContent, "@NAME@", name);
            std::ofstream hFile(projectDir / "src" / (name + ".h"));
            hFile << hContent;
            hFile.close();

            std::ifstream exportTemplate(gfx::templatePath("export.cpp"));
            if (!exportTemplate.is_open())
            {
                throw std::runtime_error("Failed to open export template file");
            }
            std::string exportContent((std::istreambuf_iterator<char>(exportTemplate)), std::istreambuf_iterator<char>());
            replaceAll(exportContent, "@NAME@", name);
            std::ofstream exportFile(projectDir / "src" / "export.cpp");
            exportFile << exportContent;
            exportFile.close();

            std::ifstream vcpkgTemplate(gfx::templatePath("vcpkg.json"));
            if (!vcpkgTemplate.is_open())
            {
                throw std::runtime_error("Failed to open vcpkg template file");
            }
            std::string vcpkgContent((std::istreambuf_iterator<char>(vcpkgTemplate)), std::istreambuf_iterator<char>());
            auto lowerCaseName = name;
            std::ranges::transform(lowerCaseName, lowerCaseName.begin(), ::tolower);
            replaceAll(vcpkgContent, "@NAME@", lowerCaseName);
            std::ofstream vcpkgFile(projectDir / "vcpkg.json");
            vcpkgFile << vcpkgContent;
            vcpkgFile.close();

            std::ifstream cmakeTemplate(gfx::templatePath("CMakeLists.txt"));
            if (!cmakeTemplate.is_open())
            {
                throw std::runtime_error("Failed to open cmake template file");
            }
            std::string cmakeContent((std::istreambuf_iterator<char>(cmakeTemplate)), std::istreambuf_iterator<char>());
            replaceAll(cmakeContent, "@NAME@", name);
            replaceAll(cmakeContent, "@PATH@", projectDir.generic_string());
            replaceAll(cmakeContent, "@GFX_PATH@", PROJECT_ROOT);
            std::ofstream cmakeFile(projectDir / "CMakeLists.txt");
            cmakeFile << cmakeContent;
            cmakeFile.close();

            std::ifstream cmakePresets(gfx::templatePath("CMakePresets.json"));
            if (!cmakePresets.is_open())
            {
                throw std::runtime_error("Failed to open cmake presets file");
            }
            std::string cmakePresetsContent((std::istreambuf_iterator<char>(cmakePresets)), std::istreambuf_iterator<char>());
            replaceAll(cmakePresetsContent, "@VCPKG@", VCPKG_ROOT);
            std::ofstream cmakePresetsFile(projectDir / "CMakePresets.json");
            cmakePresetsFile << cmakePresetsContent;
            cmakePresetsFile.close();

            _projects.insert(projectDir);

            // open clion at the project directory
#ifdef _WIN32
            std::string command = "clion " + projectDir.string();
#elif defined(__linux__) || defined(__APPLE__)
            std::string command = "clion " + projectDir.string() + " &";
#else
#error "Unsupported platform"
#endif
            system(command.c_str());
        }

    private:
        inline static std::set<std::filesystem::path> _projects {};
    };
}
