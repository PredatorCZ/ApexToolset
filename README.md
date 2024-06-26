> [!IMPORTANT]
> This repo has been archived, it contains out of date toolset.\
> Possible future development/releases will be available in [ApexToolset](https://github.com/PredatorCZ/ApexLib/master/toolset) subproject for [ApexLib](https://github.com/PredatorCZ/ApexLib).

# ApexToolset

Apex Toolset is a collection of modding tools for Apex Engine titles.

## ddscConvert

Converts between AVTX and DDS formats.\
This app uses multithreading, so you can process multiple files at the same time. Best way is to drag'n'drop files or even folders onto app (When folder is dropped, it will be recursively scanned).\
For this reason a .config file is placed alongside executable file, since app itself only takes file paths as arguments.\
A .config file is in XML format. \
***Please do not create any spaces/tabs/uppercase letters/commas as decimal points within setting field. \
Program must run at least once to generate .config file.\
If a DDS is being converted to AVTX, make sure that DDS is properly encoded and have generated full mipmap chain.***

### Recommended input DDS encodings

- Diffuse/Albedo/Base color/Mask texture without alpha channel: DXT1(BC1)
- Diffuse/Albedo/Base color/Mask texture with alpha channel: DXT5(BC3)
- Normal texture: ATI2(BC5)
- Greyscale texture: ATI1(BC4)
- Cubemap texture: 32bit RGBA

### Settings (.config file)

- ***Convert_DDS_to_legacy:***\
        Tries to convert AVTX into legacy (DX9) DDS format.
- ***Force_unconvetional_legacy_formats:***\
        Will try to convert some matching formats from DX10 to DX9, for example: RG88 to AL88.
- ***Generate_Log:***\
        Will generate text log of console output next to application location.
- ***Extract_largest_mipmap:***\
        Will try to extract only highest mipmap.\
        Texture musn't be converted back afterwards, unless you regenerate mipmaps!\
        This setting does not apply, if texture have arrays or is a cubemap!
- ***Folder_scan_DDSC_only:***\
        When providing input parameter as folder, program will scan only DDSC files.\
        When false, program will scan for DDS files only.

### Following settings are for AVTX creation

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

## R2SmallArchive

Extracts .bl, .ee, .nl, .fl archives from RAGE 2. This app uses multithreading, so you can process multiple files at the same time. Best way is to drag'n'drop files onto app.
For this reason a .config file is placed alongside executable file, since app itself only takes file paths as arguments.
A .config file is in XML format. \
***Please do not create any spaces/tabs/uppercase letters/commas as decimal points within setting field. \
Program must run at least once to generate .config file.***

### Settings (.config file)

- ***Generate_Log:***\
        Will generate text log of console output next to application location.
- ***sarc0_gtoc_file_path:***\
        A full file path to sarc.0.gtoc file. (Inside game10 achive)
- ***expentities_gtoc_file_path:***\
        A full file path to expentities.gtoc file. (Inside game10 achive)

## SmallArchive

Extracts or creates .bl, .ee, .nl, .fl, .blz, .eez, .nlz, .flz archives.\
This app uses multithreading, but only for extraction or creation from toc file, so you can process multiple files at the same time. Best way is to drag'n'drop files onto app.\
For this reason a .config file is placed alongside executable file.
A .config file is in XML format. \
***Please do not create any spaces/tabs/uppercase letters/commas as decimal points within setting field. \
Program must run at least once to generate .config file.***\
**WARNING: Do not extract and create archive at the same time!**

### Supported archives

- SARC verrsions 2 and 3
- AAF
- Zlib compressed SARC archives

### CLI parameters

- `-h`\
        Will only show help.
- `<file1> <file2> ... <fileN>`\
        Same as drag'n'drop mode.
- `-a <archive name> <version> <fodler>`\
        Will create `archive name` from a `folder`.\
        Supported `version`s: 2, 3.\
- `-c`\
        Same as `-a` but compresses archive with Zlib.
- `-f`\
        Same as `-a` but compresses archive as an AAF.

### Archive creation

Archives can be created with `-a`, `-c`, `-f` parameters.\
For example: `SmallArchive -a myArchive.ee 2 "/my/path/to/a/folder"`.\
They can be also created when TOC file is dropped on application or it's path provided as parameter.

### TOC file

TOC files can be generated by extracting archives, or by creating manually.\
TOC uses ASCII code page.\
Archive name is determined from a TOC name. For example, a toc with name `archivename.ee.toc` will generate `archivename.ee` archive.\
TOC file structure:

```text
<TOC HEADER>
filepath1
filepath2
...
filepathN
```

Where `filepath` is path to a file relative to a TOC location.

A `filepath` can have `E` suffix (for example: `foo.bar E`), meaning is's an exported file.\
This will mean, that such file won't be added to the archive, but will be referenced for external loading (will be loaded from dropzone or virtual archive).\
Exported file must be still available!

`TOC HEADER`'s structure: `TOC [ L | B ] <version> [U | C | A]`

- `L`, `B`\
        Little or Big Endian, (Only Little is supported for now)
- `U`\
        Uncompressed archive
- `C`\
        Zlib compression
- `A`\
        AAF compression

### Archive types by title

- ***Just Cause 2, the Hunter, the Hunter Primal***\
        Version 2, can be Zlib compressed
- ***Mad Max***\
        Version 2, no compression
- ***Just Cause 3, the Hunter Call of Wild***\
        Version 2, can be AAF compressed
- ***Just Cause 4***\
        Version 3, no compression
- ***Generation Zero***\
        Version 3, can be AAF compressed

### Settings (.config file)

- ***Generate_Log:***\
        Will generate text log of console output next to application location.
- ***Ignore_extensions***\
        Won't add files with those extensions into the archives.
- ***Generate_TOC***\
        Will generate TOC file next to the extracted archive.

## [Latest Release](https://github.com/PredatorCZ/ApexToolset/releases)

## License

This toolset is available under GPL v3 license. (See LICENSE.md)

This toolset uses following libraries:

- ApexLib, Copyright (c) 2014-2019 Lukas Cone
- zlib, Copyright (C) 1995-2017 Jean-loup Gailly and Mark Adler
