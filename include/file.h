//
// Created by radue on 2/21/2026.
//

#pragma once

#include <fstream>
#include <vector>
#include <filesystem>

namespace gfx::utils
{
    inline std::string ReadFileAsString(const std::filesystem::path& filePath)
    {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filePath.string());
        }

        std::string buffer;
        while (!file.eof())
        {
            std::string line;
            std::getline(file, line);
            buffer += line + "\n";
        }

        file.close();
        return buffer;
    }

    inline void WriteToFile(const std::filesystem::path& filePath, const std::string& data)
    {
        std::ofstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file for writing: " + filePath.string());
        }

        file.write(data.data(), data.size());
        file.close();
    }

    inline std::vector<glm::u32> ReadFileToUIntVector(const std::filesystem::path& filePath)
    {
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open file: " + filePath.string());
        }

        const auto fileSize = static_cast<size_t>(file.tellg());
        if (fileSize % sizeof(glm::u32) != 0)
        {
            throw std::runtime_error("File size is not a multiple of uint32_t size: " + filePath.string());
        }

        std::vector<glm::u32> buffer(fileSize / sizeof(glm::u32));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
        file.close();

        return buffer;
    }

    inline void WriteUIntVectorToFile(const std::filesystem::path& filePath, const std::vector<glm::u32>& data)
    {
        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open())        {
            throw std::runtime_error("Failed to open file for writing: " + filePath.string());
        }

        file.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(glm::u32));
        file.close();
    }
}
