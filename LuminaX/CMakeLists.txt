include(FetchContent)

FetchContent_Declare(
  d3dx12
  GIT_REPOSITORY "https://github.com/microsoft/DirectX-Headers.git"
  GIT_TAG "v1.614.1"
  GIT_SHALLOW 1
)
FetchContent_MakeAvailable(d3dx12)

FetchContent_Declare(
	directxtk12
	GIT_REPOSITORY "https://github.com/microsoft/DirectXTK12.git"
	GIT_TAG "oct2024"
	GIT_SHALLOW 1
)
FetchContent_MakeAvailable(directxtk12)

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_VIEW OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  assimp
  GIT_REPOSITORY "https://github.com/assimp/assimp.git"
  GIT_SHALLOW 1
)
FetchContent_MakeAvailable(assimp)


FetchContent_Declare(imgui_external
	GIT_REPOSITORY "https://github.com/ocornut/imgui.git"
	GIT_TAG "v1.91.2-docking"
	GIT_SHALLOW 1
	EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(imgui_external)

add_library(imgui STATIC
	${imgui_external_SOURCE_DIR}/imgui.cpp
	${imgui_external_SOURCE_DIR}/imgui_draw.cpp
	${imgui_external_SOURCE_DIR}/imgui_tables.cpp
	${imgui_external_SOURCE_DIR}/imgui_widgets.cpp
	${imgui_external_SOURCE_DIR}/backends/imgui_impl_win32.cpp
	${imgui_external_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
)
target_compile_definitions(imgui PUBLIC _UNICODE UNICODE _WIN32_WINNT=0x0A00)
target_include_directories(imgui PUBLIC ${imgui_external_SOURCE_DIR} ${imgui_external_SOURCE_DIR}/backends/)
target_link_libraries(imgui PRIVATE d3d12.lib d3dcompiler.lib dxgi.lib)


#FetchContent_Declare(stb_external
#	GIT_REPOSITORY "https://github.com/nothings/stb.git"
#	GIT_SHALLOW 1
#	EXCLUDE_FROM_ALL
#)
#FetchContent_MakeAvailable(stb_external)


file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_executable(LuminaX ${SRC_FILES})
set_target_properties(LuminaX PROPERTIES WIN32_EXECUTABLE TRUE)

target_link_libraries(LuminaX PRIVATE Microsoft::DirectX-Headers DirectXTK12 assimp dxguid.lib d3d12.lib d3dcompiler.lib dxgi.lib imgui)
target_compile_definitions(LuminaX PUBLIC _UNICODE UNICODE _WIN32_WINNT=0x0A00)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/Shaders DESTINATION ${CMAKE_BINARY_DIR}/LuminaX)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET LuminaX PROPERTY CXX_STANDARD 20)
endif()