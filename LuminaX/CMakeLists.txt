include(FetchContent)


FetchContent_Declare(
  d3dx12
  GIT_REPOSITORY "https://github.com/microsoft/DirectX-Headers.git"
  GIT_TAG "v1.614.1"
)
FetchContent_MakeAvailable(d3dx12)

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_VIEW OFF CACHE BOOL "" FORCE)
#FetchContent_Declare(
#  assimp
#  GIT_REPOSITORY "https://github.com/assimp/assimp.git"
#)
#FetchContent_MakeAvailable(assimp)


file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)


add_compile_definitions(UNICODE)
add_executable (LuminaX ${SRC_FILES})

target_link_libraries(LuminaX PRIVATE Microsoft::DirectX-Headers d3d12.lib d3dcompiler.lib dxgi.lib)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET LuminaX PROPERTY CXX_STANDARD 20)
endif()