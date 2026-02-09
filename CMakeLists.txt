cmake_minimum_required(VERSION 3.20)

project(lvgl_industrial_dashboard_pc C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(FETCH_DEPS "Fetch LVGL + lv_drivers from GitHub at configure time" ON)
option(PREFER_LOCAL_DEPS "Prefer deps from third_party/ when available" ON)

set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# Optional local overrides (useful in offline/corporate networks)
set(LVGL_DIR "" CACHE PATH "Path to a local lvgl checkout (expects CMakeLists.txt)")
set(LV_DRIVERS_DIR "" CACHE PATH "Path to a local lv_drivers checkout")
set(SDL2_SOURCE_DIR "" CACHE PATH "Path to a local SDL2 source checkout (optional)")

include(FetchContent)

if(PREFER_LOCAL_DEPS)
  if(LVGL_DIR STREQUAL "" AND EXISTS "${THIRD_PARTY_DIR}/lvgl/CMakeLists.txt")
    set(LVGL_DIR "${THIRD_PARTY_DIR}/lvgl" CACHE PATH "" FORCE)
  endif()
  if(LV_DRIVERS_DIR STREQUAL "" AND EXISTS "${THIRD_PARTY_DIR}/lv_drivers/CMakeLists.txt")
    set(LV_DRIVERS_DIR "${THIRD_PARTY_DIR}/lv_drivers" CACHE PATH "" FORCE)
  endif()
  if(SDL2_SOURCE_DIR STREQUAL "" AND EXISTS "${THIRD_PARTY_DIR}/SDL/CMakeLists.txt")
    set(SDL2_SOURCE_DIR "${THIRD_PARTY_DIR}/SDL" CACHE PATH "" FORCE)
  endif()
endif()

if(NOT (LVGL_DIR STREQUAL "") AND NOT (LV_DRIVERS_DIR STREQUAL ""))
  message(STATUS "Using local LVGL: ${LVGL_DIR}")
  message(STATUS "Using local lv_drivers: ${LV_DRIVERS_DIR}")

  add_subdirectory("${LVGL_DIR}" lvgl)
  add_subdirectory("${LV_DRIVERS_DIR}" lv_drivers)
elseif(FETCH_DEPS)
  FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v8.3.11
  )

  FetchContent_Declare(
    lv_drivers
    GIT_REPOSITORY https://github.com/lvgl/lv_drivers.git
    GIT_TAG release/v8.3
  )

  FetchContent_MakeAvailable(lvgl lv_drivers)
else()
  message(FATAL_ERROR
    "LVGL deps are missing and FETCH_DEPS=OFF.\n"
    "Provide local checkouts via:\n"
    "  -DLVGL_DIR=... (contains CMakeLists.txt)\n"
    "  -DLV_DRIVERS_DIR=... (contains CMakeLists.txt)\n"
    "Or put them here (recommended):\n"
    "  ${THIRD_PARTY_DIR}/lvgl\n"
    "  ${THIRD_PARTY_DIR}/lv_drivers\n"
  )
endif()

## SDL2 is required by lv_drivers/monitor (PC simulator)
# On some Windows setups SDL2 dev package is not available.
# In that case we fetch and build SDL2 automatically.
set(SDL2_TARGET "")
find_package(SDL2 QUIET)

if(TARGET SDL2::SDL2)
  set(SDL2_TARGET SDL2::SDL2)
elseif(SDL2_FOUND)
  set(SDL2_TARGET ${SDL2_LIBRARIES})
endif()

if(NOT SDL2_TARGET)
  if(NOT (SDL2_SOURCE_DIR STREQUAL ""))
    message(STATUS "Using local SDL2 source: ${SDL2_SOURCE_DIR}")

    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)

    add_subdirectory("${SDL2_SOURCE_DIR}" SDL2)

    if(TARGET SDL2::SDL2)
      set(SDL2_TARGET SDL2::SDL2)
    elseif(TARGET SDL2-static)
      set(SDL2_TARGET SDL2-static)
    else()
      message(FATAL_ERROR "Local SDL2 added, but no known CMake target was found")
    endif()
  elseif(FETCH_DEPS)
    message(STATUS "SDL2 not found via find_package(); fetching SDL2...")
    FetchContent_Declare(
      SDL2
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG release-2.30.9
    )

    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(SDL2)

    if(TARGET SDL2::SDL2)
      set(SDL2_TARGET SDL2::SDL2)
    elseif(TARGET SDL2-static)
      set(SDL2_TARGET SDL2-static)
    else()
      message(FATAL_ERROR "Fetched SDL2 but no known CMake target was found")
    endif()
  else()
    message(FATAL_ERROR
      "SDL2 not found and FETCH_DEPS=OFF.\n"
      "Install SDL2 dev package or provide SDL2_SOURCE_DIR / third_party/SDL." )
  endif()
endif()

add_executable(dashboard_pc
  src/main.c
  src/app/app.c
  src/app/screens/dashboard.c
  src/app/obuf.c
  src/app/lv_font_simsun_16_cjk.c
  src/app/my_font_30.c
)

target_include_directories(dashboard_pc PRIVATE
  ${SDL2_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/config
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(dashboard_pc PRIVATE
  LV_CONF_INCLUDE_SIMPLE
)

# Ensure LVGL itself can find our config/lv_conf.h when building as a dependency
if(TARGET lvgl)
  target_compile_definitions(lvgl PUBLIC
    LV_CONF_INCLUDE_SIMPLE
  )
  target_include_directories(lvgl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/config
  )
endif()

# Ensure lv_drivers can find config/lv_drv_conf.h
if(TARGET lv_drivers)
  target_compile_definitions(lv_drivers PUBLIC
    LV_DRV_CONF_INCLUDE_SIMPLE
  )
  target_include_directories(lv_drivers PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/config
  )

  # lv_drivers/sdl needs SDL headers while compiling
  if(SDL2_TARGET)
    target_link_libraries(lv_drivers PUBLIC
      ${SDL2_TARGET}
    )
  endif()
endif()

# LVGL CMake target is typically "lvgl" on v8
# lv_drivers target is typically "lv_drivers"

target_link_libraries(dashboard_pc PRIVATE
  lvgl
  lv_drivers
  ${SDL2_TARGET}
)
