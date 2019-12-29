/*      R2SmallArchive
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

#include "datas/MultiThread.hpp"
#include "datas/SettingsManager.hpp"
#include "datas/binreader.hpp"
#include "datas/fileinfo.hpp"
#include "project.h"
#include "pugixml.hpp"

static struct R2SmallArchive : SettingsManager {
  DECLARE_REFLECTOR;
  bool Generate_Log = false;
  std::string sarc0_gtoc_file_path = "Path into sarc.0.gtoc",
              expentities_gtoc_file_path = "Path into expentities.gtoc";
} settings;

REFLECTOR_START_WNAMES(R2SmallArchive, sarc0_gtoc_file_path,
                       expentities_gtoc_file_path, Generate_Log);

struct GTOCFile {
  uint hash1, hash2;
  int fileSize;
  char fileName[1];
};

struct GTOCFileEntry {
  int fileEntryOffset, fileOffset;

  const GTOCFile *Entry() const {
    return reinterpret_cast<const GTOCFile *>(
        reinterpret_cast<const char *>(this) + fileEntryOffset);
  }
};

struct GTOCEntry {
  uint hash1, hash2;
  int numFiles;

  const GTOCFileEntry *Files() const {
    return reinterpret_cast<const GTOCFileEntry *>(this + 1);
  }

  void mkdirs(const TSTRING &inFilepath) const {
    for (int f = 0; f < numFiles; f++) {
      const char *cfle = Files()[f].Entry()->fileName;
      TSTRING filePath = esStringConvert<TCHAR>(cfle);

      for (size_t s = 0; s < filePath.length(); s++)
        if (filePath[s] == '\\' || filePath[s] == '/') {
          TSTRING genpath = inFilepath;
          genpath.append(filePath.substr(0, s));
          _tmkdir(genpath.c_str());
        }
    }
  }
};

struct GTOC {
  typedef std::unique_ptr<GTOC> Ptr;
  static const int gtocID = CompileFourCC("GT0C");

  char *masterBuffer;
  std::vector<GTOCEntry *> archives;

  GTOC() : masterBuffer(nullptr) {}
  ~GTOC() {
    if (masterBuffer)
      free(masterBuffer);
  }

  int Load(BinReader *rd) {
    int ID;
    rd->Read(ID);

    if (ID != gtocID) {
      printerror("Invalid file format, expected gtoc.");
      return 1;
    }

    int numArchives;
    rd->Read(numArchives);
    archives.resize(numArchives);
    const size_t fleSize = rd->GetSize() - 8;
    masterBuffer = static_cast<char *>(malloc(fleSize));
    rd->ReadBuffer(masterBuffer, fleSize);
    char *masterCopy = masterBuffer;

    for (auto &a : archives) {
      a = reinterpret_cast<GTOCEntry *>(masterCopy);
      masterCopy += sizeof(GTOCEntry) + a->numFiles * sizeof(GTOCFileEntry);
    }

    return 0;
  }

  GTOCEntry *FindEntry(uint hash) const {
    for (auto &a : archives)
      if (a->hash2 == hash)
        return a;

    return nullptr;
  }
};

GTOC::Ptr LoadGTOC(const std::string &path) {
  TSTRING filepath = esStringConvert<TCHAR>(path.c_str());
  BinReader rd(filepath);

  if (!rd.IsValid()) {
    printerror("Cannot open gtoc file.");
    return nullptr;
  }

  GTOC *gtoc = new GTOC();
  if (gtoc->Load(&rd)) {
    delete gtoc;
    return nullptr;
  }

  return GTOC::Ptr(gtoc);
}

void FilehandleITFC(const TCHAR *fle, GTOC::Ptr *globalTOC) {
  printline("Loading file: ", << fle);

  TSTRING filepath = fle;
  BinReader rd(fle);

  if (!rd.IsValid()) {
    printerror("Cannot open file.");
    return;
  }

  const size_t sarcSize = rd.GetSize();
  char *dataBuffer = static_cast<char *>(malloc(sarcSize));
  rd.ReadBuffer(dataBuffer, sarcSize);

  const GTOCEntry *cEntry =
      (*globalTOC)->FindEntry(*reinterpret_cast<uint *>(dataBuffer));

  if (!cEntry)
    cEntry =
        (*(globalTOC + 1))->FindEntry(*reinterpret_cast<uint *>(dataBuffer));

  if (!cEntry) {
    printerror("Cannot find file in global table.");
    return;
  }

  TFileInfo finfo(filepath);
  cEntry->mkdirs(finfo.GetPath());
  const int numFiles = cEntry->numFiles;

  for (int f = 0; f < numFiles; f++) {
    const GTOCFileEntry &cFile = cEntry->Files()[f];
    const GTOCFile *cFileName = cFile.Entry();

    if (cFileName->fileSize < 1 || cFile.fileOffset < 4)
      continue;

    TSTRING cFilePath =
        finfo.GetPath() + esStringConvert<TCHAR>(cFileName->fileName);
    std::ofstream fileOut(cFilePath,
                          std::ios_base::binary | std::ios_base::out);

    if (fileOut.fail()) {
      printerror("Couldn't create file: ", << cFilePath);
      continue;
    }

    fileOut.write(dataBuffer + cFile.fileOffset, cFileName->fileSize);
    fileOut.close();
  }

  free(dataBuffer);

  printer << numFiles << " files extracted." >> 1;
  return;
}

struct SarcQueueTraits {
  int queue;
  int queueEnd;
  TCHAR **files;
  GTOC::Ptr *mainGTOC;
  typedef void return_type;

  return_type RetreiveItem() { FilehandleITFC(files[queue], mainGTOC); }

  operator bool() { return queue < queueEnd; }
  void operator++(int) { queue++; }
  int NumQueues() const { return queueEnd - 1; }
};

int _tmain(int argc, _TCHAR *argv[]) {
  setlocale(LC_ALL, "");
  printer.AddPrinterFunction(wprintf);

  printline(R2SmallArchive_DESC
            " V" R2SmallArchive_VERSION "\n" R2SmallArchive_COPYRIGHT
            "\nSimply drag'n'drop files into "
            "application or use as " R2SmallArchive_PRODUCT_NAME
            " file1 file2 ...\n");

  TFileInfo configInfo(*argv);
  const TSTRING configName =
      configInfo.GetPath() + configInfo.GetFileName() + _T(".config");

  settings.FromXML(configName);

  pugi::xml_document doc = {};
  settings.ToXML(doc);
  doc.save_file(configName.c_str(), "\t",
                pugi::format_write_bom | pugi::format_indent);

  if (argc < 2) {
    printerror("Insufficient argument count, expected at least 1.\n");
    return 1;
  }

  if (settings.Generate_Log)
    settings.CreateLog(configInfo.GetPath() + configInfo.GetFileName());

  GTOC::Ptr mainGTOC[2] = {LoadGTOC(settings.sarc0_gtoc_file_path),
                           LoadGTOC(settings.expentities_gtoc_file_path)};

  for (auto &t : mainGTOC)
    if (!t)
      return 2;

  SarcQueueTraits sarQue;
  sarQue.files = argv;
  sarQue.queue = 1;
  sarQue.queueEnd = argc;
  sarQue.mainGTOC = mainGTOC;

  printer.PrintThreadID(true);
  RunThreadedQueue(sarQue);

  return 0;
}