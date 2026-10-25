#include <cmath>
#include <fstream>
#include <memory>
#include <new>
#include <utility>
#include <filesystem>
#include <cstddef>
#include <system_error>
#include <cstring>
#include <string>
#include <string.h>
#include <sys/stat.h>
#include <esp_err.h>
#include <esp_log.h>

#include "../DataTypes.hpp"
#include "Storage.hpp"

#include "AnimationStore.hpp"

#define STORAGE_LABEL "storage" // Partition label
#define MOUNT_POINT "/storage" // Internal mount path

// Singleton instance of CAnimationStore class, available everywhere
CAnimationStore AnimationStore;

Result_t CAnimationStore::Initialize() {
    ESP_LOGI(TAG, "Initializing animation store");

    // Make sure the file system is mounted
    Result_t result = Storage.Mount(FileSystemType_t::LittleFS, STORAGE_LABEL, MOUNT_POINT, true);
    if (result != Result_t::OK) {
        ESP_LOGE(TAG, "Failed to mount storage: %s", GetResultDescription(result));
        return result;
    }

    // Ensure the animations directory exists
    result = Storage.EnsureDirectoryExists(MOUNT_POINT, "animations");
    if (result != Result_t::OK) {
        ESP_LOGE(TAG, "Failed to ensure animations directory exists: %s", GetResultDescription(result));
        return result;
    }

    return Result_t::OK;
}

Result<bool> CAnimationStore::Exists(const std::string_view name) {
    // Sanity check
    if (name.empty()) {
        ESP_LOGE(TAG, "Animation name is empty");
        return Result<bool>::Err(Result_t::InvalidArg);
    }

    // Get the correct file name
    Result<std::string> result = Storage.GetFilePath(MOUNT_POINT "/animations", name);
    if (!result) {
        ESP_LOGE(TAG, "Failed to get file path for animation: %s", name.data());
        return Result<bool>::Err(result.error());
    }

    // Determine if the file exists
    struct stat st;
    bool exists = stat(result.value().c_str(), &st) == 0;
    ESP_LOGD(TAG, "Animation %s exists: %s", name.data(), exists ? "true" : "false");

    return Result<bool>::Ok(exists);
}

Result<LightAnimationPtr> CAnimationStore::Load(const std::string_view name) {
    // Sanity checks
    if (name.empty()) {
        ESP_LOGE(TAG, "Animation name is empty");
        return Result<LightAnimationPtr>::Err(Result_t::InvalidArg);
    }

    Result<std::string> fullPath = Storage.GetFilePath(MOUNT_POINT "/animations", name);
    if (!fullPath) {
        ESP_LOGE(TAG, "Failed to get file path for animation: %s", name.data());
        return Result<LightAnimationPtr>::Err(fullPath.error());
    }

    try {
        // Open the file
        std::ifstream file(fullPath.value(), std::ios::binary | std::ios::in);
        if (!file) {
            ESP_LOGE(TAG, "Failed to open animation file: %s", fullPath.value().c_str());
            return Result<LightAnimationPtr>::Err(Result_t::FileNotFound);
        }

        // Read the frame rate
        float frameRate;
        file.read(reinterpret_cast<char*>(&frameRate), sizeof(frameRate));
        if (!file) {
            ESP_LOGE(TAG, "Failed to read frame rate from animation file: %s", fullPath.value().c_str());
            return Result<LightAnimationPtr>::Err(Result_t::FileLoadEOF);
        }

        // Read the frame count
        std::size_t numFrames;
        file.read(reinterpret_cast<char*>(&numFrames), sizeof(numFrames));
        if (!file) {
            ESP_LOGE(TAG, "Failed to read frame count from animation file: %s", fullPath.value().c_str());
            return Result<LightAnimationPtr>::Err(Result_t::FileLoadEOF);
        }

        // Sanity check
        if (!std::isfinite(frameRate) || frameRate <= 0.0f || numFrames == 0) {
            ESP_LOGE(TAG, "Invalid animation file: %s", fullPath.value().c_str());
            return Result<LightAnimationPtr>::Err(Result_t::FileLoadBadFile);
        }

        // Instantiate the LightAnimationPtr
        LightAnimationPtr animation = std::make_unique<LightAnimation_t>();
        animation->FrameRate = frameRate;
        animation->Frames.resize(numFrames);

        // Load the frames
        for (LightState_t& frame : animation->Frames) {
            // Read the frame
            file.read(reinterpret_cast<char*>(frame.data()), static_cast<std::streamsize>(frame.size()));
            if (!file) {
                ESP_LOGE(TAG, "Failed to read frame from animation file: %s", fullPath.value().c_str());
                return Result<LightAnimationPtr>::Err(Result_t::FileLoadEOF);
            }
        }

        // Return successful result
        return Result<LightAnimationPtr>::Ok(std::move(animation));
    } catch (const std::bad_alloc&) {
        // Out of memory
        ESP_LOGE(TAG, "Out of memory");
        return Result<LightAnimationPtr>::Err(Result_t::OutOfMemory);
    }
}

