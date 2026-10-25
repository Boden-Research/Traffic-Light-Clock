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

#include "../DataTypes.hpp"
#include "Storage.hpp"

#include "AnimationStore.hpp"

#define STORAGE_LABEL "storage" // Partition label
#define MOUNT_POINT "/storage" // Internal mount path

// Singleton instance of CAnimationStore class, available everywhere
CAnimationStore AnimationStore;

Result_t CAnimationStore::Initialize() {
    // Make sure the file system is mounted
    return Storage.Mount(FileSystemType_t::LittleFS, STORAGE_LABEL, MOUNT_POINT, true);
}

Result<bool> CAnimationStore::Exists(const std::string_view name) {
    // Sanity check
    if (name.empty())
        return Result<bool>::Err(Result_t::InvalidArg);

    // Get the correct file name
    Result<std::string> result = Storage.GetFilePath(MOUNT_POINT "/animations", name);
    if (!result)
        return Result<bool>::Err(result.error());

    // Determine if the file exists
    struct stat st;
    bool exists = stat(result.value().c_str(), &st) == 0;

    return Result<bool>::Ok(exists);
}

Result<LightAnimationPtr> CAnimationStore::Load(const std::string_view name) {
    // Sanity checks
    if (name.empty())
        return Result<LightAnimationPtr>::Err(Result_t::InvalidArg);

    Result<std::string> fullPath = Storage.GetFilePath(MOUNT_POINT "/animations", name);
    if (!fullPath)
        return Result<LightAnimationPtr>::Err(fullPath.error());

    try {
        // Open the file
        std::ifstream file(fullPath.value(), std::ios::binary | std::ios::in);
        if (!file)
            return Result<LightAnimationPtr>::Err(Result_t::FileNotFound);

        // Read the frame rate
        float frameRate;
        file.read(reinterpret_cast<char*>(&frameRate), sizeof(frameRate));
        if (!file)
            return Result<LightAnimationPtr>::Err(Result_t::FileLoadEOF);

        // Read the frame count
        uint16_t numFrames;
        file.read(reinterpret_cast<char*>(&numFrames), sizeof(numFrames));
        if (!file)
            return Result<LightAnimationPtr>::Err(Result_t::FileLoadEOF);

        // Sanity check
        if (!std::isfinite(frameRate) || frameRate <= 0.0f || numFrames == 0)
            return Result<LightAnimationPtr>::Err(Result_t::FileLoadBadFile);

        // Instantiate the LightAnimationPtr
        LightAnimationPtr animation = std::make_unique<LightAnimation_t>();
        animation->FrameRate = frameRate;
        animation->Frames.resize(numFrames);

        // Load the frames
        for (LightState_t& frame : animation->Frames) {
            // Read the frame
            file.read(reinterpret_cast<char*>(frame.data()), static_cast<std::streamsize>(frame.size()));
            if (!file)
                return Result<LightAnimationPtr>::Err(Result_t::FileLoadEOF);
        }

        // Return successful result
        return Result<LightAnimationPtr>::Ok(std::move(animation));
    } catch (const std::bad_alloc&) {
        // Out of memory
        return Result<LightAnimationPtr>::Err(Result_t::OutOfMemory);
    }
}

Result_t CAnimationStore::Save(const std::string_view name, const LightAnimationPtr& animation) {
    // Sanity check
    if (name.empty())
        return Result_t::InvalidArg;
    if (!std::isfinite(animation->FrameRate) ||
        animation->FrameRate < 0.0f ||
        animation->Frames.size() == 0)
            return Result_t::InvalidArg;

    // Get the correct file name
    Result<std::string> fullPath = Storage.GetFilePath(MOUNT_POINT "/animations", name);
    if (!fullPath)
        return fullPath.error();

    try {
        // Open the file
        std::ofstream file(fullPath.value(), std::ios::binary | std::ios::out | std::ios::trunc);
        if (!file)
            return Result_t::FileNotFound;

        // Write the frame rate
        file.write(reinterpret_cast<const char*>(&animation->FrameRate), sizeof(animation->FrameRate));
        if (!file)
            return Result_t::OutOfStorageSpace;

        // Write the frame count
        std::size_t size = animation->Frames.size();
        file.write(reinterpret_cast<const char*>(&size), sizeof(size));
        if (!file)
            return Result_t::OutOfStorageSpace;

        // Write the frames
        for (const LightState_t& frame : animation->Frames) {
            // Write one frame
            file.write(reinterpret_cast<const char*>(frame.data()), frame.size());
            if (!file)
                return Result_t::OutOfStorageSpace;
        }

        // Success
        return Result_t::OK;
    } catch (const std::bad_alloc&) {
        // Out of memory
        return Result_t::OutOfMemory;
    }
}

Result_t CAnimationStore::Delete(const std::string_view name) {
    // Sanity check
    if (name.empty())
        return Result_t::InvalidArg;

    // Get the correct file name
    Result<std::string> fullPath = Storage.GetFilePath(MOUNT_POINT "/animations", name);
    if (!fullPath)
        // Pass error back to caller
        return fullPath.error();

    // Delete the file
    std::error_code err;
    std::filesystem::remove(fullPath.value(), err);
    if (err)
        return Result_t::FileNotFound;

    // Success
    return Result_t::OK;
}

Result<AnimationStoreIterator> CAnimationStore::OpenAnimationIterator() {
    try {
        // Create a new iterator instance
        AnimationStoreIterator iterator = std::make_unique<AnimationStoreIterator_t>();

        // Start an std library iteration over the animations directory
        std::error_code err;
        iterator->Current = std::filesystem::directory_iterator(MOUNT_POINT "/animations", err);

        // Test for error
        if (err)
            return Result<AnimationStoreIterator>::Err(Result_t::Failed);

        return Result<AnimationStoreIterator>::Ok(std::move(iterator));
    } catch (const std::bad_alloc&) {
        // Out of memory
        return Result<AnimationStoreIterator>::Err(Result_t::OutOfMemory);
    } catch (const std::filesystem::filesystem_error&) {
        // File system failure
        return Result<AnimationStoreIterator>::Err(Result_t::FileNotFound);
    }
}

bool CAnimationStore::NextAnimation(AnimationStoreIterator& iterator, std::string& fileName, Result_t& result) {
    // Sanity checks
    if (!iterator) {
        // Bad args
        result = Result_t::InvalidArg;
        return false;
    }
    if (iterator->Failed) {
        // Iteration has failed
        result = iterator->PendingError;
        return false;
    }

    std::error_code err;
    try {
        while (iterator->Current != iterator->End) {
            // Make sure this is a file, not a directory
            bool isFile = iterator->Current->is_regular_file(err);
            if (err) {
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
        result = Result_t::OutOfMemory;
        return false;
    } catch (const std::filesystem::filesystem_error&) {
        // File system failure
        result = Result_t::FileNotFound;
        return false;
    }

    // Normal iterator exhaustion
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
