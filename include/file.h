//
// Created by radue on 2/21/2026.
//

#pragma once

#include <fstream>
#include <vector>
#include <filesystem>
#include <glm/fwd.hpp>

/** @brief Small file helpers, mostly for loading shader sources and binaries. */
namespace kor::utils
{
    /**
     * @brief Reads a whole text file into a string.
     * @param filePath File to read.
     * @return Its contents. Line endings are normalised to '\\n', and the result always ends with one.
     * @throws std::runtime_error if the file cannot be opened.
     */
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

    /**
     * @brief Writes a string to a file, replacing anything already there.
     * @param filePath File to write. Its parent directory must exist.
     * @param data Contents to write.
     * @throws std::runtime_error if the file cannot be opened for writing.
     */
    inline void WriteToFile(const std::filesystem::path& filePath, const std::string& data)
    {
        std::ofstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file for writing: " + filePath.string());
        }

        file.write(data.data(), data.size());
        file.close();
    }

    /**
     * @brief Reads a binary file as an array of 32-bit words — the form compiled SPIR-V takes.
     * @param filePath File to read.
     * @return Its contents, one element per four bytes.
     * @throws std::runtime_error if the file cannot be opened, or if its size is not a multiple of
     *         four, which for a shader binary means it is truncated or not SPIR-V at all.
     */
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

    /**
     * @brief Writes an array of 32-bit words to a binary file, replacing anything already there.
     * @param filePath File to write. Its parent directory must exist.
     * @param data Words to write, in order.
     * @throws std::runtime_error if the file cannot be opened for writing.
     */
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
