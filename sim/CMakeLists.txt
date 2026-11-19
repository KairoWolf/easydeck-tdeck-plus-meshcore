# EasyDeck desktop UI simulator: renders the real UI code into PNG screenshots.
#   cmake -S sim -B sim/build && cmake --build sim/build -j && sim/build/easydeck_sim sim/scenarios/tour.txt sim/shots
cmake_minimum_required(VERSION 3.16)
project(easydeck_sim C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

set(FW ${CMAKE_CURRENT_SOURCE_DIR}/../firmware)
set(LVGL_DIR ${FW}/.pio/libdeps/tdeck_plus/lvgl CACHE PATH "LVGL source dir")

file(GLOB_RECURSE LVGL_SRC ${LVGL_DIR}/src/*.c)
file(GLOB UI_SRC ${FW}/src/ui/*.cpp ${FW}/src/ui/screens/*.cpp ${FW}/src/ui/fonts/*.c ${FW}/src/core/*.cpp)

add_executable(easydeck_sim main.cpp SimModel.cpp ${UI_SRC} ${LVGL_SRC})
target_include_directories(easydeck_sim PRIVATE ${FW}/include ${FW}/src ${FW}/src/ui ${LVGL_DIR} ${LVGL_DIR}/src)
target_compile_definitions(easydeck_sim PRIVATE LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE EASYDECK_SIM SIM_TILE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tiles")
target_compile_options(easydeck_sim PRIVATE -O1 -w -g -fsanitize=address -fno-omit-frame-pointer)
target_link_options(easydeck_sim PRIVATE -fsanitize=address)
target_link_libraries(easydeck_sim m)
