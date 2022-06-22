# SPDX-License-Identifier: GPL-2.0-only

cmake_minimum_required(VERSION 3.4 FATAL_ERROR)
project(tgvisd VERSION 1.0 LANGUAGES C CXX)

list(APPEND CMAKE_PREFIX_PATH "tdlib/lib/cmake/Td")

find_package(Td 1.8.1 REQUIRED)
include_directories(..)
include_directories(tdlib/include)
