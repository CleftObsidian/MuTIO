#include "IOThreadPool.h"

namespace fs = std::filesystem;

int main(void)
{
	constexpr size_t NUM_THREADS = 4;
	constexpr size_t NUM_FILES_TO_READ = 1000;

	constexpr size_t MAX_READ_PROC_COUNT = 3;			// must be >0, Only use ThreadMode::READ_N_AND_PROCESS_N

	//constexpr ThreadMode THREAD_MODE = ThreadMode::READ_ALL_AND_PROCESS_ALL;
	//constexpr ThreadMode THREAD_MODE = ThreadMode::READ_CHECK_PROCESS_CHECK;
	//constexpr ThreadMode THREAD_MODE = ThreadMode::READ_N_AND_PROCESS_N;							// N follows MAX_READ_PROC_COUNT
	//constexpr ThreadMode THREAD_MODE = ThreadMode::CREATE_ALL_AND_READ_ALL_AND_PROCESS_ALL;
	//constexpr ThreadMode THREAD_MODE = ThreadMode::CREATE_ALL_AND_READ_CHECK_PROCESS_CHECK;
	//constexpr ThreadMode THREAD_MODE = ThreadMode::SINGLE_READ_ONLY_ELSE_PROCESS_ONLY;
	constexpr ThreadMode THREAD_MODE = ThreadMode::SINGLE_READ_ALL_AND_PROCESS_ELSE_PROCESS_ONLY;
	//constexpr ThreadMode THREAD_MODE = ThreadMode::SYNC_IO;

	const fs::path directoryPathToReadFiles(fs::absolute(fs::current_path().parent_path() / "generatedFiles"));
	fs::directory_iterator it(directoryPathToReadFiles);
	size_t numAddedFiles = 0;

	IOThreadPool threadPool(NUM_THREADS, NUM_FILES_TO_READ, MAX_READ_PROC_COUNT, THREAD_MODE);
	switch (THREAD_MODE)
	{
	case ThreadMode::READ_ALL_AND_PROCESS_ALL:
	case ThreadMode::READ_CHECK_PROCESS_CHECK:
	case ThreadMode::READ_N_AND_PROCESS_N:
	case ThreadMode::SINGLE_READ_ONLY_ELSE_PROCESS_ONLY:
	case ThreadMode::SINGLE_READ_ALL_AND_PROCESS_ELSE_PROCESS_ONLY:
	case ThreadMode::SYNC_IO:
		for (; it != fs::end(it); ++it)
		{
			if (numAddedFiles < NUM_FILES_TO_READ)
			{
				threadPool.AddFileToHashMap(numAddedFiles, it->path());
				++numAddedFiles;
			}
			else
			{
				break;
			}
		}
		break;
	case ThreadMode::CREATE_ALL_AND_READ_ALL_AND_PROCESS_ALL:
	case ThreadMode::CREATE_ALL_AND_READ_CHECK_PROCESS_CHECK:
		for (; it != fs::end(it); ++it)
		{
			if (numAddedFiles < NUM_FILES_TO_READ)
			{
				threadPool.AddFileToHashMapForHandle(numAddedFiles, it->path());
				++numAddedFiles;
			}
			else
			{
				break;
			}
		}
		break;
	default:
		break;
	}

	return 0;
}