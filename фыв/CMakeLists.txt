cmake_minimum_required(VERSION 3.15)
project(CloudService VERSION 1.0.0 LANGUAGES C CXX)

# Установка стандартов C и C++
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Опции сборки
option(BUILD_TESTS "Build test suite" ON)
option(USE_SANITIZERS "Enable sanitizers" OFF)

# Настройка компилятора
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    if(USE_SANITIZERS)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined")
    endif()
endif()

# Добавление подпроектов
add_subdirectory(src)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Установка путей для заголовочных файлов
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/third_party)

# Настройка зависимостей
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)
find_package(Redis REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread filesystem) 