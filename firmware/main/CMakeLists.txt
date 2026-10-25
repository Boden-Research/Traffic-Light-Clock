# Automatically find all source code files in project and add to build
file(GLOB_RECURSE SOURCES
     "*.c" "*.c"
     "*.cpp" "*.cpp"
)

# Automatically find all header files in project and add to header search paths
file(GLOB_RECURSE HEADERS
     "*.h" "*.h"
     "*.hpp" "*.hpp"
)

set(INCLUDE_DIRS ".")

# Find all directories containing header files
foreach(header_file ${HEADERS})
    get_filename_component(header_dir ${header_file} DIRECTORY)
    list(APPEND INCLUDE_DIRS ${header_dir})
endforeach()
list(REMOVE_DUPLICATES INCLUDE_DIRS)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    REQUIRES "spiffs" "esp_http_server" "esp_wifi" "esp_netif" "nvs_flash" "esp_event" "driver" "littlefs"
)

# Compile the httproot folder into a SPIFFS partition for flashing
spiffs_create_partition_image(www "../httproot" FLASH_IN_PROJECT)

# Enable all warnings as errors for this project
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
