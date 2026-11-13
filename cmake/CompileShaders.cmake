cmake_minimum_required(VERSION 3.20)

find_program(GLSLC_EXECUTABLE NAMES glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin bin)
if(NOT GLSLC_EXECUTABLE)
    message(STATUS "glslc not found. Keeping existing shaders.")
    return()
endif()

file(REAL_PATH "${SHADER_PROJECT_DIR}" project_dir)
file(REAL_PATH "${SHADER_BUILD_DIR}" build_dir)
set(source_dir "${project_dir}/shader_sources")
set(output_dir "${project_dir}/shaders/glsl")
set(staging_dir "${build_dir}/glsl-staging")

# Verify the output paths before removing anything.
foreach(directory IN ITEMS "${project_dir}/shaders" "${output_dir}" "${staging_dir}")
    if(IS_SYMLINK "${directory}")
        message(FATAL_ERROR "Shader output path is a symbolic link: ${directory}")
    endif()
    if(NOT EXISTS "${directory}")
        continue()
    endif()
    file(REAL_PATH "${directory}" resolved_directory)
    if(NOT IS_DIRECTORY "${directory}" OR NOT resolved_directory STREQUAL directory)
        message(FATAL_ERROR "Unexpected shader output directory: ${directory}")
    endif()
endforeach()

file(LOCK "${build_dir}/glsl-compile.lock" GUARD PROCESS)
file(REMOVE_RECURSE "${staging_dir}")
file(MAKE_DIRECTORY "${staging_dir}")

file(GLOB_RECURSE shader_files LIST_DIRECTORIES false RELATIVE "${source_dir}" "${source_dir}/*")
foreach(shader IN LISTS shader_files)
    if(shader MATCHES "\\.glsl$")
        # A .glsl file without a stage pragma can be included by another shader.
        file(STRINGS "${source_dir}/${shader}" stage_pragma
            REGEX "^[ \t]*#[ \t]*pragma[ \t]+shader_stage[ \t]*[(]" LIMIT_COUNT 1)
        if(NOT stage_pragma)
            continue()
        endif()
    elseif(NOT shader MATCHES "\\.(vert|frag|comp)$")
        continue()
    endif()

    set(compiled_shader "${staging_dir}/${shader}")
    get_filename_component(compiled_directory "${compiled_shader}" DIRECTORY)
    file(MAKE_DIRECTORY "${compiled_directory}")
    execute_process(
        COMMAND "${GLSLC_EXECUTABLE}" -x glsl -I "${source_dir}"
            -c "${source_dir}/${shader}" -o "${compiled_shader}"
        RESULT_VARIABLE result
    )
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "glslc failed for ${shader}: ${result}")
    endif()
endforeach()

# Replace the existing output only after all shaders compile successfully.
file(REMOVE_RECURSE "${output_dir}")
file(COPY "${staging_dir}/" DESTINATION "${output_dir}")
