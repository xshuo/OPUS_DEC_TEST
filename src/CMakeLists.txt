#xshuo c1 avc(opus dec)
cmake_minimum_required(VERSION 3.2)

MESSAGE(STATUS "This is SOURCE dir " ${PROJECT_SOURCE_DIR})
MESSAGE(STATUS "This is BINARY dir " ${PROJECT_BINARY_DIR})

#SET(PROJECT_BINARY_DIR build)
#SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

#MESSAGE(STATUS "Change BINARY dir to " ${PROJECT_BINARY_DIR})


INCLUDE_DIRECTORIES(
    ${PROJECT_SOURCE_DIR}/include
    )

LINK_DIRECTORIES(
    ${PROJECT_SOURCE_DIR}/lib
    )

set (CMAKE_CXX_STANDARD 11)
ADD_DEFINITIONS(-DChanelstereo)

AUX_SOURCE_DIRECTORY(. SRC_LIST)
FIND_LIBRARY(LIB_PTHREAD libpthread.so REQUIRED)
FIND_LIBRARY(LIB_AVC libavcodecstereo.a REQUIRED)
MESSAGE("pthread is: ${LIB_PTHREAD}")
MESSAGE("pthread is: ${LIB_AVC}")

ADD_EXECUTABLE(C1_OPUS_DEC ${SRC_LIST})

TARGET_LINK_LIBRARIES(
    C1_OPUS_DEC
    libavcodecstereo.a
    ${LIB_PTHREAD}
    )

