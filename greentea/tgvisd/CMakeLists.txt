# SPDX-License-Identifier: GPL-2.0-only

cmake_minimum_required(VERSION 3.4 FATAL_ERROR)
project(tgvisd VERSION 1.0 LANGUAGES C CXX)

list(APPEND CMAKE_PREFIX_PATH "tdlib/lib/cmake/Td")

find_package(Td 1.8.4 REQUIRED)
include_directories(..)
include_directories(tdlib/include)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS "-Wall -Wextra -ggdb3 -Wpedantic -fno-exceptions -fno-omit-frame-pointer -Wno-gnu-statement-expression -Wno-unused-parameter")

if(SANITIZE_BUILD)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector-strong -fsanitize=address")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-stack-protector")
endif()

add_library(tgvisdtd SHARED
	Td/Td.cpp
	Td/Td.hpp
)
set_property(TARGET tgvisdtd PROPERTY CXX_STANDARD 20)
target_link_libraries(tgvisdtd PRIVATE Td::TdStatic pthread)
target_include_directories(tgvisdtd PRIVATE tdlib/include)
target_compile_options(tgvisdtd PRIVATE -flto=thin)
target_link_options(tgvisdtd PRIVATE -flto=thin)


add_executable(tgvisd
	common.h
	compiler_attributes.h
	main.cpp
	print.c
	print.h
	WorkQueue.cpp
	WorkQueue.hpp
)
target_link_libraries(tgvisd PRIVATE tgvisdtd ssl crypto mysqlclient pthread)
target_include_directories(tgvisd PRIVATE tdlib/include)
