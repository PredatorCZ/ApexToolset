// ddscConvert.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <thread>
#include <mutex>
#include "formats/DDS.hpp"
#include "AVTX.h"
#include "datas/binreader.hpp"
#include "datas/SettingsManager.hpp"
#include "datas/fileinfo.hpp"

static struct ddscConvert : SettingsManager
{
	DECLARE_REFLECTOR;
	bool Convert_DDS_to_legacy = true;
	bool Force_unconvetional_legacy_formats = true;
	bool Use_HMDDSC = false;
	bool Generate_Log = false;
	bool No_Tiling = true;
	int Number_of_ATX_levels = 2;
	int ATX_level0_max_resolution = 256;
	int ATX_level1_max_resolution = 1024;
	int ATX_level2_max_resolution = 2048;
}settings;

REFLECTOR_START_WNAMES(ddscConvert, Convert_DDS_to_legacy, Force_unconvetional_legacy_formats, Generate_Log, Number_of_ATX_levels, Use_HMDDSC, ATX_level0_max_resolution,
	ATX_level1_max_resolution, ATX_level2_max_resolution, No_Tiling);

static int levelResolutions[] = { 0x8000, 0x8000, 0x8000, 0x8000, 0x8000 };

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
		AVTX tx;
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

		if (tx.flags[AVTX::Flag_CubeMap])
		{
			tex.caps01 = decltype(tex.caps01)(DDS::Caps01Flags_CubeMap, DDS::Caps01Flags_CubeMap_NegativeX, DDS::Caps01Flags_CubeMap_NegativeY, DDS::Caps01Flags_CubeMap_NegativeZ,
				DDS::Caps01Flags_CubeMap_PositiveX, DDS::Caps01Flags_CubeMap_PositiveY, DDS::Caps01Flags_CubeMap_PositiveZ);
		}

		const int sizetoWrite = !settings.Convert_DDS_to_legacy || tex.arraySize > 1 || tex.ToLegacy(settings.Force_unconvetional_legacy_formats) ? tex.DDS_SIZE : tex.LEGACY_SIZE;

		if (settings.Convert_DDS_to_legacy && sizetoWrite == tex.DDS_SIZE)
		{
			printwarning("Couldn't convert DX10 dds to legacy.")
		}
		
		ofs.write(reinterpret_cast<const char *>(&tex), sizetoWrite);

		if (tx.flags[AVTX::Flag_CubeMap] && tx.mipCount > 1)
		{
			DDS::Mips cubes[6] = {};
			tex.ComputeBPP();

			int bufferSize = tex.ComputeBufferSize(*cubes) * tex.arraySize;

			if (!bufferSize)
			{
				printerror("Unsupported DDS format.");
				return;
			}

			for (int c = 1; c < 6; c++)
				memcpy_s(&cubes[c], sizeof(DDS::Mips), cubes, sizeof(DDS::Mips));
			
			int currentMipOffset =  0;
			
			for (int m = 0; m < tx.mipCount; m++)
			{
				const int currentMipSize = cubes[0].sizes[m];

				for (int c = 0; c < 6; c++)
				{
					cubes[c].offsets[m] = currentMipOffset + (currentMipSize * c);
				}

				currentMipOffset = cubes[5].offsets[m] + currentMipSize;
			}

			for (int c = 0; c < 6; c++)
				for (int m = 0; m < tx.mipCount; m++)
					ofs.write(tx.buffer + cubes[c].offsets[m], cubes[c].sizes[m]);

		}
		else
			ofs.write(tx.buffer, tx.BufferSize());

		ofs.close();
	}
	else if (ID == DDS::ID)
	{
		printline("Converting DDS -> AVTX.");
		DDS tex = {};
		rd.ReadBuffer(reinterpret_cast<char *>(&tex), tex.LEGACY_SIZE);

		if (tex.caps01[DDS::Caps01Flags_Volume])
		{
			printerror("Volumetric DDS textures are not supported.");
			return;
		}

		if (tex.caps01[DDS::Caps01Flags_CubeMap])
		{
			if (!tex.caps01[DDS::Caps01Flags_CubeMap_NegativeX] && tex.caps01[DDS::Caps01Flags_CubeMap_NegativeY] && tex.caps01[DDS::Caps01Flags_CubeMap_NegativeZ] &&
				tex.caps01[DDS::Caps01Flags_CubeMap_PositiveX] && tex.caps01[DDS::Caps01Flags_CubeMap_PositiveY] && tex.caps01[DDS::Caps01Flags_CubeMap_PositiveZ])
			{
				printerror("Cubemap DDS must have all sides.");
				return;
			}
		}

		if (tex.fourCC != DDSFormat_DX10.fourCC)
		{
			if (tex.FromLegacy())
			{
				printerror("DDS file cannot be converted to DX10!");
				return;
			}
		}
		else
		{
			rd.Read(static_cast<DDS_HeaderDX10 &>(tex));
		}
		
		if (tex.mipMapCount < 2)
		{
			printerror("DDS file must have generated mipmaps.");
			return;
		}	

		DDS::Mips dMips = {};
		int mipIDs[DDS::Mips::maxMips] = {};
		tex.ComputeBPP();
		const int bufferSize = tex.ComputeBufferSize(dMips) * tex.arraySize * (tex.caps01[DDS::Caps01Flags_CubeMap] ? 6 : 1);

		if (!bufferSize)
		{
			printerror("Usupported DDS format.");
			return;
		}

		char *masterBuffer = static_cast<char *>(malloc(bufferSize));
		rd.ReadBuffer(masterBuffer, bufferSize);

		AVTX tx;
		tx.flags(AVTX::Flag_ExternalBuffers, tex.arraySize == 1 && settings.Number_of_ATX_levels > 0 && !tex.caps01[DDS::Caps01Flags_CubeMap]);
		tx.flags(AVTX::Flag_NoTiling, settings.No_Tiling);
		tx.flags(AVTX::Flag_CubeMap, tex.caps01[DDS::Caps01Flags_CubeMap]);
		tx.numArrayElements = tex.arraySize;
		tx.width = tex.width;
		tx.height = tex.height;
		tx.format = tex.dxgiFormat;
		tx.mipCount = tex.mipMapCount;
		tx.entries[0].offset = 128;
		tx.entries[0].flags += AVTX::Entry::Flag_Used;
		tx.entries[0].size = tex.arraySize > 1 || tex.caps01[DDS::Caps01Flags_CubeMap] ? bufferSize : 0;

		if (!tx.flags[AVTX::Flag_ExternalBuffers])
			tx.headerMipCount = tx.mipCount;

		TFileInfo fleInfo = fle;
		TSTRING masterFileName = fleInfo.GetPath() + fleInfo.GetFileName() + _T(".ddsc");
		std::ofstream ddscStream(masterFileName, std::ios::out | std::ios::binary);
		ddscStream.write(reinterpret_cast<char *>(&tx), sizeof(AVTX));

		if (!tx.flags[AVTX::Flag_ExternalBuffers] && !tex.caps01[DDS::Caps01Flags_CubeMap])
		{
			ddscStream.write(masterBuffer, bufferSize);
			ddscStream.close();
			return;
		}

		if (tex.caps01[DDS::Caps01Flags_CubeMap])
		{
			DDS::Mips cubes[6] = {};
			const int sideSize = bufferSize / 6;

			for (int c = 0; c < 6; c++)
			{
				memcpy_s(&cubes[c], sizeof(DDS::Mips), &dMips, sizeof(DDS::Mips));

				for (int m = 0; m < tx.mipCount; m++)
					cubes[c].offsets[m] += sideSize * c;
			}
			
			for (int m = 0; m < tx.mipCount; m++)
				for (int c = 0; c < 6; c++)
					ddscStream.write(masterBuffer + cubes[c].offsets[m], cubes[c].sizes[m]);

			ddscStream.close();
			return;
		}

		int _weight = 1,
			_height = 1,
			currentLevel = 0,
			externalMipID = 0,
			mipOffsetWithinLevel = 0;

		for (int m = tex.mipMapCount - 1; m >= 0; m--)
		{
			mipIDs[m] = currentLevel;
			if (!currentLevel)
			{
				tx.entries[0].size += dMips.sizes[m];
				tx.headerMipCount++;
			}
			else
			{
				tx.entries[externalMipID].externalID = currentLevel;
				tx.entries[externalMipID].flags += AVTX::Entry::Flag_Used;
				tx.entries[externalMipID].offset = mipOffsetWithinLevel;
				tx.entries[externalMipID].size = dMips.sizes[m];
				mipOffsetWithinLevel += dMips.sizes[m];
				externalMipID++;
			}

			_weight *= 2;
			_height *= 2;
			
			if ((_weight | _height) > levelResolutions[currentLevel])
			{
				if (!currentLevel)
					externalMipID++;

				currentLevel++;
				mipOffsetWithinLevel = 0;
			}
		}

		std::ofstream ofs;
		currentLevel = 0;

		for (int m = tex.mipMapCount - 1; m >= 0; m--)
		{
			if (currentLevel != mipIDs[m])
			{
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
		ddscStream.write(masterBuffer + (bufferSize - tx.entries[0].size), tx.entries[0].size);
		ddscStream.close();
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
	
	if (argc < 2)
	{
		printerror("Insufficient argument count, expected at aleast 1");
		return 1;
	}

	TFileInfo configInfo(*argv);
	const TSTRING configName = configInfo.GetPath() + configInfo.GetFileName() + _T(".config");

	settings.FromXML(configName);
	settings.ToXML(configName);

	if (settings.Generate_Log)
		settings.CreateLog(configInfo.GetPath() + configInfo.GetFileName());

	if (settings.Number_of_ATX_levels > 3 || settings.Number_of_ATX_levels < 0)
	{
		int temp = settings.Number_of_ATX_levels;
		settings.Number_of_ATX_levels = settings.Number_of_ATX_levels > 3 ? 3 : 0;

		printwarning("Number_of_ATX_levels: Unexpected value ", << temp << _T(", clamping to ") << settings.Number_of_ATX_levels);
	}

	if (settings.Use_HMDDSC && settings.Number_of_ATX_levels > 1)
		settings.Number_of_ATX_levels = 1;

	for (int l = 0; l < settings.Number_of_ATX_levels; l++)
	{
		levelResolutions[l] = *(&settings.ATX_level0_max_resolution + l);
	}

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