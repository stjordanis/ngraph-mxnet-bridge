#*******************************************************************************
# Copyright 2018 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#*******************************************************************************

cmake_minimum_required(VERSION 3.0.2)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-comment -mf16c")
SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-g")
SET(CMAKE_CXX_FLAGS_DEBUG  "-O0 -g")
set(NGRAPH_INSTALL_PREFIX ${CMAKE_CURRENT_BINARY_DIR})

# Flags required for SDL325
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -D_FORTIFY_SOURCE=2 -fPIE")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wformat -Wformat-security")
if (APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-bind_at_load")
    set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} -Wl,-bind_at_load")
else()
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pie")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now")
    set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now")
endif()
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.9)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector-strong")
    endif()
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector-strong")
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(UNIX)
	find_program(LSB_RELEASE_EXEC lsb_release)
	execute_process(COMMAND ${LSB_RELEASE_EXEC} -d
	    OUTPUT_VARIABLE LSB_RELEASE_ID
	    OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	if(LSB_RELEASE_ID MATCHES "Ubuntu 16.04")
		set(NGRAPH_USE_PREBUILT_LLVM 1)
	endif()
endif()

if(USE_NGRAPH_IE)
  add_definitions(-DMXNET_USE_NGRAPH_IE=1)
endif(USE_NGRAPH_IE)

if(USE_NGRAPH_INTERPRETER)
  add_definitions(-DMXNET_USE_NGRAPH_INTERPRETER=1)
endif(USE_NGRAPH_INTERPRETER)

if(USE_NGRAPH_DISTRIBUTED)
  add_definitions(-DMXNET_USE_NGRAPH_DISTRIBUTED=1)
endif(USE_NGRAPH_DISTRIBUTED)

if (CUDA_INCLUDE_DIRS)
  include_directories(SYSTEM ${CUDA_INCLUDE_DIRS})
else()
  if (USE_NGRAPH_GPU)
    message(FATAL_ERROR "USE_CUDA_PATH should be set when USE_NGRAPH_GPU is enabled.")
  endif()
endif()
if(USE_MKLDNN)
    add_definitions(-DMXNET_USE_MKLDNN=1)
    include_directories("${MKLDNN_INCLUDE_DIR}")
endif()


include(cmake/ngraph.cmake)

include_directories(src src/ops)
include_directories(../../include/)
include_directories(../mshadow/)
include_directories(../dmlc-core/include/)
include_directories(../tvm/nnvm/include/)
include_directories(../tvm/dlpack/include/)
include_directories(../dlpack/include/)
include_directories(build/include/)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/ngraph/src/ext_ngraph-build/mkl/src/ext_mkl/include)

add_subdirectory(src)

set(NGRAPH_BRIDGE_UNIT_TEST_SOURCE "")
add_subdirectory(tests/ngraph)
get_directory_property(NGRAPH_BRIDGE_UNIT_TEST_SOURCE DIRECTORY tests/ngraph DEFINITION NGRAPH_BRIDGE_UNIT_TEST_SOURCE) 

add_dependencies(ngraph-mxnet-bridge ext_ngraph)
