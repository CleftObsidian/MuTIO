#pragma once

#include <Windows.h>
#include <filesystem>
#include <vector>

enum class ThreadMode
{
	READ_ALL_AND_PROCESS_ALL,							// Open and read all files, and then process
	READ_CHECK_PROCESS_CHECK,							// Alway check both queue to read, and to process
	READ_N_AND_PROCESS_N,								// Repeat send read call for N files, and process N files
	CREATE_ALL_AND_READ_ALL_AND_PROCESS_ALL,			// Open all files first, and read all files, and then process
	CREATE_ALL_AND_READ_CHECK_PROCESS_CHECK,			// Open all files first, and check both queue to read, and to process
	SINGLE_READ_ONLY_ELSE_PROCESS_ONLY,					// One thread only sends read call, other threads only process
	SINGLE_READ_ALL_AND_PROCESS_ELSE_PROCESS_ONLY,		// One thread sends read call first and then process, other threads only process
	SYNC_IO,											// NOT overlapped I/O, each thread open, read, and process each file
};