cmake_minimum_required(VERSION 3.22)
project(KartRecomp VERSION 0.2.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


add_subdirectory(app)
enable_testing()
add_subdirectory(tests)
