/*  R2SmallArchive
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

#include <thread>
#include <mutex>
#include "datas/binreader.hpp"
#include "datas/SettingsManager.hpp"
#include "datas/fileinfo.hpp"
#include "pugixml.hpp"

static struct R2SmallArchive : SettingsManager
{
	DECLARE_REFLECTOR;
	bool Generate_Log = false;
	std::string sarc0_gtoc_file_path = "Path into sarc.0.gtoc",
		expentities_gtoc_file_path = "Path into expentities.gtoc";
}settings;

REFLECTOR_START_WNAMES(R2SmallArchive, sarc0_gtoc_file_path, expentities_gtoc_file_path, Generate_Log);

struct GTOCFile
{
	uint hash1,
		hash2;
	int fileSize;
	char fileName[1];
};

struct GTOCAddtionalFileEntry
{
	int fileEntryOffset,
	fileOffset;

	ES_INLINE const GTOCFile *GetEntry() const
	{
		return reinterpret_cast<const GTOCFile *>(reinterpret_cast<const char *>(this) + fileEntryOffset);
	}
};

struct GTOCEntry
{
	uint hash1,
		hash2;
	int numFiles;
	uint firstEntryOffset;
	uint count01;

	ES_INLINE const GTOCAddtionalFileEntry *GetAdditionalFiles() const
	{
		return reinterpret_cast<const GTOCAddtionalFileEntry *>(this + 1);
	}

	ES_INLINE uint GetFileOffset(int id) const
	{
		if (id == 1)
			return 4;
		else
			return (GetAdditionalFiles() + id - 1)->fileOffset;
	}

	ES_INLINE const GTOCFile *GetFile(int id) const
	{
		if (id == 1)
			return reinterpret_cast<const GTOCFile *>(reinterpret_cast<const char *>(this) + offsetof(GTOCEntry, firstEntryOffset) + firstEntryOffset);
		else
			return (GetAdditionalFiles() + id - 1)->GetEntry();
	}

	void mkdirs(const TSTRING &inFilepath) const
	{
		for (int f = 0; f < numFiles; f++)
		{
			const char *cfle = GetFile(f)->fileName;
			TSTRING filePath = esStringConvert<TCHAR>(cfle);

			for (size_t s = 0; s < filePath.length(); s++)
				if (filePath[s] == '\\' || filePath[s] == '/')
				{
					TSTRING genpath = inFilepath;
					genpath.append(filePath.substr(0, s));
					_tmkdir(genpath.c_str());
				}
		}
	}
};

struct GTOC
{
	static const int gtocID = CompileFourCC("GT0C");

	char *masterBuffer;
	std::vector<GTOCEntry *> archives;

	GTOC() : masterBuffer(nullptr) {}
	~GTOC()
	{
		if (masterBuffer)
			free(masterBuffer);
	}

	int Load(BinReader *rd)
	{
		int ID;
		rd->Read(ID);

		if (ID != gtocID)
		{
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

		for (auto &a : archives)
		{
			a = reinterpret_cast<GTOCEntry *>(masterCopy);
			masterCopy += static_cast<size_t>(a->numFiles - 1) * sizeof(GTOCAddtionalFileEntry) + sizeof(GTOCEntry);
		}

		return 0;
	}

	GTOCEntry *FindEntry(uint hash) const
	{
		for (auto &a : archives)
			if (a->hash2 == hash)
				return a;

		return nullptr;
	}
};

GTOC *LoadGTOC(const std::string &path)
{
	TSTRING filepath = esStringConvert<TCHAR>(path.c_str());
	BinReader rd(filepath);

	if (!rd.IsValid())
	{
		printerror("Cannot open gtoc file.");
		return nullptr;
	}

	GTOC *gtoc = new GTOC();
	if (gtoc->Load(&rd))
	{
		delete gtoc;
		return nullptr;
	}

	return gtoc;
}

void FilehandleITFC(const TCHAR *fle, const GTOC **globalTOC)
{
	printline("Loading file: ", << fle);

	TSTRING filepath = fle;
	BinReader rd(fle);

	if (!rd.IsValid())
	{
		printerror("Cannot open file.");
		return;
	}

	const size_t sarcSize = rd.GetSize();
	char *dataBuffer = static_cast<char *>(malloc(sarcSize));
	rd.ReadBuffer(dataBuffer, sarcSize);
	
	const GTOCEntry *cEntry = (*globalTOC)->FindEntry(*reinterpret_cast<uint *>(dataBuffer));

	if (!cEntry)
		cEntry = (*(globalTOC + 1))->FindEntry(*reinterpret_cast<uint *>(dataBuffer));

	if (!cEntry)
	{
		printerror("Cannot find file in global table.");
		return;
	}

	TFileInfo finfo(filepath);
	cEntry->mkdirs(finfo.GetPath());
	const int numFiles = cEntry->numFiles;
	
	for (int f = 0; f < numFiles; f++)
	{
		const GTOCFile *cFile = cEntry->GetFile(f);

		if (cFile->fileSize < 1)
			continue;

		TSTRING cFilePath = finfo.GetPath() + esStringConvert<TCHAR>(cFile->fileName);
		std::ofstream fileOut(cFilePath, std::ios_base::binary | std::ios_base::out);
		
		if (fileOut.fail())
		{
			printerror("Couldn't create file: ", << cFilePath);
			continue;
		}

		fileOut.write(dataBuffer + cEntry->GetFileOffset(f), cFile->fileSize);		
		fileOut.close();
	}

	free(dataBuffer);
	return;
}

int _tmain(int argc, _TCHAR *argv[])
{
	setlocale(LC_ALL, "");
	printer.AddPrinterFunction(wprintf);

	printline("Rage 2 Small Archive Extractor by Lukas Cone in 2019.\nSimply drag'n'drop files into application or use as R2SmallArchive file1 file2 ...\n");

	TFileInfo configInfo(*argv);
	const TSTRING configName = configInfo.GetPath() + configInfo.GetFileName() + _T(".config");

	settings.FromXML(configName);

	pugi::xml_document doc = {};
	settings.ToXML(doc);
	doc.save_file(configName.c_str(), "\t", pugi::format_write_bom | pugi::format_indent);

	if (argc < 2)
	{
		printerror("Insufficient argument count, expected at aleast 1.\n");
		return 1;
	}

	if (settings.Generate_Log)
		settings.CreateLog(configInfo.GetPath() + configInfo.GetFileName());

	const GTOC *mainGTOC[2] = { LoadGTOC(settings.sarc0_gtoc_file_path), LoadGTOC(settings.expentities_gtoc_file_path) };

	for (auto &t : mainGTOC)
		if (!t)
			return 2;

	const int nthreads = std::thread::hardware_concurrency();
	std::vector<std::thread> threadedfuncs(nthreads);

	printer.PrintThreadID(true);

	for (int a = 1; a < argc; a += nthreads)
	{
		for (int t = 0; t < nthreads; t++)
		{
			if (a + t > argc - 1) break;
			threadedfuncs[t] = std::thread(FilehandleITFC, argv[a + t], mainGTOC);
		}

		for (int t = 0; t < nthreads; t++)
		{
			if (a + t > argc - 1) break;
			threadedfuncs[t].join();
		}
	}

	for (auto &t : mainGTOC)
		delete t;

	return 0;
}