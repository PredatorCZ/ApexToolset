cmake_minimum_required(VERSION 3.3)

project(ApexToolset)

set(TARGETEX_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/3rd_party/ApexLib/3rd_party/PreCore/cmake)
include(${TARGETEX_LOCATION}/targetex.cmake)
include(${TARGETEX_LOCATION}/chartype.cmake)

add_subdirectory(3rd_party/ApexLib)
add_subdirectory(ddscConvert)
add_subdirectory(R2SmallArchive)
add_subdirectory(SmallArchive)