Result_t CAnimationStore::Save(const std::string_view name, const LightAnimationPtr& animation) {
    // Sanity check
    if (name.empty()) {
        ESP_LOGE(TAG, "Animation name is empty");
        return Result_t::InvalidArg;
    }
    if (!std::isfinite(animation->FrameRate) ||
        animation->FrameRate < 0.0f ||
        animation->Frames.size() == 0) {
            ESP_LOGE(TAG, "Invalid animation frame rate or frame count");
            return Result_t::InvalidArg;
        }

    ESP_LOGD(TAG, "Saving animation: %s", name.data());

    // Get the correct file name
    Result<std::string> fullPath = Storage.GetFilePath(MOUNT_POINT "/animations", name);
    if (!fullPath) {
        ESP_LOGE(TAG, "Failed to get full path for animation: %.*s",
            static_cast<int>(name.size()), name.data());
        return fullPath.error();
    }

    try {
        // Open the file
        std::ofstream file(fullPath.value(), std::ios::binary | std::ios::out | std::ios::trunc);
        if (!file) {
            ESP_LOGE(TAG, "Failed to open animation file for writing: %.*s",
                static_cast<int>(fullPath.value().size()), fullPath.value().c_str());
            return Result_t::FileNotFound;
        }

        // Write the frame rate
        file.write(reinterpret_cast<const char*>(&animation->FrameRate), sizeof(animation->FrameRate));
        if (!file) {
            ESP_LOGE(TAG, "Failed to write frame rate to animation file: %.*s",
                static_cast<int>(fullPath.value().size()), fullPath.value().c_str());
            return Result_t::OutOfStorageSpace;
        }

        // Write the frame count
        std::size_t size = animation->Frames.size();
        file.write(reinterpret_cast<const char*>(&size), sizeof(size));
        if (!file) {
            ESP_LOGE(TAG, "Failed to write frame count to animation file: %.*s",
                static_cast<int>(fullPath.value().size()), fullPath.value().c_str());
            return Result_t::OutOfStorageSpace;
        }

        // Write the frames
        for (const LightState_t& frame : animation->Frames) {
            // Write one frame
            file.write(reinterpret_cast<const char*>(frame.data()), frame.size());
            if (!file) {
                ESP_LOGE(TAG, "Failed to write frame to animation file: %.*s",
                    static_cast<int>(fullPath.value().size()), fullPath.value().c_str());
                return Result_t::OutOfStorageSpace;
            }
        }

        // Success
        return Result_t::OK;
    } catch (const std::bad_alloc&) {
        // Out of memory
        ESP_LOGE(TAG, "Out of memory");
        return Result_t::OutOfMemory;
    }
}

Result_t CAnimationStore::Delete(const std::string_view name) {
    // Sanity check
    if (name.empty()) {
        ESP_LOGE(TAG, "Animation name is empty");
        return Result_t::InvalidArg;
    }

    ESP_LOGD(TAG, "Deleting animation: %.*s", static_cast<int>(name.size()), name.data());

    // Get the correct file name
    Result<std::string> fullPath = Storage.GetFilePath(MOUNT_POINT "/animations", name);
    if (!fullPath) {
        ESP_LOGE(TAG, "Failed to get full path for animation: %.*s",
            static_cast<int>(name.size()), name.data());
        // Pass error back to caller
        return fullPath.error();
    }

    // Delete the file
    std::error_code err;
    std::filesystem::remove(fullPath.value(), err);
    if (err) {
        ESP_LOGD(TAG, "Failed to delete animation file: %.*s",
            static_cast<int>(fullPath.value().size()), fullPath.value().c_str());
        return Result_t::FileNotFound;
    }

    ESP_LOGD(TAG, "Deleted animation file: %.*s",
        static_cast<int>(fullPath.value().size()), fullPath.value().c_str());

    // Success
    return Result_t::OK;
}

