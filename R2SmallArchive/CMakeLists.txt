project(R2SmallArchive VERSION 1.1)

build_target(
    TYPE APP
    SOURCES
        R2SmallArchive.cpp
        ../3rd_party/ApexLib/3rd_party/PreCore/datas/reflectorXML.cpp
    LINKS
        ApexLib
    INCLUDES
        ../3rd_party/ApexLib/include
        ../3rd_party/ApexLib/3rd_party/PreCore
        ../3rd_party/ApexLib/3rd_party/pugixml/src
    AUTHOR "Lukas Cone"
    DESCR "Rage 2 Small Archive extractor"
    NAME "R2SmallArchive"
    START_YEAR 2019
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin
)