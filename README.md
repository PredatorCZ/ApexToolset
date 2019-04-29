# ApexToolset
Apex Toolset is a collection of modding tools for Apex Engine titles.
## ddscConvert
Converts between AVTX and DDS formats. This app uses multithreading, so you can process multiple files at the same time. Best way is to drag'n'drop files onto app.
For this reason a .config file is placed alongside executable file, since app itself only takes file paths as arguments.
A .config file is in XML format. \
***Please do not create any spaces/tabs/uppercase letters/commas as decimal points within setting field. \
Program must run at least once to generate .config file.\
If a DDS is being converted to AVTX, make sure that DDS is properly encoded and have generated full mipmap chain.***

### Recommended input DDS encodings:
- Diffuse/Albedo/Base color/Mask texture without alpha channel: DXT1(BC1)
- Diffuse/Albedo/Base color/Mask texture with alpha channel: DXT5(BC3)
- Normal texture: ATI2(BC5)
- Greyscale texture: ATI1(BC4)
- Cubemap texture: 32bit RGBA

### Settings (.config file):
- ***Convert_DDS_to_legacy:***\
        Tries to convert AVTX into legacy (DX9) DDS format.
- ***Force_unconvetional_legacy_formats:***\
        Will try to convert some matching formats from DX10 to DX9, for example: RG88 to AL88.
- ***Generate_Log:***\
        Will generate text log of console output next to application location.
        
### Following settings are for AVTX creation:
- ***Number_of_ATX_levels:***\
        Number of streamed mipmaps files.\
        Titles like JC4 will use 2, Generation Zero uses 3.\
        0 means that all mip maps will be stored in one ddsc file.
- ***Use_HMDDSC:***\
        Use for titles like JC3 or the Hunter COtW.\
        It will create one .hmddsc file instead of .atx.
- ***ATX_levelN_max_resolution:***\
        Maximum texture resolution for said level.\
        Level 0 is main ddsc file, level 1 is atx1 or hmddsc file, level 2 is for atx2 and so on.
- ***No_Tiling:***\
        Texture should not tile. Should be used for object baked textures.
        
## [Latest Release](https://github.com/PredatorCZ/ApexToolset/releases)

## License
This toolset is available under GPL v3 license. (See LICENSE.md)

This toolset uses following libraries:

* ApexLib, Copyright (c) 2014-2019 Lukas Cone

  
