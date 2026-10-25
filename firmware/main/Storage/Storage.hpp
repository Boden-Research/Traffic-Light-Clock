#pragma once

#include <string>

#include <stdio.h>
#include <esp_err.h>

#include "../DataTypes.hpp"

/// @brief Enumerator for defining a file system type.
enum class FileSystemType_t {
    SPIFFS,
    LittleFS
};

/// @brief Class for allowing storage of configurations and data.
class CStorage {
    public:
        // Log tag
        static constexpr const char* TAG = "CStorage";

        /// @brief Initializes the SPIFFS storage partition and mounts the file system.
        /// @param type (in) The file system type to mount.
        /// @param partitionLabel (in) The name of the partition in partitions.csv to mount.
        /// @param mountPoint (in) The file system mount point to load the partition on.
        /// @param mayFormat (in) When true, the SPIFFS partition may be formatted if it is corrupt or uninitialized.
        /// @return A Result_t indicating success or any failures.
        Result_t Mount(FileSystemType_t type, const std::string_view partitionLabel, const std::string_view mountPoint, bool mayFormat);

        /// @brief Ensures that a directory exists, creating it if necessary.
        /// @param mountPoint (in) The mount point where the directory will be located, same as was passed to Initialize().
        /// @param directoryPath (in) The path to the directory.
        /// @return A Result_t indicating success or any failures.
        Result_t EnsureDirectoryExists(const std::string_view mountPoint, const std::string_view directoryPath);

        /// @brief Gets the full path to the specified file, normalizing file paths and ensuring no traversal outside of the mount point.
        /// @param mountPoint (in) The mount point where the file will be located, same as was passed to Initialize().
        /// @param filePath (in) The path to the file.
        /// @return If successful, a Result containing a string. Otherwise, a Result containing an error code.
        Result<std::string> GetFilePath(const std::string_view mountPoint, const std::string_view filePath);

        /// @brief Tests if a file exists in storage.
        /// @param mountPoint (in) The mount point where the file will be located, same as was passed to Initialize().
        /// @param filePath (in) The path to the file.
        /// @return True if file exists, false if not or on error.
        /// @return If successful, a Result containing a bool indicating if the file exists or not. Otherwise, a Result containing an error code.
        Result<bool> FileExists(const std::string_view mountPoint, const std::string_view filePath);

        /// @brief Gets the size of a file.
        /// @param mountPoint (in) The mount point where the file will be located, same as was passed to Initialize().
        /// @param filePath (in) The path to the file.
        /// @return If successful, a Result containing an off_t file size. Otherwise, a Result containing an error code.
        Result<off_t> GetFileSize(const std::string_view mountPoint, const std::string_view filePath);

};

// Make the static Storage instance globally available
extern CStorage Storage;
