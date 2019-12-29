project(ddscConvert VERSION 1.2)

build_target(
    TYPE APP
    SOURCES
        ddscConvert.cpp
        ../3rd_party/ApexLib/3rd_party/PreCore/datas/reflectorXML.cpp
    LINKS
        ApexLib
    INCLUDES
        ../3rd_party/ApexLib/include
        ../3rd_party/ApexLib/3rd_party/PreCore
        ../3rd_party/ApexLib/3rd_party/pugixml/src
    AUTHOR "Lukas Cone"
    DESCR "DDSC Texture Converter"
    NAME "ddscConvert"
    START_YEAR 2019
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin
)