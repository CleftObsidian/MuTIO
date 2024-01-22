#include "IOThreadPool.h"
#include <string>
#include <process.h>
#include <cvmarkersobj.h>
#include <io.h>

using namespace Concurrency::diagnostic;
marker_series g_createSeries(L"0_CreateFile");
marker_series g_readSeries(L"1_ReadFile");
marker_series g_processSeries(L"2_Processing");

IOThreadPool::IOThreadPool(size_t threadMaxSize, size_t numFiles, size_t maxReadProcCount, ThreadMode threadMode)
	: _completionPort(CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0))
	, _threadMaxSize(threadMaxSize)
	, _workerThreads()
	, _createQueue()
	, _readQueue()
	, _fileInfoHashMap()
	, _csCreateRead()
	, _csProcess()
	, _numCreatedFiles(0)
	, _numReadFiles(0)
	, _numProcessedFiles(0)
	, _targetNumFiles(numFiles)
	, _maxReadProcCount(maxReadProcCount)
	, _startIOtime()
	, _endIOtime()
{
	InitializeCriticalSection(&_csCreateRead);
	InitializeCriticalSection(&_csProcess);

	switch (threadMode)
	{
	case ThreadMode::READ_ALL_AND_PROCESS_ALL:
		for (size_t i = 0; i < _threadMaxSize; ++i)
		{
			//_workerThreads.push_back(CreateThread(NULL, 0, workerThread, this, 0, NULL));
			_workerThreads.push_back(reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, workerReadAllProcess, this, 0, NULL)));
		}
		break;
	case ThreadMode::READ_CHECK_PROCESS_CHECK:
		for (size_t i = 0; i < _threadMaxSize; ++i)
		{
			_workerThreads.push_back(reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, workerAlwaysCheck, this, 0, NULL)));
		}
		break;
	case ThreadMode::READ_N_AND_PROCESS_N:
		for (size_t i = 0; i < _threadMaxSize; ++i)
		{
			_workerThreads.push_back(reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, workerCounting, this, 0, NULL)));
		}
		break;
	case ThreadMode::CREATE_ALL_AND_READ_ALL_AND_PROCESS_ALL:
		for (size_t i = 0; i < _threadMaxSize; ++i)
		{
			_workerThreads.push_back(reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, workerCreateAllReadAllProcess, this, 0, NULL)));
		}
		break;
	case ThreadMode::CREATE_ALL_AND_READ_CHECK_PROCESS_CHECK:
		for (size_t i = 0; i < _threadMaxSize; ++i)
		{
			_workerThreads.push_back(reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, workerCreateAllAlwaysCheck, this, 0, NULL)));
		}
		break;
	case ThreadMode::SINGLE_READ_ONLY_ELSE_PROCESS_ONLY:
		_workerThreads.push_back(reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, workerOnlyRead, this, 0, NULL)));
		for (size_t i = 0; i < _threadMaxSize - 1; ++i)
		{
			_workerThreads.push_back(reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, workerOnlyProcess, this, 0, NULL)));
		}
		break;
	case ThreadMode::SINGLE_READ_ALL_AND_PROCESS_ELSE_PROCESS_ONLY:
		_workerThreads.push_back(reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, workerReadAllProcess, this, 0, NULL)));
		for (size_t i = 0; i < _threadMaxSize - 1; ++i)
		{
			_workerThreads.push_back(reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, workerOnlyProcess, this, 0, NULL)));
		}
		break;
	case ThreadMode::SYNC_IO:
		for (size_t i = 0; i < _threadMaxSize; ++i)
		{
			_workerThreads.push_back(reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, workerSyncIO, this, 0, NULL)));
		}
		break;
	default:
		break;
	}

	QueryPerformanceCounter(&_startIOtime);
}

IOThreadPool::~IOThreadPool()
{
	Release();
}

