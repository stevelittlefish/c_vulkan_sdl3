cmake_minimum_required(VERSION 3.30)

project(VulkanTest
	VERSION 1.0
	DESCRIPTION "Vulkan Test App"
	LANGUAGES C)

# Option to enable validation layers (default: OFF for release builds)
option(ENABLE_VALIDATION_LAYERS "Enable Vulkan validation layers" OFF)

if(ENABLE_VALIDATION_LAYERS)
	message(STATUS "Validation layers: ENABLED")
	add_compile_definitions(ENABLE_VALIDATION_LAYERS)
else()
	message(STATUS "Validation layers: DISABLED")
endif()

# Function to find the latest Vulkan SDK version in a given directory - needed for good old Windows
function(find_latest_vulkan_sdk base_path output_var)
    file(GLOB SDK_VERSIONS "${base_path}/*")
    list(SORT SDK_VERSIONS ORDER DESCENDING)
    foreach(version IN LISTS SDK_VERSIONS)
        if(IS_DIRECTORY "${version}")
            set(${output_var} "${version}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "No Vulkan SDK version found in ${base_path}")
endfunction()

find_package(Vulkan REQUIRED)

include_directories(${PROJECT_SOURCE_DIR}/include/)

include(sourcelist.cmake)
add_executable(main ${source_list} ${header_list})

# Library linking is OS dependent
if(UNIX)
	message(STATUS "Adding Linux dependencies")
	
	find_package(SDL3 REQUIRED)

	target_link_libraries(
	  main
	  PRIVATE
	  m
	  SDL3
	  vulkan
	)
	
	# On linux let's override the output directory
	set_target_properties(main PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist")

elseif(WIN32)
	message(STATUS "Adding Windows dependencies")

	# Set the base path for Vulkan SDK
	set(VULKAN_SDK_BASE_PATH "C:/VulkanSDK")

	# Find the latest Vulkan SDK version
	find_latest_vulkan_sdk(${VULKAN_SDK_BASE_PATH} VULKAN_SDK_PATH)

	message(STATUS "Found Vulkan SDK at: ${VULKAN_SDK_PATH}")
	
    # Add Vulkan include directory
    target_include_directories(main PUBLIC "${VULKAN_SDK_PATH}/Include")
	target_link_libraries(main PRIVATE "${VULKAN_SDK_PATH}/Lib/vulkan-1.lib")

	target_include_directories(main PUBLIC "${CMAKE_SOURCE_DIR}/win/include")
	target_link_libraries(main PRIVATE "${CMAKE_SOURCE_DIR}/win/lib/SDL3.lib")
	
	target_link_libraries(
		main
		PRIVATE
			msvcrt
	)

	# Copy SDL3.dll to target directory
	add_custom_command(
		TARGET main POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${PROJECT_SOURCE_DIR}/win/bin/SDL3.dll"
			$<TARGET_FILE_DIR:main>
	)

endif()

# Copy shaders to the output directory

# Find all .spv files in the shaders directory
file(GLOB SHADER_SPV_FILES "${PROJECT_SOURCE_DIR}/shaders/*.spv")

# Create the shaders directory in the output directory if it doesn't exist
add_custom_command(
	TARGET main POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory
		"$<TARGET_FILE_DIR:main>/shaders"
)

# Copy each .spv file to the shaders subdirectory in the output directory
foreach(SHADER_FILE ${SHADER_SPV_FILES})
	get_filename_component(FILE_NAME ${SHADER_FILE} NAME)
	add_custom_command(
		TARGET main POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${SHADER_FILE}"
			"$<TARGET_FILE_DIR:main>/shaders/${FILE_NAME}"
	)
endforeach()