Result<AnimationStoreIterator> CAnimationStore::OpenAnimationIterator() {
    try {
        ESP_LOGD(TAG, "Opening animation iterator in directory: " MOUNT_POINT "/animations");

        // Create a new iterator instance
        AnimationStoreIterator iterator = std::make_unique<AnimationStoreIterator_t>();

        // Start an std library iteration over the animations directory
        std::error_code err;
        iterator->Current = std::filesystem::directory_iterator(MOUNT_POINT "/animations", err);

        // Test for error
        if (err) {
            ESP_LOGE(TAG, "Failed to start iterating over animations directory: %s", err.message().c_str());
            return Result<AnimationStoreIterator>::Err(Result_t::Failed);
        }

        ESP_LOGD(TAG, "Animation iterator opened successfully");

        return Result<AnimationStoreIterator>::Ok(std::move(iterator));
    } catch (const std::bad_alloc&) {
        // Out of memory
        ESP_LOGE(TAG, "Out of memory");
        return Result<AnimationStoreIterator>::Err(Result_t::OutOfMemory);
    } catch (const std::filesystem::filesystem_error& e) {
        ESP_LOGE(TAG, "Filesystem error while opening animation iterator: %s", e.what());
        // File system failure
        return Result<AnimationStoreIterator>::Err(Result_t::FileNotFound);
    }
}

bool CAnimationStore::NextAnimation(AnimationStoreIterator& iterator, std::string& fileName, Result_t& result) {
    // Sanity checks
    if (!iterator) {
        ESP_LOGE(TAG, "Invalid animation iterator");
        // Bad args
        result = Result_t::InvalidArg;
        return false;
    }
    if (iterator->Failed) {
        // Iteration has failed
        ESP_LOGE(TAG, "Animation iterator has failed with pending error: %d", static_cast<int>(iterator->PendingError));
        result = iterator->PendingError;
        return false;
    }

    std::error_code err;
    try {
        while (iterator->Current != iterator->End) {
            // Make sure this is a file, not a directory
            bool isFile = iterator->Current->is_regular_file(err);
            if (err) {
                ESP_LOGE(TAG, "Error checking if current entry is a regular file: %s", err.message().c_str());
                iterator->PendingError = Result_t::Failed;
                iterator->Failed = true;
                result = Result_t::Failed;
                return false;
            }

            if (isFile) {
                // Get the file name
                fileName = iterator->Current->path().filename().string();

                // Move to the next iteration
                iterator->Current.increment(err);
                if (err) {
                    ESP_LOGE(TAG, "Error incrementing animation iterator: %s", err.message().c_str());
                    // Current iteration is valid, so don't return an error
                    // But next iterator call should return this error
                    iterator->PendingError = Result_t::Failed;
                    iterator->Failed = true;
                }

                // Success response
                result = Result_t::OK;
                return true;
            }

            // Not a regular file, so we step past it
            iterator->Current.increment(err);
            if (err) {
                ESP_LOGE(TAG, "Error incrementing animation iterator: %s", err.message().c_str());
                // Iteration failed, and we have no valid result
                // So we immediately return failure on this iteration
                iterator->PendingError = Result_t::Failed;
                iterator->Failed = true;
                result = Result_t::Failed;
                return false;
            }
        }
    } catch (const std::bad_alloc&) {
        // Out of memory
        ESP_LOGE(TAG, "Out of memory");
        result = Result_t::OutOfMemory;
        return false;
    } catch (const std::filesystem::filesystem_error& e) {
        // File system failure
        ESP_LOGE(TAG, "Filesystem error: %s", e.what());
        result = Result_t::FileNotFound;
        return false;
    }

    // Normal iterator exhaustion
    ESP_LOGD(TAG, "Animation iteration complete");
    result = Result_t::OK;
    return false;
}

bool CAnimationStore::ValidateAnimationName(const std::string& name) {
    const char* namePtr = name.c_str();

    if (strcmp(namePtr, ".") == 0)
        return false;
    if (strcmp(namePtr, "..") == 0)
        return false;
    if (strchr(namePtr, '/'))
        return false;
    if (strlen(namePtr) > 255)
        return false;
    
    return true;
}
