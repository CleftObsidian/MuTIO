#pragma once
#include "Common.h"
#include <queue>
#include <unordered_map>

#include "FileInfo.h"

class IOThreadPool final
{
public:
	IOThreadPool(size_t threadMaxSize, size_t numFiles, size_t maxReadProcCount, ThreadMode threadMode);
	IOThreadPool(const IOThreadPool& other) = delete;
	IOThreadPool& operator=(const IOThreadPool& other) = delete;
	~IOThreadPool();

	void												AddFileToHashMap(size_t fileKey, std::filesystem::path filePath);
	void												AddFileToHashMapForHandle(size_t fileKey, std::filesystem::path filePath);
	void												Release(void);

private:
	HANDLE												_completionPort;
	const size_t										_threadMaxSize;
	std::vector<HANDLE>									_workerThreads;

	std::queue<size_t>									_createQueue;
	std::queue<size_t>									_readQueue;
	std::unordered_map<size_t, FileInfo*>				_fileInfoHashMap;

	CRITICAL_SECTION									_csCreateRead;
	CRITICAL_SECTION									_csProcess;

	size_t												_numCreatedFiles;
	size_t												_numReadFiles;
	size_t												_numProcessedFiles;
	const size_t										_targetNumFiles;

	const size_t										_maxReadProcCount;

	LARGE_INTEGER										_startIOtime;
	LARGE_INTEGER										_endIOtime;
	
private:
	static UINT __stdcall								workerReadAllProcess(LPVOID pParam);
	static UINT __stdcall								workerAlwaysCheck(LPVOID pParam);
	static UINT __stdcall								workerCounting(LPVOID pParam);
	static UINT __stdcall								workerCreateAllReadAllProcess(LPVOID pParam);
	static UINT __stdcall								workerCreateAllAlwaysCheck(LPVOID pParam);
	static UINT __stdcall								workerOnlyRead(LPVOID pParam);
	static UINT __stdcall								workerOnlyProcess(LPVOID pParam);
	static UINT __stdcall								workerSyncIO(LPVOID pParam);

	void												threadCreateReadFile(const size_t& fileKey);
	void												threadOnlyCreateFile(const size_t& fileKey);
	void												threadOnlyReadFile(const size_t& fileKey);
	void												threadProcessFile(const size_t& fileKey);
	void												threadSyncIO(const size_t& fileKey);
};