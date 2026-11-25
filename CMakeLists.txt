cmake_minimum_required(VERSION 3.16)
project(vision_hw1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源码与输出里有中文，MSVC 需要加 /utf-8，否则会按 GBK 解析导致编译告警或乱码
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(OpenCV REQUIRED)
message(STATUS "Found OpenCV ${OpenCV_VERSION}")
message(STATUS "  include: ${OpenCV_INCLUDE_DIRS}")
message(STATUS "  libs   : ${OpenCV_LIBS}")

# ---- 任务一：蓝色/红色矩形框选 ----
add_executable(task1_color_box src/main_task1.cpp)
target_include_directories(task1_color_box PRIVATE src ${OpenCV_INCLUDE_DIRS})
target_link_libraries(task1_color_box PRIVATE ${OpenCV_LIBS})

# ---- 任务二：装甲板灯条检测与配对 ----
add_executable(task2_armor src/main_task2.cpp)
target_include_directories(task2_armor PRIVATE src ${OpenCV_INCLUDE_DIRS})
target_link_libraries(task2_armor PRIVATE ${OpenCV_LIBS})

# Windows 提示：运行前需要能找到 opencv_world4xx.dll，
# 把 <opencv>/build/x64/vc16/bin 加入 PATH，或把 dll 复制到可执行文件同目录。
