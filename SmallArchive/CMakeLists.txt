project(SmallArchive VERSION 1.3)

build_target(
    TYPE APP
    SOURCES
        SmallArchive.cpp
        ../3rd_party/ApexLib/3rd_party/PreCore/datas/reflectorXML.cpp
        ../3rd_party/zlib/adler32.c
        ../3rd_party/zlib/crc32.c
        ../3rd_party/zlib/inffast.c
        ../3rd_party/zlib/inflate.c
        ../3rd_party/zlib/inftrees.c
        ../3rd_party/zlib/uncompr.c
        ../3rd_party/zlib/zutil.c
        ../3rd_party/zlib/trees.c
        ../3rd_party/zlib/deflate.c
    LINKS
        ApexLib
    INCLUDES
        ../3rd_party/ApexLib/include
        ../3rd_party/ApexLib/3rd_party/PreCore
        ../3rd_party/ApexLib/3rd_party/pugixml/src
        ../3rd_party/zlib
    AUTHOR "Lukas Cone"
    DESCR "SARC Converter"
    NAME "SmallArchive"
    START_YEAR 2017
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin
)

