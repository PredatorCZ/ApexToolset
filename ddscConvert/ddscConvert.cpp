/*      ddscConvert
        Copyright(C) 2019 Lukas Cone

        This program is free software : you can redistribute it and / or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation, either version 3 of the License, or
        (at your option) any later version.

        This program is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with this program.If not, see <https://www.gnu.org/licenses/>.
*/

#include "AVTX.h"
#include "datas/DirectoryScanner.hpp"
#include "datas/MultiThread.hpp"
#include "datas/SettingsManager.hpp"
#include "datas/binreader.hpp"
#include "datas/fileinfo.hpp"
#include "formats/DDS.hpp"
#include "project.h"
#include "pugixml.hpp"

static struct ddscConvert : SettingsManager {
  DECLARE_REFLECTOR;
  bool Convert_DDS_to_legacy = true;
  bool Force_unconvetional_legacy_formats = true;
  bool Use_HMDDSC = false;
  bool Generate_Log = false;
  bool No_Tiling = true;
  bool Extract_largest_mipmap = false;
  bool Folder_scan_DDSC_only = true;
  int Number_of_ATX_levels = 2;
  int ATX_level0_max_resolution = 256;
  int ATX_level1_max_resolution = 1024;
  int ATX_level2_max_resolution = 2048;
} settings;

REFLECTOR_START_WNAMES(ddscConvert, Convert_DDS_to_legacy,
                       Force_unconvetional_legacy_formats,
                       Extract_largest_mipmap, Folder_scan_DDSC_only,
                       Generate_Log, Number_of_ATX_levels, Use_HMDDSC,
                       ATX_level0_max_resolution, ATX_level1_max_resolution,
                       ATX_level2_max_resolution, No_Tiling);

static int levelResolutions[] = {0x8000, 0x8000, 0x8000, 0x8000, 0x8000};
static const char help[] = "\nConverts between AVTX and DDS formats.\n\
If a DDS is being converted to AVTX, make sure that DDS is properly encoded and have generated full mipmap chain.\n\n\
Settings (.config file):\n\
  Convert_DDS_to_legacy: \n\
        Tries to convert AVTX into legacy (DX9) DDS format.\n\
  Force_unconvetional_legacy_formats:\n\
        Will try to convert some matching formats from DX10 to DX9,\n\
        for example: RG88 to AL88.\n\
  Extract_largest_mipmap:\n\
        Will try to extract only highest mipmap.\n\
        Texture musn't be converted back afterwards, unless you regenerate mipmaps!\n\
        This setting does not apply, if texture have arrays or is a cubemap!\n\
  Folder_scan_DDSC_only:\n\
        When providing input parameter as folder, program will scan only DDSC files.\n\
        When false, program will scan for DDS files only.\n\
  Generate_Log: \n\
        Will generate text log of console output next to application location.\n\n\
Following settings are for AVTX creation:\n\
  Number_of_ATX_levels: \n\
        Number of streamed mipmaps files. \n\
        Titles like JC4 will use 2, Generation Zero uses 3. \n\
        0 means that all mip maps will be stored in one file.\n\
  Use_HMDDSC:\n\
        Use for titles like JC3 or the Hunter COtW. \n\
        It will create one .hmddsc file instead of .atx.\n\
  ATX_levelN_max_resolution: \n\
        Maximum texture resolution for said level. \n\
        Level 0 is main ddsc file, level 1 is atx1 or hmddsc file, \n\
        level 2 is for atx2 and so on.\n\
  No_Tiling: \n\
        Texture should not tile. Should be used for object baked textures.\n\t";

static const char pressKeyCont[] = "\nPress any key to close.";

