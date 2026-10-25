#include <iostream>
#include <string_view>
#include <filesystem>
#include <cstdlib>
#include <esp_spiffs.h>
#include <esp_littlefs.h>
#include <string.h>
#include <esp_err.h>
#include <esp_log.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>

#include "../DataTypes.hpp"

#include "Storage.hpp"

#define STORAGE_LABEL "storage" // SPIFFS partition label
#define MOUNT_POINT "/storage" // Internal SPIFFS mount path

// Singleton instance of CStorage class, available everywhere
CStorage Storage;

Result_t CStorage::Mount(FileSystemType_t type, const std::string_view partitionLabel, const std::string_view mountPoint, bool mayFormat) {
    ESP_LOGI(TAG, "Mounting storage \"%.*s\"...", partitionLabel.size(), partitionLabel.data());

    try {
        // Create strings from parameters
        std::string partitionLabelStr(partitionLabel);
        std::string mountPointStr(mountPoint);

        // Mount the file system
        esp_err_t err;
        size_t total = 0;
        size_t used = 0;
        esp_vfs_spiffs_conf_t confSpifs = {};
        esp_vfs_littlefs_conf_t confLittlefs = {};
        switch (type) {
            case FileSystemType_t::SPIFFS:
                // Mount SPIFFS
                confSpifs.base_path = mountPointStr.c_str(),
                confSpifs.partition_label = partitionLabelStr.c_str(),
                confSpifs.max_files = 5,
                confSpifs.format_if_mount_failed = mayFormat;

                err = esp_vfs_spiffs_register(&confSpifs);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to initialize SPIFFS file system \"%.*s\": %s", partitionLabel.size(), partitionLabel.data(), esp_err_to_name(err));
                    return EspErrToResult(err);
                }

                // Verify file system
                err = esp_spiffs_info(partitionLabelStr.c_str(), &total, &used);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to verify SPIFFS file system \"%.*s\": %s", partitionLabel.size(), partitionLabel.data(), esp_err_to_name(err));
                    return EspErrToResult(err);
                }
                break;

            case FileSystemType_t::LittleFS:
                // Mount LittleFS
                confLittlefs.base_path = mountPointStr.c_str();
                confLittlefs.partition_label = partitionLabelStr.c_str();
                confLittlefs.format_if_mount_failed = mayFormat;

                err = esp_vfs_littlefs_register(&confLittlefs);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to initialize LittleFS file system \"%.*s\": %s", partitionLabel.size(), partitionLabel.data(), esp_err_to_name(err));
                    return EspErrToResult(err);
                }

                // Verify file system
                err = esp_littlefs_info(partitionLabelStr.c_str(), &total, &used);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to verify LittleFS file system \"%.*s\": %s", partitionLabel.size(), partitionLabel.data(), esp_err_to_name(err));
                    return EspErrToResult(err);
                }
                break;

        }

        ESP_LOGI(TAG, "Storage mounted successfully");

        // Success
        return Result_t::OK;
    } catch (const std::bad_alloc&) {
        ESP_LOGE(TAG, "Out of memory");
        return Result_t::OutOfMemory;
    }
}

Result_t CStorage::EnsureDirectoryExists(const std::string_view mountPoint, const std::string_view directoryPath) {
    try {
        ESP_LOGD(TAG, "Ensuring directory exists: %.*s/%.*s",
            static_cast<int>(mountPoint.size()), mountPoint.data(),
            static_cast<int>(directoryPath.size()), directoryPath.data());

        // Get the full path to the directory
        Result<std::string> fullPath = GetFilePath(mountPoint, directoryPath);
        if (!fullPath) {
            ESP_LOGE(TAG, "Failed to get full path for directory: %.*s",
                static_cast<int>(directoryPath.size()), directoryPath.data());
            return fullPath.error();
        }

        // Create the directory if it doesn't exist
        std::error_code err;
        std::filesystem::create_directories(fullPath.value(), err);
        if (err) {
            ESP_LOGE(TAG, "Failed to create directory: %.*s",
                static_cast<int>(fullPath.value().size()), fullPath.value().c_str());
            return Result_t::Failed;
        }

        // Success
        return Result_t::OK;
    } catch (const std::bad_alloc&) {
        // Out of memory
        ESP_LOGE(TAG, "Out of memory");
        return Result_t::OutOfMemory;
    }
}

Result<std::string> CStorage::GetFilePath(const std::string_view mountPoint,
    const std::string_view filePath)
{
    // Sanity checks
    // mountPoint is trusted and doesn't need to be checked
    if (filePath.empty() || // Params must have vqalid values
        filePath.find('\0') != std::string_view::npos) // No nulls in filePath
            return Result<std::string>::Err(Result_t::InvalidArg);

    try {
        std::string result(mountPoint);

        // Normalize the untrusted path into relative components.
        std::vector<std::string_view> components;
        size_t start = 0;
        while (start < filePath.size()) {
            // Find the start of the next path component
            while (start < filePath.size() &&
                (filePath[start] == '/' || filePath[start] == '\\'))
                    start++;

            // Find the end of the current path component
            size_t end = start;
            while (end < filePath.size() &&
                filePath[end] != '/' &&
                filePath[end] != '\\')
                    end++;

            // Extract the path component
            const std::string_view part = filePath.substr(start, end - start);

            // Validate the path component
            if (part.empty() || part == ".") {
                // NO-OP: Ignore empty and current-directory components.
            } else if (part == "..") {
                // Never pop beyond the mount point.
                if (!components.empty())
                    components.pop_back();
            } else
                components.push_back(part);

            start = end;            
        }

        // Rebuild the canonical path.
        for (const auto part : components) {
            // Insert path separator
            result.push_back('/');

            // Copy the path component to output
            for (const char c : part)
                // Avoid embedded NUL bytes being interpreted as string
                // terminators by C filesystem APIs.
                result.push_back(c == '\0' ? '_' : c);
        }

        // Return the result
        return Result<std::string>::Ok(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result<std::string>::Err(Result_t::OutOfMemory);
    }
}

Result<bool> CStorage::FileExists(const std::string_view mountPoint,
    const std::string_view filePath)
{
    // Get file name
    Result<std::string> result = GetFilePath(mountPoint, filePath);
    if (!result)
        return Result<bool>::Err(result.error()); // Failed

    // Determine if file exists
    struct stat fileStat;
    if (stat(result.value().c_str(), &fileStat) == -1)
        return Result<bool>::Ok(false);

    return Result<bool>::Ok(true);
}

Result<off_t> CStorage::GetFileSize(const std::string_view mountPoint,
    const std::string_view filePath)
{
    // Get file name
    Result<std::string> result = GetFilePath(mountPoint, filePath);
    if (!result)
        return Result<off_t>::Err(result.error()); // Failed

    // Determine if file exists and get file stats
    struct stat fileStat;
    if (stat(result.value().c_str(), &fileStat) == -1)
        return Result<off_t>::Err(Result_t::FileNotFound);

    return Result<off_t>::Ok(fileStat.st_size);
}
