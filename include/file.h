//
// Created by radue on 2/21/2026.
//

#pragma once

#include <fstream>
#include <vector>
#include <filesystem>
#include <glm/fwd.hpp>

#include "error.h"

/**
 * @brief Small file helpers, mostly for loading shader sources and binaries.
 *
 * Named verb-what-where throughout — ReadFileAsString, WriteUIntsToFile — and UpperCamel because
 * they move data between places, the same category as Buffer::Read and Buffer::Write. @see koral.h
 * for the naming rule.
 */
namespace kor::utils
{
    /**
     * @brief Reads a whole text file into a string.
     * @param filePath File to read.
     * @return Its contents, or an error if the file cannot be opened. Line endings are normalised
     *         to '\\n', and the result always ends with one.
     */
    [[nodiscard]] inline kor::Result<std::string> ReadFileAsString(const std::filesystem::path& filePath)
    {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            return kor::fail(kor::ErrorCode::eFileNotReadable, "Failed to open file: {}", filePath.string());
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
     * @return An empty result, or an error if the file cannot be opened for writing.
     */
    [[nodiscard]] inline kor::VoidResult WriteStringToFile(const std::filesystem::path& filePath, const std::string& data)
    {
        std::ofstream file(filePath);
        if (!file.is_open()) {
            return kor::fail(kor::ErrorCode::eFileNotReadable, "Failed to open file for writing: {}", filePath.string());
        }

        file.write(data.data(), data.size());
        file.close();
        return {};
    }

    /**
     * @brief Reads a binary file as an array of 32-bit words — the form compiled SPIR-V takes.
     * @param filePath File to read.
     * @return Its contents, one element per four bytes; an error if the file cannot be opened, or
     *         if its size is not a multiple of four — which for a shader binary means it is
     *         truncated or not SPIR-V at all.
     */
    [[nodiscard]] inline kor::Result<std::vector<glm::u32>> ReadFileAsUInts(const std::filesystem::path& filePath)
    {
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            return kor::fail(kor::ErrorCode::eFileNotReadable, "Failed to open file: {}", filePath.string());
        }

        const auto fileSize = static_cast<std::size_t>(file.tellg());
        if (fileSize % sizeof(glm::u32) != 0)
        {
            return kor::fail(kor::ErrorCode::eFileNotReadable,
                             "File size {} is not a multiple of 4 bytes: {}", fileSize, filePath.string());
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
     * @return An empty result, or an error if the file cannot be opened for writing.
     */
    [[nodiscard]] inline kor::VoidResult WriteUIntsToFile(const std::filesystem::path& filePath, const std::vector<glm::u32>& data)
    {
        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            return kor::fail(kor::ErrorCode::eFileNotReadable, "Failed to open file for writing: {}", filePath.string());
        }

        file.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(glm::u32));
        file.close();
        return {};
    }
}
