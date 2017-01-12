cmake_minimum_required(VERSION 2.8) # Проверка версии CMake.
#
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -lpthread")
add_executable(final main.cpp)       # Создает исполняемый файл 
