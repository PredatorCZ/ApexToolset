/*      SmallArchive
        Copyright(C) 2017-2019 Lukas Cone

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

#include "datas/DirectoryScanner.hpp"
#include "datas/MasterPrinter.hpp"
#include "datas/MultiThread.hpp"
#include "datas/SettingsManager.hpp"
#include "datas/binreader.hpp"
#include "datas/binwritter.hpp"
#include "datas/esString.h"
#include "datas/fileinfo.hpp"
#include "lookup3.h"
#include "project.h"
#include "pugixml.hpp"
#include "zlib.h"

static struct SmallArchive : SettingsManager {
  DECLARE_REFLECTOR;
  bool Generate_Log = false;
  bool Generate_TOC = true;
  std::string Ignore_extensions = ".hmddsc;.atx1;.atx2;.atx3;.ee;.eez;.bl;.blz;.fl;.flz;.nl;.nlz;.sarc;.toc";

  std::vector<TSTRING> _ignoredExts;

  void Process() {
    size_t curOffset = 0;
    size_t lastOffset = 0;

    do {
      curOffset = Ignore_extensions.find(';', lastOffset);
      std::string sub(Ignore_extensions.cbegin() + lastOffset,
                      curOffset == std::string::npos
                          ? Ignore_extensions.cend()
                          : Ignore_extensions.cbegin() + curOffset);
      _ignoredExts.emplace_back(esString(sub));
      lastOffset = curOffset + 1;
    } while (curOffset != std::string::npos);
  }

  bool IsExcluded(const TSTRING &input) {
    for (auto &e : _ignoredExts)
      if (input.find(e) != input.npos)
        return true;

    return false;
  }

} settings;

REFLECTOR_START_WNAMES(SmallArchive, Generate_Log, Generate_TOC,
                       Ignore_extensions);

static const char help[] = "\nWill extract/create SARC/AAF archives.\n\n\
Settings (.config file):\n\
    Generate_Log: \n\
        Will generate text log of console output next to application location.\n\
    Ignore_extensions:\n\
        Won't add files with those extensions into the archives.\n\
    Generate_TOC: \n\
        Will generate TOC file next to the extracted archive.\n\n\
CLI Parameters:\n\
    -h  Will show help.\n\
    -a <archive name> <version> <folder>\n\
        Will create SARC archive.\n\
        Supported versions: 2, 3\n\
    -c  Same as -a, but compresses archive.\n\
    -f  Same as -a, but compresses archive as an AAF.\n\t";

static const char pressKeyCont[] = "\nPress any key to close.";

struct SARCFileEntry {
  std::string fileName;
  int offset;
  int length;

  SARCFileEntry() = default;
  SARCFileEntry(const std::string str, int fSize)
      : fileName(str), length(fSize) {}

  void Load(BinReader *rd) {
    rd->ReadContainer(fileName);
    rd->Read(offset);
    rd->Read(length);
  }

  void Write(BinWritter *bw) const {
    uint allignment = fileName.size() & 0x3;

    if (allignment)
      allignment = 4 - allignment;

    bw->Write(allignment + fileName.size());
    bw->WriteContainer(fileName);
    bw->Skip(allignment);
    bw->Write(offset);
    bw->Write(length);
  }
};

struct _SARC3FileEntry {
  int fileNameOffset;
  int offset;
  int length;
  int fileNameHash, hash02;
};

struct SARC3FileEntry : _SARC3FileEntry {
  std::string fileName;

  void Load(BinReader *rd, const char *masterBuffer) {
    rd->Read(static_cast<_SARC3FileEntry &>(*this));
    fileName = masterBuffer + fileNameOffset;
  }

  void Write(BinWritter *wr) const {
    wr->Write(static_cast<const _SARC3FileEntry &>(*this));
  }
};

struct SARC {
  static constexpr int ID = CompileFourCC("SARC");

  enum CompressionType { C_NONE, C_ZLIB, C_AAF };

  virtual int Load(BinReader *rd) = 0;
  virtual void Write(BinWritter *wr) = 0;
  virtual void AddFileEntry(const std::string &filePath, int fileSize,
                            bool external) = 0;
  virtual void ExtractFiles(BinReader *rd, const TSTRING &inFilepath,
                            CompressionType compType) = 0;
  virtual void mkdirs(const TSTRING &inFilepath) = 0;
  virtual int GetVersion() const = 0;
};

template <class C> struct SARC_t : SARC {
  std::vector<C> files;

  void ExtractFiles(BinReader *rd, const TSTRING &inFile,
                    CompressionType compType) {
    TFileInfo fInf(inFile);
    TSTRING inFilepath = fInf.GetPath();

    printline("Generating folder structure.");
    mkdirs(inFilepath);
    printline("Extracting files.");

    std::ofstream tocFile;

    if (settings.Generate_TOC) {
      auto tocFileName = inFile + _T(".toc");
      tocFile.open(tocFileName);

      if (tocFile.fail()) {
        printerror("Cannot create: ", << tocFileName);
      } else {
        tocFile << "TOCL" << GetVersion();

        switch (compType) {
        case C_NONE:
          tocFile << 'U';
          break;
        case C_ZLIB:
          tocFile << 'C';
          break;
        case C_AAF:
          tocFile << 'A';
          break;
        }

        tocFile << std::endl;
      }
    }

    for (auto &f : files) {
      if (settings.Generate_TOC && !tocFile.fail()) {
        tocFile << f.fileName.c_str();

        if (!f.offset)
          tocFile << " E";

        tocFile << std::endl;
      }

      if (f.offset > 0) {
        TSTRING genpath = inFilepath;
        genpath.append(esString(f.fileName));
        std::ofstream result =
            std::ofstream(genpath, std::ios::out | std::ios::binary);

        if (result.fail())
          continue;

        if (f.offset > 0) {
          rd->Seek(f.offset);
          char *tmp = static_cast<char *>(malloc(f.length));
          rd->ReadBuffer(tmp, f.length);
          result.write(tmp, f.length);
          free(tmp);
        }
        result.close();
      }
    }

    tocFile.close();
  }

  void mkdirs(const TSTRING &inFilepath) {
    for (auto &f : files) {
      std::string cfle = f.fileName;
      for (size_t s = 0; s < cfle.length(); s++)
        if (cfle[s] == '\\' || cfle[s] == '/') {
          TSTRING genpath = inFilepath;
          genpath.append(esString(cfle.substr(0, s)));
          _tmkdir(genpath.c_str());
        }
    }
  }
};

struct SARC2 : SARC_t<SARCFileEntry> {
  struct {
    int hlen;
    int hid;
    int version;
    uint tocSize;
  } header;

  SARC2() : header{4, ID, 2} {}

  void AddFileEntry(const std::string &filePath, int fileSize,
                    bool external) override {
    files.emplace_back(filePath, fileSize);
    files[files.size() - 1].offset = external ? -1 : 0;
  }

  int GetVersion() const override { return 2; }

  int Load(BinReader *rd) override {
    rd->Read(header);

    if (header.hlen != 4 || header.hid != ID)
      return 1;

    if (header.version > 2)
      return 2;

    const uint tocEnd = header.tocSize + sizeof(header);

    while (rd->Tell() < tocEnd) {
      SARCFileEntry cf;
      cf.Load(rd);

      if (!cf.fileName.size())
        break;

      files.push_back(cf);
    }

    return 0;
  }

  void Write(BinWritter *bw) override {
    const size_t begin = bw->Tell();

    bw->Write(header);

    const size_t tocStart = bw->Tell();

    for (auto f : files)
      f.Write(bw);

    bw->ApplyPadding();

    size_t lastOffset = bw->Tell();

    header.tocSize = lastOffset - tocStart;

    bw->Seek(begin);
    bw->Write(header);

    for (auto f : files) {
      if (f.offset < 0) {
        f.offset = 0;
        continue;
      }

      f.offset = lastOffset;

      uint allignment = f.length & 0xF;

      if (allignment)
        allignment = 0x10 - allignment;

      lastOffset += allignment + f.length;
      f.Write(bw);
    }

    bw->ApplyPadding();
  }
};

struct SARC3 : SARC_t<SARC3FileEntry> {
  struct {
    int hlen;
    int hid;
    int version;
    uint dataOffset;
    uint bufferLen;
  } header;

  std::string nameBuffer;

  SARC3() : header{4, ID, 3} {}

  void AddFileEntry(const std::string &filePath, int fileSize,
                    bool external) override {
    SARC3FileEntry nEntry;

    nEntry.fileNameOffset = nameBuffer.size();
    nameBuffer.append(filePath).push_back(0);
    nEntry.fileNameHash = JenkinsLookup3(filePath.c_str());
    nEntry.hash02 = 0;
    nEntry.length = fileSize;
    nEntry.offset = external ? -1 : 0;

    files.push_back(nEntry);
  }

  int GetVersion() const override { return 3; }

  int Load(BinReader *rd) override {
    rd->Read(header);

    if (header.hlen != 4 || header.hid != ID)
      return 1;

    if (header.version != 3)
      return 2;

    nameBuffer.resize(header.bufferLen);
    rd->ReadContainer(nameBuffer, header.bufferLen);

    while (rd->Tell() + sizeof(_SARC3FileEntry) <= header.dataOffset) {
      SARC3FileEntry cf;
      cf.Load(rd, nameBuffer.c_str());
      files.push_back(cf);
    }

    return 0;
  }

  void Write(BinWritter *wr) override {
    const size_t begin = wr->Tell();

    wr->Write(header);
    wr->WriteContainer(nameBuffer);
    wr->ApplyPadding(4);
    header.bufferLen = wr->Tell() - begin - sizeof(header);

    const size_t TOCbegin = wr->Tell();

    for (auto &f : files)
      f.Write(wr);

    wr->ApplyPadding();
    header.dataOffset = wr->Tell();

    wr->Seek(begin);
    wr->Write(header);
    wr->Seek(TOCbegin);

    for (auto &f : files) {
      if (f.offset < 0) {
        f.offset = 0;
        continue;
      }

      f.offset = header.dataOffset;
      uint allignment = f.length & 0xF;

      if (allignment)
        allignment = 0x10 - allignment;

      header.dataOffset += allignment + f.length;
      f.Write(wr);
    }
  }
};

struct EWAM {
  static constexpr int ID = CompileFourCC("EWAM");

  struct {
    int compressedSize;
    int uncompressedSize;
    int nextBlock;
    int id;
  } header;
  char *intermediateData;

  int Load(BinReader *rd) {
    rd->Read(header);

    if (header.id != ID)
      return 1;

    char *compressedStream = static_cast<char *>(malloc(header.compressedSize));
    intermediateData = static_cast<char *>(malloc(header.uncompressedSize));
    rd->ReadBuffer(compressedStream, header.compressedSize);

    z_stream infstream;
    infstream.zalloc = Z_NULL;
    infstream.zfree = Z_NULL;
    infstream.opaque = Z_NULL;
    infstream.avail_in = header.compressedSize;
    infstream.next_in = reinterpret_cast<Bytef *>(compressedStream);
    infstream.avail_out = header.uncompressedSize;
    infstream.next_out = reinterpret_cast<Bytef *>(intermediateData);
    inflateInit2(&infstream, -MAX_WBITS);
    int state = inflate(&infstream, Z_FINISH);
    inflateEnd(&infstream);
    free(compressedStream);

    if (state != Z_STREAM_END) {
      printerror("[ZLIB] Expected Z_STREAM_END.");
      return 2;
    }

    return 0;
  }

  int Write(BinWritter *wr) {
    char *compressedStream =
        static_cast<char *>(malloc(header.uncompressedSize));
    z_stream infstream;
    infstream.zalloc = Z_NULL;
    infstream.zfree = Z_NULL;
    infstream.opaque = Z_NULL;
    infstream.avail_in = header.uncompressedSize;
    infstream.next_in = reinterpret_cast<Bytef *>(intermediateData);
    infstream.avail_out = header.uncompressedSize;
    infstream.next_out = reinterpret_cast<Bytef *>(compressedStream);

    deflateInit2(&infstream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                 Z_DEFAULT_STRATEGY);
    int state = deflate(&infstream, Z_FINISH);
    deflateEnd(&infstream);

    if (state != Z_STREAM_END) {
      printerror("[ZLIB] Expected Z_STREAM_END.");
      return 2;
    }

    header.compressedSize = infstream.total_out;

    const size_t begin = wr->Tell();

    wr->Write(header);
    wr->WriteBuffer(compressedStream, header.compressedSize);
    wr->ApplyPadding();
    header.nextBlock = wr->Tell() - begin;

    const size_t end = wr->Tell();

    wr->Seek(begin);
    wr->Write(header);
    wr->Seek(end);
    free(compressedStream);

    return 0;
  }

  EWAM() : intermediateData(nullptr), header{0, 0, 0, ID} {}

  ~EWAM() {
    if (intermediateData)
      free(intermediateData);
  }
};

struct AAF {
  static constexpr int ID = CompileFourCC("AAF\0");
  static constexpr int ID2[] = {CompileFourCC("AVAL"), CompileFourCC("ANCH"),
                                CompileFourCC("EARC"), CompileFourCC("HIVE"),
                                CompileFourCC("FORM"), CompileFourCC("ATIS"),
                                CompileFourCC("COOL")};
  static constexpr int MAX_BLOCK_SIZE = 0x2000000;

  struct Header {
    int id;
    int version;
    int id2[7];
    int uncompressedSize;
    int blockSize;
    int blockCount;

    Header()
        : id(ID), version(1), id2{ID2[0], ID2[1], ID2[2], ID2[3],
                                  ID2[4], ID2[5], ID2[6]} {}
  } header;

  std::vector<std::unique_ptr<EWAM>> blocks;

  int Load(BinReader *rd) {
    rd->Read(header);

    if (header.id != ID)
      return 1;

    if (memcmp(header.id2, ID2, sizeof(ID2)))
      return 2;

    for (int b = 0; b < header.blockCount; b++) {
      EWAM *cb = new EWAM;
      size_t cpos = rd->Tell();
      int rtval = cb->Load(rd);

      if (rtval)
        return 3;

      blocks.emplace_back(cb);
      rd->Seek(cpos + cb->header.nextBlock);
    }

    return 0;
  }

  int Write(BinWritter *wr, char *buffer, size_t buffSize) {
    header.blockCount = buffSize / MAX_BLOCK_SIZE;
    header.uncompressedSize = buffSize;
    const size_t lastBlockSize = buffSize % MAX_BLOCK_SIZE;

    if (lastBlockSize)
      header.blockCount++;

    if (header.blockCount > 1)
      header.blockSize = MAX_BLOCK_SIZE;
    else
      header.blockSize = lastBlockSize;

    wr->Write(header);

    for (int b = 1; b < header.blockCount; b++) {
      EWAM ew;
      ew.intermediateData = buffer + (b - 1) * MAX_BLOCK_SIZE;
      ew.header.uncompressedSize = MAX_BLOCK_SIZE;

      if (ew.Write(wr))
        return 1;

      ew.intermediateData = nullptr;
    }

    EWAM ew;
    ew.intermediateData = buffer + (header.blockCount - 1) * MAX_BLOCK_SIZE;
    ew.header.uncompressedSize = lastBlockSize;

    if (ew.Write(wr))
      return 1;

    ew.intermediateData = nullptr;

    return 0;
  }

  void GetNewStream(std::stringstream *str) {
    for (auto &b : blocks)
      str->write(b->intermediateData, b->header.uncompressedSize);
  }
};

int CompressArchive(BinWritter *wr, char *buffer, size_t bufferSize) {
  std::string compressedStream;
  compressedStream.resize(bufferSize);
  z_stream infstream;
  infstream.zalloc = Z_NULL;
  infstream.zfree = Z_NULL;
  infstream.opaque = Z_NULL;
  infstream.avail_in = bufferSize;
  infstream.next_in = reinterpret_cast<Bytef *>(buffer);
  infstream.avail_out = bufferSize;
  infstream.next_out = reinterpret_cast<Bytef *>(&compressedStream[0]);

  deflateInit2(&infstream, Z_BEST_COMPRESSION, Z_DEFLATED, MAX_WBITS, 8,
               Z_DEFAULT_STRATEGY);
  int state = deflate(&infstream, Z_FINISH);
  deflateEnd(&infstream);

  if (state != Z_STREAM_END) {
    printerror("[ZLIB] Expected Z_STREAM_END.");
    return 1;
  }

  compressedStream.resize(infstream.total_out);
  wr->WriteContainer(compressedStream);

  return 0;
}

struct SARCPacker {
  enum SARCVersion { V2 = 2, V3 = 3 };

  void Create(BinWritter &out, const DirectoryScanner::storage_type &files,
              SARCVersion ver, const TSTRING &dir) {
    std::unique_ptr<SARC> sarcInstance;

    if (ver == V2)
      sarcInstance = std::unique_ptr<SARC>(new SARC2());
    else
      sarcInstance = std::unique_ptr<SARC>(new SARC3());

    size_t maxSize = 0;

    for (auto &f : files) {
      bool external = false;

      if (f[f.size() - 2] == ' ' && f[f.size() - 1] == 'E') {
        external = true;
      }

      TSTRING cFleName(f.cbegin(), f.cend() - (external ? 2 : 0));

      if (settings.IsExcluded(cFleName))
        continue;

      BinReader rd(cFleName);

      if (!rd.IsValid()) {
        printerror("Cannot open: ", << cFleName);
        continue;
      }

      const size_t fleSize = rd.GetSize();

      if (fleSize > maxSize)
        maxSize = fleSize;

      const TCHAR lastDirChar = *std::prev(dir.end());
      const int additionalDirSize =
          lastDirChar == '/' || lastDirChar == '\\' ? 0 : 1;

      std::string localFilePath =
          esString(cFleName.substr(dir.size() + additionalDirSize));

      sarcInstance->AddFileEntry(localFilePath, fleSize, external);
    }

    sarcInstance->Write(&out);
    std::string tempBuffer;
    tempBuffer.resize(maxSize);

    for (auto &f : files) {
      if ((f[f.size() - 2] == ' ' && f[f.size() - 1] == 'E') ||
          settings.IsExcluded(f))
        continue;

      BinReader rd(f);

      if (!rd.IsValid())
        continue;

      rd.ReadContainer(tempBuffer, rd.GetSize());
      out.ApplyPadding();
      out.WriteContainer(tempBuffer);
    }
  }

  void Scan(BinWritter &out, const TSTRING &dir, SARCVersion ver) {
    DirectoryScanner ds;
    ds.Scan(dir);
    Create(out, ds.Files(), ver, dir);
  }

  int FromTOC(std::istream &str, BinWritter &out, const TSTRING &dir) {
    std::string cLine;
    std::getline(str, cLine);
    int ver = atohLUT[cLine[4]];
    SARC::CompressionType cType = SARC::C_NONE;

    if (cLine[5] == 'C')
      cType = SARC::C_ZLIB;
    else if (cLine[5] == 'A')
      cType = SARC::C_AAF;
    else if (cLine[5] != 'U') {
      printwarning("[TOC] Unexpected compression token: ", << cLine[5]);
    }

    if (ver < 2 || ver > 3) {
      printerror("[TOC] Unknown version!");
      return 1;
    }

    DirectoryScanner::storage_type files;

    while (std::getline(str, cLine), cLine.size()) {
      files.push_back(dir + static_cast<TSTRING>(esString(cLine)));
    }

    if (cType == SARC::C_NONE) {
      Create(out, files, static_cast<SARCVersion>(ver), dir);
    } else {
      std::stringstream ms;
      BinWritter wr(ms);

      Create(wr, files, static_cast<SARCVersion>(ver), dir);

      std::string rBuffer = ms.str();

      ms.str("");

      if (cType == SARC::C_AAF) {
        AAF aaf;

        if (aaf.Write(&out, &rBuffer[0], rBuffer.size()))
          return 2;
      } else {
        if (CompressArchive(&out, &rBuffer[0], rBuffer.size()))
          return 2;
      }
    }

    return 0;
  }
};

int FileExtractArchive(BinReader *rd, const TCHAR *file,
                       SARC::CompressionType compType) {
  SARC *SARCInstance = new SARC2();
  int resltld = SARCInstance->Load(rd);

  if (resltld == 1) {
    return 1;
  } else if (resltld == 2) {
    rd->Seek(0);
    delete SARCInstance;
    SARCInstance = new SARC3();
    resltld = SARCInstance->Load(rd);
  }

  if (resltld)
    return 1;

  printline("SARC V", << SARCInstance->GetVersion() << " detected.");

  SARCInstance->ExtractFiles(rd, file, compType);

  return 0;
}

void FilehandleITFC(const _TCHAR *fle) {
  printline("Loading Archive: ", << fle);
  TSTRING infile = fle;
  BinReader rd(infile);

  if (!rd.IsValid()) {
    printline("Could not load file.");
    return;
  }

  int magic;
  rd.Read(magic);
  rd.Seek(0);

  if (magic == AAF::ID) {
    printline("AAF detected.");
    AAF AAFInstance;
    int resltld = AAFInstance.Load(&rd);

    if (resltld == 3) {
      printerror("Corrupted AAF file!");
      return;
    } else if (resltld == 2) {
      printerror("Invalid AAF file!");
      return;
    }

    std::stringstream ss;
    AAFInstance.GetNewStream(&ss);
    rd.SetStream(ss);

    FileExtractArchive(&rd, fle, SARC::C_AAF);
  } else if (magic == 4) {
    FileExtractArchive(&rd, fle, SARC::C_NONE);
  } else if (magic == CompileFourCC("TOCL")) {
    printline("TOC detected.");
    TFileInfo fInf(infile);
    TSTRING aFile = fInf.GetPath() + fInf.GetFileName();
    printline("Creating archive: ", << aFile);
    SARCPacker pck;
    BinWritter wr(aFile);
    std::istream dummy(0);
    rd.SetStream(dummy);
    std::ifstream textStream(infile);

    if (!wr.IsValid() || pck.FromTOC(textStream, wr, fInf.GetPath())) {
      printerror("Cannot create archive!");
      textStream.close();
      return;
    }

    textStream.close();

    printline("Archive created.");
  } else if (static_cast<uchar>(magic) == 0x78) {
    const size_t fileSize = rd.GetSize();
    std::string buffer;
    std::string oBuffer;
    std::stringstream ss;
    BinWritter lwr(ss);
    buffer.resize(fileSize < (AAF::MAX_BLOCK_SIZE / 2)
                      ? fileSize
                      : (AAF::MAX_BLOCK_SIZE / 2));
    oBuffer.resize(AAF::MAX_BLOCK_SIZE / 2);
    rd.ReadContainer(buffer, buffer.size());

    z_stream infstream;
    infstream.zalloc = Z_NULL;
    infstream.zfree = Z_NULL;
    infstream.opaque = Z_NULL;
    infstream.avail_in = buffer.size();
    infstream.next_in = reinterpret_cast<Bytef *>(&buffer[0]);
    infstream.avail_out = oBuffer.size();
    infstream.next_out = reinterpret_cast<Bytef *>(&oBuffer[0]);
    inflateInit2(&infstream, MAX_WBITS);
    int state = 0;

    while (state = inflate(&infstream, Z_SYNC_FLUSH), !state) {
      if (!infstream.avail_in) {
        rd.ReadContainer(buffer, buffer.size());
        infstream.avail_in = buffer.size();
        infstream.next_in = reinterpret_cast<Bytef *>(&buffer[0]);
      }

      if (!infstream.avail_out) {
        lwr.WriteContainer(oBuffer);
        infstream.avail_out = oBuffer.size();
        infstream.next_out = reinterpret_cast<Bytef *>(&oBuffer[0]);
      }
    }

    if (state != Z_STREAM_END) {
      printerror("[ZLIB] Expected Z_STREAM_END.");
      return;
    }

    oBuffer.resize((AAF::MAX_BLOCK_SIZE / 2) - infstream.avail_out);
    lwr.WriteContainer(oBuffer);
    inflateEnd(&infstream);
    BinReader lrd(ss);
    FileExtractArchive(&lrd, fle, SARC::C_ZLIB);
  } else {
    printerror("Unknown file type!");
  }
}

struct SarcQueueTraits {
  int queue;
  int queueEnd;
  TCHAR **files;
  typedef void return_type;

  return_type RetreiveItem() { FilehandleITFC(files[queue]); }

  operator bool() { return queue < queueEnd; }
  void operator++(int) { queue++; }
  int NumQueues() const { return queueEnd - 1; }
};

int _tmain(int argc, _TCHAR *argv[]) {
  setlocale(LC_ALL, "");
  printer.AddPrinterFunction(wprintf);

  printline(SmallArchive_DESC " V" SmallArchive_VERSION
                              "\n" SmallArchive_COPYRIGHT
                              "\nSimply drag'n'drop files into "
                              "application or use as " SmallArchive_PRODUCT_NAME
                              " file1 file2 ...\n");

  TFileInfo configInfo(*argv);
  const TSTRING configName =
      configInfo.GetPath() + configInfo.GetFileName() + _T(".config");

  settings.FromXML(configName);
  settings.Process();

  pugi::xml_document doc = {};
  settings.ToXML(doc);
  doc.save_file(configName.c_str(), "\t",
                pugi::format_write_bom | pugi::format_indent);

  if (argc < 2) {
    printerror("Insufficient argument count, expected at least 1.\n");
    printer << help << pressKeyCont >> 1;
    getchar();
    return 1;
  }

  if (argv[1][0] == '-') {
    if (argv[1][1] == '?' || argv[1][1] == 'h') {
      printer << help << pressKeyCont >> 1;
      getchar();
      return 0;
    } else if (argv[1][1] == 'a') {
      printline("Creating archive: ", << argv[2]) SARCPacker pck;
      BinWritter wr(argv[2]);

      if (!wr.IsValid()) {
        printerror("Cannot create archive!");
        return 2;
      }

      int version = atohLUT[argv[3][0]];

      if (version < 2 || version > 3) {
        printerror("Unknown version parameter!");
        return 2;
      }

      pck.Scan(wr, argv[4], static_cast<SARCPacker::SARCVersion>(version));
      printline("Archive created.");

      return 0;
    } else if (argv[1][1] == 'c' || argv[1][1] == 'f') {
      printline("Creating compressed archive: ", << argv[2]) SARCPacker pck;
      BinWritter wrout(argv[2]);

      if (!wrout.IsValid()) {
        printerror("Cannot create: ", << argv[2]);
        return 2;
      }

      int version = atohLUT[argv[3][0]];

      if (version < 2 || version > 3) {
        printerror("Unknown version parameter!");
        return 2;
      }

      std::stringstream ms;
      BinWritter wr(ms);

      pck.Scan(wr, argv[4], static_cast<SARCPacker::SARCVersion>(version));

      std::string rBuffer = ms.str();

      ms.str("");

      if (argv[1][1] == 'f') {
        AAF aaf;

        if (aaf.Write(&wrout, &rBuffer[0], rBuffer.size()))
          return 3;
      } else {
        if (CompressArchive(&wrout, &rBuffer[0], rBuffer.size()))
          return 3;
      }

      printline("Archive created.");

      return 0;
    }
  }

  if (settings.Generate_Log)
    settings.CreateLog(configInfo.GetPath() + configInfo.GetFileName());

  SarcQueueTraits sarQue;
  sarQue.files = argv;
  sarQue.queue = 1;
  sarQue.queueEnd = argc;

  printer.PrintThreadID(true);
  RunThreadedQueue(sarQue);

  // getchar();
  return 0;
}
