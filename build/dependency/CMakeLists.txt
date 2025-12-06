include(${CMAKE_BINARY_DIR}/_deps/xcmake_tools/Common.cmake)

FetchAndPopulate("https://github.com/LIONant-depot/xstrtool.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xbitmap.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xtextfile.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xmath.git" "main")
FetchAndPopulate("https://github.com/assimp/assimp.git" "master")
FetchAndPopulate("https://github.com/mmikk/MikkTSpace.git" "master")

if( assimp_POPULATED )
  # Configure the project for Visual Studio 2022 (Win32)
  message(STATUS "Configuring CMake project...")
  execute_process(
      COMMAND ${CMAKE_COMMAND} -G "Visual Studio 17 2022" -S "${CMAKE_SOURCE_DIR}/dependencies/assimp" -B "${CMAKE_SOURCE_DIR}/dependencies/assimp/BINARIES/Win32"
      RESULT_VARIABLE config_result
  )
  if(config_result)
      message(FATAL_ERROR "CMake configuration failed: ${config_result}")
  endif()

  # Build the project in Release configuration
  message(STATUS "Building project in Release configuration...")
  execute_process(
      COMMAND ${CMAKE_COMMAND} --build "${CMAKE_SOURCE_DIR}/dependencies/assimp/BINARIES/Win32" --config Release
      RESULT_VARIABLE build_result
  )
  if(build_result)
      message(FATAL_ERROR "CMake build failed: ${build_result}")
  endif()
endif()

DefineInterfaceComponent(xraw3d "dependencies/xtools"
  "source/xraw3d.cpp"
  "source/xraw3d.h"
  "source/xraw3d_anim.h"
  "source/xraw3d_assimp_import.h"
  "source/xraw3d_geom.h"
  "README.md"
)
if( xraw3d_CREATED )
  AppendComponentIncludes(xraw3d "${CMAKE_SOURCE_DIR}/dependencies/assimp/include")
  AppendComponentIncludes(xraw3d "${CMAKE_SOURCE_DIR}/dependencies/assimp/BINARIES/Win32/include")
  AppendComponentLinkerPaths( xraw3d "${CMAKE_SOURCE_DIR}/.")
  
endif()