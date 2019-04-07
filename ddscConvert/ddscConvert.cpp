// ddscConvert.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <thread>
#include <mutex>
#include <chrono>
#include <ctime>
#include <codecvt>
#include <locale>
#include <tchar.h>
#include "formats/DDS.hpp"
#include "AVTX.h"
#include "datas/binreader.hpp"
#include "datas/reflector.hpp"
#include "datas/fileinfo.hpp"
#include "datas/masterprinter.hpp"

static struct ddscConvert: private Reflector
{
	DECLARE_REFLECTOR;
	bool Convert_DDS_to_legacy = true;
	bool Use_HMDDSC = false;
	bool Generate_Log = false;
	int Number_of_ATX_levels = 2;
	int ATX_level0_max_resolution = 256;
	int ATX_level1_max_resolution = 1024;
	int ATX_level2_max_resolution = 2048;
	int ATX_leveln_min_resolution = 4096;

	using Reflector::FromXML;
	using Reflector::ToXML;

	std::wofstream logger;

	static void tprintf(const TCHAR *str)
	{
		settings.logger << str;
	}

	void CreateLog(const TFileInfo &fle)
	{
		if (!Generate_Log)
			return;

		time_t curTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::tm timeStruct = {};
		localtime_s(&timeStruct, &curTime);
		TSTRING dateBuffer;
		dateBuffer.resize(31);
		_tcsftime(const_cast<TCHAR *>(dateBuffer.data()), 32, _T("_%y_%m_%d-%H.%M.%S"), &timeStruct);
		TSTRING logName = fle.GetPath() + fle.GetFileName() + dateBuffer.c_str();
		logName.append(_T(".txt"));
		logger.open(logName, std::ios::out);

		std::locale utf8_locale = std::locale(std::locale(), new std::codecvt_utf8<TCHAR>());
		logger.imbue(utf8_locale);

		printer.AddPrinterFunction(settings.tprintf);

		dateBuffer.resize(64);
		_tcsftime(const_cast<TCHAR *>(dateBuffer.data()), 64, _T("%c %Z"), &timeStruct);

		logger << "Current time: " << dateBuffer.c_str() << std::endl;
		logger << "Number of concurrent threads: " << std::thread::hardware_concurrency() << std::endl;
		logger << "Configuration:" << std::endl;

		const int numSettings = GetNumReflectedValues();

		for (int t = 0; t < numSettings; t++)
		{
			KVPair pair = GetReflectedPair(t);
			logger << '\t' << pair.name << ": " << pair.value.c_str() << std::endl;
		}

		logger << std::endl;
	}
}settings;

REFLECTOR_START_WNAMES(ddscConvert, Convert_DDS_to_legacy, Generate_Log, Number_of_ATX_levels, Use_HMDDSC, ATX_level0_max_resolution, ATX_level1_max_resolution, ATX_level2_max_resolution, ATX_leveln_min_resolution);

void FilehandleITFC(const _TCHAR *fle)
{
	printline("Loading file: ", << fle);

	BinReader rd(fle);

	if (!rd.IsValid())
	{
		printerror("Cannot open file.");
		return;
	}

	int ID;
	rd.Read(ID);
	rd.Seek(0);

	if (ID == AVTX::ID)
	{
		printline("Converting AVTX -> DDS.");
		AVTX tx = {};
		tx.Load(fle, rd);

		TFileInfo fleInfo = fle;
		std::ofstream ofs(fleInfo.GetPath() + fleInfo.GetFileName() + _T(".dds"), std::ios::out | std::ios::binary);

		DDS tex = {};
		tex = DDSFormat_DX10;
		tex.dxgiFormat = static_cast<DXGI_FORMAT>(tx.format);
		tex.NumMipmaps(tx.mipCount);
		tex.width = tx.width;
		tex.height = tx.height;
		tex.arraySize = tx.numArrayElements;

		const int sizetoWrite = !settings.Convert_DDS_to_legacy || tex.arraySize > 1 || tex.ToLegacy() ? tex.DDS_SIZE : tex.LEGACY_SIZE;

		if (settings.Convert_DDS_to_legacy && sizetoWrite == tex.DDS_SIZE)
		{
			printwarning("Couldn't convert DX10 dds to legacy.")
		}

		ofs.write(reinterpret_cast<const char *>(&tex), sizetoWrite);
		ofs.write(tx.buffer, tx.BufferSize());
		ofs.close();
	}
	else if (ID == DDS::ID)
	{
		printline("Converting DDS -> AVTX.");
		DDS tex = {};
		rd.ReadBuffer(reinterpret_cast<char *>(&tex), tex.LEGACY_SIZE);

		if (tex.fourCC != DDSFormat_DX10.fourCC)
		{
			if (tex.ToLegacy())
			{
				printerror("DDS file cannot be converted to DX10!");
				return;
			}
		}
		else
		{
			rd.Read(static_cast<DDS_HeaderDX10 &>(tex));
		}

		DDS::MipSizes mips;
		tex.ComputeBPP();
		const int bufferSize = tex.ComputeBufferSize(mips);

		if (!bufferSize)
		{
			printerror("Usupported DDS format.");
			return;
		}

	}
	else
	{
		printerror("Invalid file format.")
	}
}

int _tmain(int argc, _TCHAR *argv[])
{
	setlocale(LC_ALL, "");
	printer.AddPrinterFunction(wprintf);
	printline("Apex AVTX Converter by Lukas Cone in 2019.\nSimply drag'n'drop files into application or use as ddscConvert file1 file2 ...\n");

	TFileInfo configInfo(*argv);
	const TSTRING configName = configInfo.GetPath() + configInfo.GetFileName() + _T(".config");

	settings.FromXML(configName);
	settings.ToXML(configName);

	if (argc < 2)
	{
		printerror("Insufficient argument count, expected at aleast 1");
		return 1;
	}

	settings.CreateLog(configInfo);

	const int nthreads = std::thread::hardware_concurrency();
	std::vector<std::thread> threadedfuncs(nthreads);

	printer.PrintThreadID(true);

	for (int a = 1; a < argc; a += nthreads)
	{
		for (int t = 0; t < nthreads; t++)
		{
			if (a + t > argc - 1) break;
			threadedfuncs[t] = std::thread(FilehandleITFC, argv[a + t]);
		}

		for (int t = 0; t < nthreads; t++)
		{
			if (a + t > argc - 1) break;
			threadedfuncs[t].join();
		}
	}

	return 0;
}