void FilehandleITFC(const TSTRING &fle) {
  printline("Loading file: ", << fle);
  BinReader rd(fle);

  if (!rd.IsValid()) {
    printerror("Cannot open file.");
    return;
  }

  int ID;
  rd.Read(ID);
  rd.Seek(0);

  if (ID == AVTX::ID) {
    printline("Converting AVTX -> DDS.");
    AVTX tx;
    tx.Load(fle.c_str(), &rd);

    TFileInfo fleInfo = fle;
    std::ofstream ofs(fleInfo.GetPath() + fleInfo.GetFileName() + _T(".dds"),
                      std::ios::out | std::ios::binary);

    DDS tex = {};
    tex = DDSFormat_DX10;
    tex.dxgiFormat = static_cast<DXGI_FORMAT>(tx.format);
    tex.width = tx.width;
    tex.height = tx.height;
    tex.arraySize = tx.numArrayElements;

    bool maxMipOnly = settings.Extract_largest_mipmap;

    if (tx.flags[AVTX::Flag_CubeMap]) {
      tex.caps01 = decltype(tex.caps01)(DDS::Caps01Flags_CubeMap,
                                        DDS::Caps01Flags_CubeMap_NegativeX,
                                        DDS::Caps01Flags_CubeMap_NegativeY,
                                        DDS::Caps01Flags_CubeMap_NegativeZ,
                                        DDS::Caps01Flags_CubeMap_PositiveX,
                                        DDS::Caps01Flags_CubeMap_PositiveY,
                                        DDS::Caps01Flags_CubeMap_PositiveZ);

      if (maxMipOnly) {
        maxMipOnly = false;
        printwarning("Cubemap detected, Extract_largest_mipmap ignored.")
      }
    }

    if (maxMipOnly && tx.numArrayElements > 1) {
      maxMipOnly = false;
      printwarning("Texture uses arrays, Extract_largest_mipmap ignored.")
    }

    tex.NumMipmaps(maxMipOnly ? 1 : tx.mipCount);

    const int sizetoWrite =
        !settings.Convert_DDS_to_legacy || tex.arraySize > 1 ||
                tex.ToLegacy(settings.Force_unconvetional_legacy_formats)
            ? tex.DDS_SIZE
            : tex.LEGACY_SIZE;

    if (settings.Convert_DDS_to_legacy && sizetoWrite == tex.DDS_SIZE) {
      printwarning("Couldn't convert DX10 dds to legacy.")
    }

    ofs.write(reinterpret_cast<const char *>(&tex), sizetoWrite);

    if (tx.flags[AVTX::Flag_CubeMap] && tx.mipCount > 1) {
      DDS::Mips cubes[6] = {};
      tex.ComputeBPP();

      int bufferSize = tex.ComputeBufferSize(*cubes) * tex.arraySize;

      if (!bufferSize) {
        printerror("Unsupported DDS format.");
        return;
      }

      for (int c = 1; c < 6; c++)
        memcpy_s(&cubes[c], sizeof(DDS::Mips), cubes, sizeof(DDS::Mips));

      int currentMipOffset = 0;

      for (int m = 0; m < tx.mipCount; m++) {
        const int currentMipSize = cubes[0].sizes[m];

        for (int c = 0; c < 6; c++) {
          cubes[c].offsets[m] = currentMipOffset + (currentMipSize * c);
        }

        currentMipOffset = cubes[5].offsets[m] + currentMipSize;
      }

      for (int c = 0; c < 6; c++)
        for (int m = 0; m < tx.mipCount; m++)
          ofs.write(tx.buffer + cubes[c].offsets[m], cubes[c].sizes[m]);

    } else {
      int oBufferSize = tx.BufferSize();

      if (maxMipOnly) {
        DDS::Mips rMips = {};
        tex.ComputeBufferSize(rMips);

        if (rMips.sizes[0])
          oBufferSize = rMips.sizes[0];
      }

      ofs.write(tx.buffer, oBufferSize);
    }

    ofs.close();
  } else if (ID == DDS::ID) {
    printline("Converting DDS -> AVTX.");
    DDS tex = {};
    rd.ReadBuffer(reinterpret_cast<char *>(&tex), tex.LEGACY_SIZE);

    if (tex.caps01[DDS::Caps01Flags_Volume]) {
      printerror("Volumetric DDS textures are not supported.");
      return;
    }

    if (tex.caps01[DDS::Caps01Flags_CubeMap]) {
      if (!tex.caps01[DDS::Caps01Flags_CubeMap_NegativeX] &&
          tex.caps01[DDS::Caps01Flags_CubeMap_NegativeY] &&
          tex.caps01[DDS::Caps01Flags_CubeMap_NegativeZ] &&
          tex.caps01[DDS::Caps01Flags_CubeMap_PositiveX] &&
          tex.caps01[DDS::Caps01Flags_CubeMap_PositiveY] &&
          tex.caps01[DDS::Caps01Flags_CubeMap_PositiveZ]) {
        printerror("Cubemap DDS must have all sides.");
        return;
      }
    }

    if (tex.fourCC != DDSFormat_DX10.fourCC) {
      if (tex.FromLegacy()) {
        printerror("DDS file cannot be converted to DX10!");
        return;
      }
    } else {
      rd.Read(static_cast<DDS_HeaderDX10 &>(tex));
    }

    if (tex.mipMapCount < 2) {
      printerror("DDS file must have generated mipmaps.");
      return;
    }

    DDS::Mips dMips = {};
    int mipIDs[DDS::Mips::maxMips] = {};
    tex.ComputeBPP();
    const int bufferSize = tex.ComputeBufferSize(dMips) * tex.arraySize *
                           (tex.caps01[DDS::Caps01Flags_CubeMap] ? 6 : 1);

    if (!bufferSize) {
      printerror("Usupported DDS format.");
      return;
    }

    char *masterBuffer = static_cast<char *>(malloc(bufferSize));
    rd.ReadBuffer(masterBuffer, bufferSize);

    AVTX tx;
    tx.flags(AVTX::Flag_ExternalBuffers,
             tex.arraySize == 1 && settings.Number_of_ATX_levels > 0 &&
                 !tex.caps01[DDS::Caps01Flags_CubeMap]);
    tx.flags(AVTX::Flag_NoTiling, settings.No_Tiling);
    tx.flags(AVTX::Flag_CubeMap, tex.caps01[DDS::Caps01Flags_CubeMap]);
    tx.numArrayElements = tex.arraySize;
    tx.width = tex.width;
    tx.height = tex.height;
    tx.format = tex.dxgiFormat;
    tx.mipCount = tex.mipMapCount;
    tx.entries[0].offset = 128;
    tx.entries[0].flags += AVTX::Entry::Flag_Used;
    tx.entries[0].size =
        tex.arraySize > 1 || tex.caps01[DDS::Caps01Flags_CubeMap] ? bufferSize
                                                                  : 0;

    if (!tx.flags[AVTX::Flag_ExternalBuffers])
      tx.headerMipCount = tx.mipCount;

    TFileInfo fleInfo = fle;
    TSTRING masterFileName =
        fleInfo.GetPath() + fleInfo.GetFileName() + _T(".ddsc");
    std::ofstream ddscStream(masterFileName, std::ios::out | std::ios::binary);
    ddscStream.write(reinterpret_cast<char *>(&tx), sizeof(AVTX));

    if (!tx.flags[AVTX::Flag_ExternalBuffers] &&
        !tex.caps01[DDS::Caps01Flags_CubeMap]) {
      ddscStream.write(masterBuffer, bufferSize);
      ddscStream.close();
      return;
    }

    if (tex.caps01[DDS::Caps01Flags_CubeMap]) {
      DDS::Mips cubes[6] = {};
      const int sideSize = bufferSize / 6;

      for (int c = 0; c < 6; c++) {
        memcpy_s(&cubes[c], sizeof(DDS::Mips), &dMips, sizeof(DDS::Mips));

        for (int m = 0; m < tx.mipCount; m++)
          cubes[c].offsets[m] += sideSize * c;
      }

      for (int m = 0; m < tx.mipCount; m++)
        for (int c = 0; c < 6; c++)
          ddscStream.write(masterBuffer + cubes[c].offsets[m],
                           cubes[c].sizes[m]);

      ddscStream.close();
      return;
    }

    int _weight = 1, _height = 1, currentLevel = 0, externalMipID = 0,
        mipOffsetWithinLevel = 0;

    for (int m = tex.mipMapCount - 1; m >= 0; m--) {
      mipIDs[m] = currentLevel;
      if (!currentLevel) {
        tx.entries[0].size += dMips.sizes[m];
        tx.headerMipCount++;
      } else {
        tx.entries[externalMipID].externalID = currentLevel;
        tx.entries[externalMipID].flags += AVTX::Entry::Flag_Used;
        tx.entries[externalMipID].offset = mipOffsetWithinLevel;
        tx.entries[externalMipID].size = dMips.sizes[m];
        mipOffsetWithinLevel += dMips.sizes[m];
        externalMipID++;
      }

      _weight *= 2;
      _height *= 2;

      if ((_weight | _height) > levelResolutions[currentLevel]) {
        if (!currentLevel)
          externalMipID++;

        currentLevel++;
        mipOffsetWithinLevel = 0;
      }
    }

    std::ofstream ofs;
    currentLevel = 0;

    for (int m = tex.mipMapCount - 1; m >= 0; m--) {
      if (currentLevel != mipIDs[m]) {
        currentLevel = mipIDs[m];

        TSTRING fileName = fleInfo.GetPath() + fleInfo.GetFileName();

        if (settings.Use_HMDDSC)
          fileName.append(_T(".hmddsc"));
        else
          fileName.append(_T(".atx")) += ToTSTRING(mipIDs[m]);

        ofs.close();
        ofs.clear();
        ofs.open(fileName, std::ios::out | std::ios::binary);
      }

      if (!currentLevel)
        continue;

      ofs.write(masterBuffer + dMips.offsets[m], dMips.sizes[m]);
    }

    ofs.close();
    ofs.clear();
    ddscStream.write(masterBuffer + (bufferSize - tx.entries[0].size),
                     tx.entries[0].size);
    ddscStream.close();
  } else {
    printerror("Invalid file format.")
  }
}