void IOThreadPool::AddFileToHashMap(size_t fileKey, std::filesystem::path filePath)
{
	EnterCriticalSection(&_csCreateRead);
	if (_fileInfoHashMap.find(fileKey) == _fileInfoHashMap.end())
	{
		_fileInfoHashMap.insert(std::make_pair(fileKey, new FileInfo(filePath)));
		_readQueue.push(fileKey);
	}
	LeaveCriticalSection(&_csCreateRead);
}

void IOThreadPool::AddFileToHashMapForHandle(size_t fileKey, std::filesystem::path filePath)
{
	EnterCriticalSection(&_csCreateRead);
	if (_fileInfoHashMap.find(fileKey) == _fileInfoHashMap.end())
	{
		_fileInfoHashMap.insert(std::make_pair(fileKey, new FileInfo(filePath)));
		_createQueue.push(fileKey);
	}
	LeaveCriticalSection(&_csCreateRead);
}

void IOThreadPool::Release(void)
{
	for (size_t i = 0; i < _threadMaxSize; ++i)
	{
		WaitForSingleObject(_workerThreads[i], INFINITE);
		CloseHandle(_workerThreads[i]);
	}

	QueryPerformanceCounter(&_endIOtime);
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	double elapsedTime = static_cast<double>(_endIOtime.QuadPart - _startIOtime.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
	std::printf("\nTotal elapsed time: %lf ms\n", elapsedTime);

	for (auto it = _fileInfoHashMap.begin(); it != _fileInfoHashMap.end(); ++it)
	{
		delete it->second;
	}

	DeleteCriticalSection(&_csCreateRead);
	DeleteCriticalSection(&_csProcess);

	CloseHandle(_completionPort);
}

UINT __stdcall IOThreadPool::workerReadAllProcess(LPVOID pParam)
{
	IOThreadPool* pPool = static_cast<IOThreadPool*>(pParam);
	DWORD dwTransfer = 0;
	ULONG_PTR keyValue = 0;
	LPOVERLAPPED pOverlapped = { 0 };
	bool bAllProcessed = false;
	bool bAvailable = false;

	while (true)
	{
		EnterCriticalSection(&pPool->_csProcess);
		bAllProcessed = pPool->_numProcessedFiles < pPool->_targetNumFiles;
		LeaveCriticalSection(&pPool->_csProcess);

		if (pPool->_numReadFiles < pPool->_targetNumFiles)
		{
			size_t fileKey(SIZE_MAX);
			EnterCriticalSection(&pPool->_csCreateRead);
			bAvailable = (0 < pPool->_readQueue.size());
			if (true == bAvailable)
			{
				fileKey = pPool->_readQueue.front();
				pPool->_readQueue.pop();
				++pPool->_numReadFiles;
			}
			LeaveCriticalSection(&pPool->_csCreateRead);

			if (true == bAvailable)
			{
				pPool->threadCreateReadFile(fileKey);
			}
		}
		else if (bAllProcessed)
		{
			bAvailable = GetQueuedCompletionStatus(pPool->_completionPort, &dwTransfer, &keyValue, &pOverlapped, 0);
			if (true == bAvailable)
			{
				EnterCriticalSection(&pPool->_csProcess);
				++pPool->_numProcessedFiles;
				LeaveCriticalSection(&pPool->_csProcess);

				pPool->threadProcessFile(keyValue);

				EnterCriticalSection(&pPool->_csCreateRead);
				FileInfo* info = pPool->_fileInfoHashMap[keyValue];
				LeaveCriticalSection(&pPool->_csCreateRead);
				info->CloseFileHandle();
			}
		}
		else
		{
			break;	// end thread when file I/O completed.
		}
	}

	std::printf("Thread end (ID: %d)\n", GetCurrentThreadId());
	return 0;
}

UINT __stdcall IOThreadPool::workerAlwaysCheck(LPVOID pParam)
{
	IOThreadPool* pPool = static_cast<IOThreadPool*>(pParam);
	DWORD dwTransfer = 0;
	ULONG_PTR keyValue = 0;
	LPOVERLAPPED pOverlapped = { 0 };
	bool bAllProcessed = false;
	bool bAvailable = false;

	while (true)
	{
		if (pPool->_numReadFiles < pPool->_targetNumFiles)
		{
			size_t fileKey(SIZE_MAX);
			EnterCriticalSection(&pPool->_csCreateRead);
			bAvailable = (0 < pPool->_readQueue.size());
			if (true == bAvailable)
			{
				fileKey = pPool->_readQueue.front();
				pPool->_readQueue.pop();
				++pPool->_numReadFiles;
			}
			LeaveCriticalSection(&pPool->_csCreateRead);

			if (true == bAvailable)
			{
				pPool->threadCreateReadFile(fileKey);
			}
		}
		
		EnterCriticalSection(&pPool->_csProcess);
		bAllProcessed = pPool->_numProcessedFiles < pPool->_targetNumFiles;
		LeaveCriticalSection(&pPool->_csProcess);

		if (bAllProcessed)
		{
			bAvailable = GetQueuedCompletionStatus(pPool->_completionPort, &dwTransfer, &keyValue, &pOverlapped, 0);
			if (true == bAvailable)
			{
				EnterCriticalSection(&pPool->_csProcess);
				++pPool->_numProcessedFiles;
				LeaveCriticalSection(&pPool->_csProcess);

				pPool->threadProcessFile(keyValue);

				EnterCriticalSection(&pPool->_csCreateRead);
				FileInfo* info = pPool->_fileInfoHashMap[keyValue];
				LeaveCriticalSection(&pPool->_csCreateRead);
				info->CloseFileHandle();
			}
		}
		else
		{
			break;	// end thread when file I/O completed.
		}
	}

	std::printf("Thread end (ID: %d)\n", GetCurrentThreadId());
	return 0;
}

UINT __stdcall IOThreadPool::workerCounting(LPVOID pParam)
{
	IOThreadPool* pPool = static_cast<IOThreadPool*>(pParam);
	DWORD dwTransfer = 0;
	ULONG_PTR keyValue = 0;
	LPOVERLAPPED pOverlapped = { 0 };
	bool bAllProcessed = false;
	bool bAvailable = false;

	size_t readCount = 0;
	size_t procCount = 0;

	while (true)
	{
		if (readCount < pPool->_maxReadProcCount)
		{
			if (pPool->_numReadFiles < pPool->_targetNumFiles)
			{
				size_t fileKey(SIZE_MAX);
				EnterCriticalSection(&pPool->_csCreateRead);
				bAvailable = (0 < pPool->_readQueue.size());
				if (true == bAvailable)
				{
					fileKey = pPool->_readQueue.front();
					pPool->_readQueue.pop();
					++pPool->_numReadFiles;
				}
				LeaveCriticalSection(&pPool->_csCreateRead);

				if (true == bAvailable)
				{
					pPool->threadCreateReadFile(fileKey);
				}

				++readCount;
			}
			else
			{
				readCount = pPool->_maxReadProcCount;
			}
		}
		else
		{
			EnterCriticalSection(&pPool->_csProcess);
			bAllProcessed = pPool->_numProcessedFiles < pPool->_targetNumFiles;
			LeaveCriticalSection(&pPool->_csProcess);

			if (bAllProcessed)
			{
				bAvailable = GetQueuedCompletionStatus(pPool->_completionPort, &dwTransfer, &keyValue, &pOverlapped, 0);
				if (true == bAvailable)
				{
					EnterCriticalSection(&pPool->_csProcess);
					++pPool->_numProcessedFiles;
					LeaveCriticalSection(&pPool->_csProcess);

					pPool->threadProcessFile(keyValue);

					EnterCriticalSection(&pPool->_csCreateRead);
					FileInfo* info = pPool->_fileInfoHashMap[keyValue];
					LeaveCriticalSection(&pPool->_csCreateRead);
					info->CloseFileHandle();
				}

				++procCount;
				if (pPool->_maxReadProcCount - 1 < procCount)
				{
					procCount = 0;
					readCount = 0;
				}
			}
			else
			{
				break;	// end thread when file I/O completed.
			}
		}
	}

	std::printf("Thread end (ID: %d)\n", GetCurrentThreadId());
	return 0;
}

UINT __stdcall IOThreadPool::workerCreateAllReadAllProcess(LPVOID pParam)
{
	IOThreadPool* pPool = static_cast<IOThreadPool*>(pParam);
	DWORD dwTransfer = 0;
	ULONG_PTR keyValue = 0;
	LPOVERLAPPED pOverlapped = { 0 };
	bool bAllProcessed = false;
	bool bAvailable = false;

	LARGE_INTEGER startCreateTime;
	LARGE_INTEGER endCreateTime;
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&startCreateTime);
	while (true)
	{
		if (pPool->_numCreatedFiles < pPool->_targetNumFiles)
		{
			size_t fileKey(SIZE_MAX);
			EnterCriticalSection(&pPool->_csCreateRead);
			bAvailable = (0 < pPool->_createQueue.size());
			if (true == bAvailable)
			{
				fileKey = pPool->_createQueue.front();
				pPool->_createQueue.pop();
				++pPool->_numCreatedFiles;
			}
			LeaveCriticalSection(&pPool->_csCreateRead);

			if (true == bAvailable)
			{
				pPool->threadOnlyCreateFile(fileKey);
			}
		}
		else
		{
			break;
		}
	}
	QueryPerformanceCounter(&endCreateTime);
	double createElapsedTime = static_cast<double>(endCreateTime.QuadPart - startCreateTime.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
	std::printf("CreateFile elapsed time: %lf\n", createElapsedTime);

	while (true)
	{
		EnterCriticalSection(&pPool->_csProcess);
		bAllProcessed = pPool->_numProcessedFiles < pPool->_targetNumFiles;
		LeaveCriticalSection(&pPool->_csProcess);

		if (pPool->_numReadFiles < pPool->_targetNumFiles)
		{
			size_t fileKey(SIZE_MAX);
			EnterCriticalSection(&pPool->_csCreateRead);
			bAvailable = (0 < pPool->_readQueue.size());
			if (true == bAvailable)
			{
				fileKey = pPool->_readQueue.front();
				pPool->_readQueue.pop();
				++pPool->_numReadFiles;
			}
			LeaveCriticalSection(&pPool->_csCreateRead);

			if (true == bAvailable)
			{
				pPool->threadOnlyReadFile(fileKey);
			}
		}
		else if (bAllProcessed)
		{
			bAvailable = GetQueuedCompletionStatus(pPool->_completionPort, &dwTransfer, &keyValue, &pOverlapped, 0);
			if (true == bAvailable)
			{
				EnterCriticalSection(&pPool->_csProcess);
				++pPool->_numProcessedFiles;
				LeaveCriticalSection(&pPool->_csProcess);

				pPool->threadProcessFile(keyValue);

				EnterCriticalSection(&pPool->_csCreateRead);
				FileInfo* info = pPool->_fileInfoHashMap[keyValue];
				LeaveCriticalSection(&pPool->_csCreateRead);
				info->CloseFileHandle();
			}
		}
		else
		{
			break;	// end thread when file I/O completed.
		}
	}

	std::printf("Thread end (ID: %d)\n", GetCurrentThreadId());
	return 0;
}

UINT __stdcall IOThreadPool::workerCreateAllAlwaysCheck(LPVOID pParam)
{
	IOThreadPool* pPool = static_cast<IOThreadPool*>(pParam);
	DWORD dwTransfer = 0;
	ULONG_PTR keyValue = 0;
	LPOVERLAPPED pOverlapped = { 0 };
	bool bAllProcessed = false;
	bool bAvailable = false;

	LARGE_INTEGER startCreateTime;
	LARGE_INTEGER endCreateTime;
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&startCreateTime);
	while (true)
	{
		if (pPool->_numCreatedFiles < pPool->_targetNumFiles)
		{
			size_t fileKey(SIZE_MAX);
			EnterCriticalSection(&pPool->_csCreateRead);
			bAvailable = (0 < pPool->_createQueue.size());
			if (true == bAvailable)
			{
				fileKey = pPool->_createQueue.front();
				pPool->_createQueue.pop();
				++pPool->_numCreatedFiles;
			}
			LeaveCriticalSection(&pPool->_csCreateRead);

			if (true == bAvailable)
			{
				pPool->threadOnlyCreateFile(fileKey);
			}
		}
		else
		{
			break;
		}
	}
	QueryPerformanceCounter(&endCreateTime);
	double createElapsedTime = static_cast<double>(endCreateTime.QuadPart - startCreateTime.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
	std::printf("CreateFile elapsed time: %lf\n", createElapsedTime);

	while (true)
	{
		if (pPool->_numReadFiles < pPool->_targetNumFiles)
		{
			size_t fileKey(SIZE_MAX);
			EnterCriticalSection(&pPool->_csCreateRead);
			bAvailable = (0 < pPool->_readQueue.size());
			if (true == bAvailable)
			{
				fileKey = pPool->_readQueue.front();
				pPool->_readQueue.pop();
				++pPool->_numReadFiles;
			}
			LeaveCriticalSection(&pPool->_csCreateRead);

			if (true == bAvailable)
			{
				pPool->threadOnlyReadFile(fileKey);
			}
		}

		EnterCriticalSection(&pPool->_csProcess);
		bAllProcessed = pPool->_numProcessedFiles < pPool->_targetNumFiles;
		LeaveCriticalSection(&pPool->_csProcess);

		if (bAllProcessed)
		{
			bAvailable = GetQueuedCompletionStatus(pPool->_completionPort, &dwTransfer, &keyValue, &pOverlapped, 0);
			if (true == bAvailable)
			{
				EnterCriticalSection(&pPool->_csProcess);
				++pPool->_numProcessedFiles;
				LeaveCriticalSection(&pPool->_csProcess);

				pPool->threadProcessFile(keyValue);

				EnterCriticalSection(&pPool->_csCreateRead);
				FileInfo* info = pPool->_fileInfoHashMap[keyValue];
				LeaveCriticalSection(&pPool->_csCreateRead);
				info->CloseFileHandle();
			}
		}
		else
		{
			break;	// end thread when file I/O completed.
		}
	}

	std::printf("Thread end (ID: %d)\n", GetCurrentThreadId());
	return 0;
}

UINT __stdcall IOThreadPool::workerOnlyRead(LPVOID pParam)
{
	IOThreadPool* pPool = static_cast<IOThreadPool*>(pParam);
	bool bAvailable = false;

	while (true)
	{
		if (pPool->_numReadFiles < pPool->_targetNumFiles)
		{
			size_t fileKey(SIZE_MAX);
			EnterCriticalSection(&pPool->_csCreateRead);
			bAvailable = (0 < pPool->_readQueue.size());
			if (true == bAvailable)
			{
				fileKey = pPool->_readQueue.front();
				pPool->_readQueue.pop();
				++pPool->_numReadFiles;
			}
			LeaveCriticalSection(&pPool->_csCreateRead);

			if (true == bAvailable)
			{
				pPool->threadCreateReadFile(fileKey);
			}
		}
		else
		{
			break;
		}
	}

	std::printf("Thread end (ID: %d)\n", GetCurrentThreadId());
	return 0;
}

UINT __stdcall IOThreadPool::workerOnlyProcess(LPVOID pParam)
{
	IOThreadPool* pPool = static_cast<IOThreadPool*>(pParam);
	DWORD dwTransfer = 0;
	ULONG_PTR keyValue = 0;
	LPOVERLAPPED pOverlapped = { 0 };
	bool bAllProcessed = false;
	bool bAvailable = false;

	while (true)
	{
		EnterCriticalSection(&pPool->_csProcess);
		bAllProcessed = pPool->_numProcessedFiles < pPool->_targetNumFiles;
		LeaveCriticalSection(&pPool->_csProcess);

		if (bAllProcessed)
		{
			bAvailable = GetQueuedCompletionStatus(pPool->_completionPort, &dwTransfer, &keyValue, &pOverlapped, 0);
			if (true == bAvailable)
			{
				EnterCriticalSection(&pPool->_csProcess);
				++pPool->_numProcessedFiles;
				LeaveCriticalSection(&pPool->_csProcess);

				pPool->threadProcessFile(keyValue);

				EnterCriticalSection(&pPool->_csCreateRead);
				FileInfo* info = pPool->_fileInfoHashMap[keyValue];
				LeaveCriticalSection(&pPool->_csCreateRead);
				info->CloseFileHandle();
			}
		}
		else
		{
			break;	// end thread when file I/O completed.
		}
	}

	std::printf("Thread end (ID: %d)\n", GetCurrentThreadId());
	return 0;
}

UINT __stdcall IOThreadPool::workerSyncIO(LPVOID pParam)
{
	IOThreadPool* pPool = static_cast<IOThreadPool*>(pParam);
	bool bAllProcessed = false;
	bool bAvailable = false;

	while (true)
	{
		EnterCriticalSection(&pPool->_csCreateRead);
		bAllProcessed = pPool->_numProcessedFiles < pPool->_targetNumFiles;
		LeaveCriticalSection(&pPool->_csCreateRead);

		if (bAllProcessed)
		{
			size_t fileKey(SIZE_MAX);
			EnterCriticalSection(&pPool->_csCreateRead);
			bAvailable = (0 < pPool->_readQueue.size());
			if (true == bAvailable)
			{
				fileKey = pPool->_readQueue.front();
				pPool->_readQueue.pop();
				++pPool->_numReadFiles;
				++pPool->_numProcessedFiles;
			}
			LeaveCriticalSection(&pPool->_csCreateRead);

			if (true == bAvailable)
			{
				pPool->threadSyncIO(fileKey);
			}
		}
		else
		{
			break;
		}
	}

	std::printf("Thread end (ID: %d)\n", GetCurrentThreadId());
	return 0;
}

void IOThreadPool::threadCreateReadFile(const size_t& fileKey)
{
	EnterCriticalSection(&_csCreateRead);
	FileInfo* pFile = _fileInfoHashMap[fileKey];
	LeaveCriticalSection(&_csCreateRead);

	span* createFileSpan = new span(g_createSeries, 0, _T("CreateFile"));
	HANDLE hFile = CreateFile(pFile->GetFilePath().c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING, NULL);
	delete createFileSpan;
	if (hFile == INVALID_HANDLE_VALUE)
	{
		std::printf("ERROR: CreateFile failed\tError Code: %d\n", GetLastError());
		return;
	}

	// save file handle
	pFile->SetFileHandle(hFile);

	//span* createIOCP = new span(g_series, 0, _T("CreateIoCompletionPort"));
	HANDLE hIOCP = CreateIoCompletionPort(hFile, _completionPort, fileKey, NULL);
	//HANDLE hIOCP = CreateIoCompletionPort(hFile, _completionPort, NULL, NULL);
	//delete createIOCP;
	if (hIOCP != _completionPort)
	{
		std::printf("ERROR: CreateIoCompletionPort(existing) failed\tError Code: %d\n", GetLastError());
		return;
	}

	LARGE_INTEGER fileSize;
	GetFileSizeEx(hFile, &fileSize);
	if (0 == fileSize.QuadPart % 512)
	{
		pFile->ReserveBuffer(fileSize.QuadPart);
	}
	else
	{
		pFile->ReserveBuffer(fileSize.QuadPart / 512 * 512 + 512);
	}

	DWORD dwRead;

	span* readFileSpan = new span(g_readSeries, 1, _T("ReadFile %dKB"), fileSize.QuadPart / 1024);
	BOOL bReadResult = ReadFile(hFile, pFile->GetBuffer(), pFile->GetBufferSize(), &dwRead, &pFile->Overlapped());
	delete readFileSpan;
	if (FALSE == bReadResult)
	{
		if (GetLastError() != ERROR_IO_PENDING)
		{
			std::printf("ERROR: ReadFile failed\tError Code: %d\n", GetLastError());
			return;
		}
	}

	pFile->SetBoolHasRead(true);
}

void IOThreadPool::threadOnlyCreateFile(const size_t& fileKey)
{
	EnterCriticalSection(&_csCreateRead);
	FileInfo* pFile = _fileInfoHashMap[fileKey];
	LeaveCriticalSection(&_csCreateRead);

	span* createFileSpan = new span(g_createSeries, 0, _T("CreateFile"));
	HANDLE hFile = CreateFile(pFile->GetFilePath().c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING, NULL);
	delete createFileSpan;
	if (hFile == INVALID_HANDLE_VALUE)
	{
		std::printf("ERROR: CreateFile failed\tError Code: %d\n", GetLastError());
		return;
	}

	// save file handle
	pFile->SetFileHandle(hFile);

	//span* createIOCP = new span(g_series, 0, _T("CreateIoCompletionPort"));
	HANDLE hIOCP = CreateIoCompletionPort(hFile, _completionPort, fileKey, NULL);
	//HANDLE hIOCP = CreateIoCompletionPort(hFile, _completionPort, NULL, NULL);
	//delete createIOCP;
	if (hIOCP != _completionPort)
	{
		std::printf("ERROR: CreateIoCompletionPort(existing) failed\tError Code: %d\n", GetLastError());
		return;
	}

	EnterCriticalSection(&_csCreateRead);
	_readQueue.push(fileKey);
	LeaveCriticalSection(&_csCreateRead);
}

void IOThreadPool::threadOnlyReadFile(const size_t& fileKey)
{
	EnterCriticalSection(&_csCreateRead);
	FileInfo* pFile = _fileInfoHashMap[fileKey];
	LeaveCriticalSection(&_csCreateRead);

	LARGE_INTEGER fileSize;
	GetFileSizeEx(pFile->GetFileHandle(), &fileSize);
	if (0 == fileSize.QuadPart % 512)
	{
		pFile->ReserveBuffer(fileSize.QuadPart);
	}
	else
	{
		pFile->ReserveBuffer(fileSize.QuadPart / 512 * 512 + 512);
	}

	DWORD dwRead;

	span* readFileSpan = new span(g_readSeries, 1, _T("ReadFile %dKB"), fileSize.QuadPart / 1024);
	BOOL bReadResult = ReadFile(pFile->GetFileHandle(), pFile->GetBuffer(), pFile->GetBufferSize(), &dwRead, &pFile->Overlapped());
	delete readFileSpan;
	if (FALSE == bReadResult)
	{
		if (GetLastError() != ERROR_IO_PENDING)
		{
			std::printf("ERROR: ReadFile failed\tError Code: %d\n", GetLastError());
			return;
		}
	}

	pFile->SetBoolHasRead(true);
}

void IOThreadPool::threadProcessFile(const size_t& fileKey)
{
	EnterCriticalSection(&_csCreateRead);
	FileInfo* pFile = _fileInfoHashMap[fileKey];
	LeaveCriticalSection(&_csCreateRead);

	std::string processingTimeStr;
	size_t i = 0;
	while ('\n' != pFile->GetBuffer()[i])
	{
		processingTimeStr.push_back(pFile->GetBuffer()[i]);
		++i;
	}
	double processingTime = std::stod(processingTimeStr);
	
	LARGE_INTEGER procStartTime;
	LARGE_INTEGER procCheckTime;
	LARGE_INTEGER frequency;
	QueryPerformanceCounter(&procStartTime);
	QueryPerformanceFrequency(&frequency);

	span* processingSpan = new span(g_processSeries, 2, _T("Processing %lf ms"), processingTime);
	while (true)
	{
		QueryPerformanceCounter(&procCheckTime);
		double dt = (static_cast<double>(procCheckTime.QuadPart - procStartTime.QuadPart) * 1000.0) / static_cast<double>(frequency.QuadPart);
		if (processingTime < dt)
		{
			break;
		}

		// processing
		{
			// memory allocation
			char* temp = new char[8];
			delete[] temp;
		}

		//{
		//	// find file (system call)
		//	intptr_t hFile;
		//	_finddata_t c_file;
		//	hFile = _findfirst(pFile->GetFilePath().string().c_str(), &c_file);
		//	_findclose(hFile);
		//}
	}
	delete processingSpan;

	pFile->SetBoolHasProcessed(true);
	pFile->ReleaseBuffer();
}

void IOThreadPool::threadSyncIO(const size_t& fileKey)
{
	// reading
	EnterCriticalSection(&_csCreateRead);
	FileInfo* pFile = _fileInfoHashMap[fileKey];
	LeaveCriticalSection(&_csCreateRead);

	span* createFileSpan = new span(g_createSeries, 0, _T("CreateFile"));
	HANDLE hFile = CreateFile(pFile->GetFilePath().c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
	delete createFileSpan;
	if (hFile == INVALID_HANDLE_VALUE)
	{
		std::printf("ERROR: CreateFile failed\tError Code: %d\n", GetLastError());
		return;
	}

	LARGE_INTEGER fileSize;
	GetFileSizeEx(hFile, &fileSize);
	if (0 == fileSize.QuadPart % 512)
	{
		pFile->ReserveBuffer(fileSize.QuadPart);
	}
	else
	{
		pFile->ReserveBuffer(fileSize.QuadPart / 512 * 512 + 512);
	}

	DWORD dwRead;

	span* readFileSpan = new span(g_readSeries, 1, _T("ReadFile %dKB"), fileSize.QuadPart / 1024);
	BOOL bReadResult = ReadFile(hFile, pFile->GetBuffer(), pFile->GetBufferSize(), &dwRead, NULL);
	delete readFileSpan;
	if (FALSE == bReadResult)
	{
		if (GetLastError() != ERROR_IO_PENDING)
		{
			std::printf("ERROR: ReadFile failed\tError Code: %d\n", GetLastError());
			return;
		}
	}

	WaitForSingleObject(hFile, INFINITE);

	pFile->SetBoolHasRead(true);

	// processing
	std::string processingTimeStr;
	size_t i = 0;
	while ('\n' != pFile->GetBuffer()[i])
	{
		processingTimeStr.push_back(pFile->GetBuffer()[i]);
		++i;
	}
	double processingTime = std::stod(processingTimeStr);

	LARGE_INTEGER procStartTime;
	LARGE_INTEGER procCheckTime;
	LARGE_INTEGER frequency;
	QueryPerformanceCounter(&procStartTime);
	QueryPerformanceFrequency(&frequency);

	span* processingSpan = new span(g_processSeries, 2, _T("Processing %lf ms"), processingTime);
	while (true)
	{
		QueryPerformanceCounter(&procCheckTime);
		double dt = (static_cast<double>(procCheckTime.QuadPart - procStartTime.QuadPart) * 1000.0) / static_cast<double>(frequency.QuadPart);
		if (processingTime < dt)
		{
			break;
		}

		// processing
		{
			// memory allocation
			char* temp = new char[8];
			delete[] temp;
		}

		//{
		//	// find file (system call)
		//	intptr_t hFile;
		//	_finddata_t c_file;
		//	hFile = _findfirst(pFile->GetFilePath().string().c_str(), &c_file);
		//	_findclose(hFile);
		//}
	}
	delete processingSpan;

	pFile->SetBoolHasProcessed(true);
	pFile->ReleaseBuffer();
}
