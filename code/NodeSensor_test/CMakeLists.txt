# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Chi build "main" + cac component no thuc su can (khong build esp_lcd, fatfs...)
# -> tranh loi ICE cua GCC o esp_lcd va build nhanh hon nhieu
set(COMPONENTS main)

project(NodeSensor_test)