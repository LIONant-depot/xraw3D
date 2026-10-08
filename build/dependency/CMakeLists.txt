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

# Linux: the same assimp sources, built with the host compiler into BINARIES/Linux (static, PIC) the first time a
# compiler that uses xraw3d is configured - what the block above does with Visual Studio into BINARIES/Win32.
if(NOT WIN32)
  set(XRAW3D_ASSIMP_SRC   "${CMAKE_SOURCE_DIR}/dependencies/assimp")
  set(XRAW3D_ASSIMP_LINUX "${XRAW3D_ASSIMP_SRC}/BINARIES/Linux")
  file(MAKE_DIRECTORY "${XRAW3D_ASSIMP_SRC}/BINARIES")
  file(LOCK "${XRAW3D_ASSIMP_SRC}/BINARIES/linux.lock" GUARD FILE TIMEOUT 3600)
  if(NOT EXISTS "${XRAW3D_ASSIMP_LINUX}/lib/libassimp.a")
    message(STATUS "Building assimp (Linux, static) into ${XRAW3D_ASSIMP_LINUX}...")
    execute_process(
      COMMAND ${CMAKE_COMMAND} -G Ninja -S "${XRAW3D_ASSIMP_SRC}" -B "${XRAW3D_ASSIMP_LINUX}"
              -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
              -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DASSIMP_BUILD_ZLIB=ON
              -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_SAMPLES=OFF -DASSIMP_INSTALL=OFF
              -DASSIMP_WARNINGS_AS_ERRORS=OFF -DASSIMP_INJECT_DEBUG_POSTFIX=OFF
      RESULT_VARIABLE config_result)
    if(config_result)
      message(FATAL_ERROR "assimp: CMake configuration failed: ${config_result}")
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} --build "${XRAW3D_ASSIMP_LINUX}" RESULT_VARIABLE build_result)
    if(build_result)
      message(FATAL_ERROR "assimp: build failed: ${build_result}")
    endif()
  endif()
  file(LOCK "${XRAW3D_ASSIMP_SRC}/BINARIES/linux.lock" RELEASE)
  file(GLOB_RECURSE XRAW3D_ZLIB "${XRAW3D_ASSIMP_LINUX}/contrib/*libzlibstatic.a")
  if( xraw3d_CREATED )
    AppendComponentIncludes(xraw3d "${XRAW3D_ASSIMP_LINUX}/include")
  endif()
  if(POLICY CMP0079)
    cmake_policy(SET CMP0079 NEW)   # links the plugin's executable, defined in the plugin's top directory
  endif()
  target_link_libraries(${TARGET_PROJECT} PRIVATE "${XRAW3D_ASSIMP_LINUX}/lib/libassimp.a" ${XRAW3D_ZLIB})
endif()
