#pragma once

#include <memory>
#include <string>
#include <esp_err.h>

#include "../DataTypes.hpp"

class CAnimationStore {
    public:
        // Log tag
        static constexpr const char* TAG = "CAnimationStore";

        /// @brief Initializes the animation storage, mounting the storage file system.
        /// @return A Result_t status code indicating success or a failure reason.
        Result_t Initialize();

        /// @brief Tests if an animation exists.
        /// @param name (in) The name of the animation to find.
        /// @return A Result containing true if animation exists, false if not found, or an error result code.
        Result<bool> Exists(const std::string_view name);

        /// @brief Loads an animation from flash storage.
        /// @param name (in) The name of the animation to load.
        /// @return A Result containing the animation, or an error code result.
        Result<LightAnimationPtr> Load(const std::string_view name);

        /// @brief Saves an animation to flash storage.
        /// @param name (in) The name to save the animation under.
        /// @param animation (in) The animation to save.
        /// @return A Result_t status code indicating success or a failure reason.
        Result_t Save(const std::string_view name, const LightAnimationPtr& animation);

        /// @brief Deletes a saved animation.
        /// @param name (in) The name of the animation to delete.
        /// @return A Result_t status code indicating success or a failure reason.
        Result_t Delete(const std::string_view name);

        /// @brief Starts iterating over available animations.
        /// @return A Result containing an AnimationStoreIterator, or a failure code.
        Result<AnimationStoreIterator> OpenAnimationIterator();

        /// @brief Proceeds to the next item in the animation iteration.
        /// @param iterator (in) The iterator opened with OpenAnimationIterator().
        /// @param name (in/set) A std::string to receive the animation name.
        /// @param result (out) The result code of the movement.
        /// @return True on success, false to indicate no result.
        bool NextAnimation(AnimationStoreIterator& iterator, std::string& name, Result_t& result);

        /// @brief Validates that a string is valid for use as an animation name.
        /// @param name (in) The string to validate.
        /// @return True if valid, otherwise false.
        static bool ValidateAnimationName(const std::string& name);

};

// Make the static AnimationStore instance globally available
extern CAnimationStore AnimationStore;