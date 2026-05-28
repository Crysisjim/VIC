cmake_minimum_required(VERSION 3.24)
project(VIC LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /diagnostics:column)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# --- Sources
add_executable(VIC
  main.cpp
  backends/imgui_impl_sdl2.cpp
  backends/imgui_impl_opengl3.cpp
  assets/icon.rc
)

# --- Includes locaux
target_include_directories(VIC PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/backends
)

# --- OpenGL
find_package(OpenGL REQUIRED)

# --- SDL2 + SDL2_ttf via vcpkg
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)

# --- GLEW
find_package(GLEW CONFIG REQUIRED)    # GLEW::GLEW

# --- OpenCV
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio)

# --- ImGui via vcpkg
find_library(IMGUI_LIB NAMES imgui REQUIRED)

if(WIN32)
  list(APPEND EXTRA_WIN_LIBS Comdlg32)
endif()

# NOTE GLEW STATIC :
# target_compile_definitions(VIC PRIVATE GLEW_STATIC)  # uniquement si glew statique

target_link_libraries(VIC PRIVATE
  ${IMGUI_LIB}
  SDL2::SDL2
  SDL2_ttf::SDL2_ttf
  OpenGL::GL
  GLEW::GLEW
  ${OpenCV_LIBS}
  ${EXTRA_WIN_LIBS}
)

if(MSVC)
  set_target_properties(VIC PROPERTIES
    # WINDOWS subsystem pour cacher la fenêtre cmd par défaut
    LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
  )
endif()

# --- Plus de copie manuelle de DLL ici.
# applocal.ps1 (appelé par vcpkg/VS) s'occupe déjà de déposer les dépendances à côté de l'exe.