struct TexQueueTraits {
  int queue;
  int queueEnd;
  TCHAR **files;
  typedef void return_type;

  return_type RetreiveItem() {
    TSTRING filepath = files[queue];

    if (filepath.find('.') == filepath.npos)
      return;

    FilehandleITFC(filepath);
  }

  operator bool() { return queue < queueEnd; }
  void operator++(int) { queue++; }
  int NumQueues() const { return queueEnd - 1; }
};

struct TexFolderQueueTraits {
  int queue = 0;
  int queueEnd;
  DirectoryScanner ds;
  typedef void return_type;

  return_type RetreiveItem() {
    const TSTRING &filepath = ds.Files()[queue];
    FilehandleITFC(filepath);
  }

  operator bool() { return queue < queueEnd; }
  void operator++(int) { queue++; }
  int NumQueues() const { return queueEnd; }
};

int _tmain(int argc, _TCHAR *argv[]) {
  setlocale(LC_ALL, "");
  printer.AddPrinterFunction(wprintf);

  printline(ddscConvert_DESC " V" ddscConvert_VERSION "\n" ddscConvert_COPYRIGHT
                             "\nSimply drag'n'drop files/folders into "
                             "application or use as " ddscConvert_PRODUCT_NAME
                             " path1 path2 ...\n");

  if (argc < 2) {
    printerror("Insufficient argument count, expected at least 1.\n");
    printer << help << pressKeyCont >> 1;
    getchar();
    return 1;
  }

  if (argv[1][1] == '?' || argv[1][1] == 'h') {
    printer << help << pressKeyCont >> 1;
    getchar();
    return 0;
  }

  TFileInfo configInfo(*argv);
  const TSTRING configName =
      configInfo.GetPath() + configInfo.GetFileName() + _T(".config");

  settings.FromXML(configName);

  pugi::xml_document doc = {};
  pugi::xml_node mainNode(settings.ToXML(doc));
  mainNode.prepend_child(pugi::xml_node_type::node_comment).set_value(help);

  doc.save_file(configName.c_str(), "\t",
                pugi::format_write_bom | pugi::format_indent);

  if (settings.Generate_Log)
    settings.CreateLog(configInfo.GetPath() + configInfo.GetFileName());

  if (settings.Number_of_ATX_levels > 3 || settings.Number_of_ATX_levels < 0) {
    int temp = settings.Number_of_ATX_levels;
    settings.Number_of_ATX_levels = settings.Number_of_ATX_levels > 3 ? 3 : 0;

    printwarning("Number_of_ATX_levels: Unexpected value ",
                 << temp << _T(", clamping to ")
                 << settings.Number_of_ATX_levels);
  }

  if (settings.Use_HMDDSC && settings.Number_of_ATX_levels > 1)
    settings.Number_of_ATX_levels = 1;

  for (int l = 0; l < settings.Number_of_ATX_levels; l++) {
    levelResolutions[l] = *(&settings.ATX_level0_max_resolution + l);
  }

  const int nthreads = std::thread::hardware_concurrency();
  std::vector<std::thread> threadedfuncs(nthreads);

  printer.PrintThreadID(true);

  TexQueueTraits texQue;
  texQue.files = argv;
  texQue.queue = 1;
  texQue.queueEnd = argc;

  RunThreadedQueue(texQue);

  std::vector<const TCHAR *> folders;

  for (int a = 1; a < argc; a++) {
    const TCHAR *curItem = argv[a];

    while (*curItem) {
      if (*curItem == '.')
        break;

      curItem++;
    }

    if (!*curItem)
      folders.push_back(argv[a]);
  }

  if (folders.size()) {
    printer.PrintThreadID(false);
    printline("Scanning folders for ",
              << (settings.Folder_scan_DDSC_only ? "DDSC" : "DDS") << " files.");

    TexFolderQueueTraits flQue;
    flQue.ds.AddFilter(settings.Folder_scan_DDSC_only ? _T(".ddsc") : _T(".dds"));

    size_t lastFileCount = 0;

    for (auto &f : folders) {
      printline("Scanning: ", << f);
      flQue.ds.Scan(f);
      printline("Files found: ", << flQue.ds.Files().size() - lastFileCount);
      lastFileCount = flQue.ds.Files().size();
    }

    printline("Scanning done, total files found: ", << lastFileCount);
    flQue.queueEnd = static_cast<int>(lastFileCount);
    printer.PrintThreadID(true);
    RunThreadedQueue(flQue);
  }

  return 0;
}