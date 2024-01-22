#pragma once
#include "Common.h"

class FileInfo final
{
public:
	FileInfo(std::filesystem::path filePath);
	FileInfo(const FileInfo& other) = delete;
	FileInfo& operator=(const FileInfo& other) = delete;
	~FileInfo();

	void								ReserveBuffer(LONGLONG size);
	void								ReleaseBuffer(void);

	//void								AddDependency(size_t fileKey);

	void								SetBoolHasRead(bool hasRead);
	void								SetBoolHasProcessed(bool hasProcessed);

	void								SetFileHandle(const HANDLE handle);
	void								CloseFileHandle(void);

	OVERLAPPED&							Overlapped(void);

	const std::filesystem::path&		GetFilePath(void) const;
	char*								GetBuffer(void);
	DWORD								GetBufferSize(void) const;

	//const std::vector<size_t>&		GetDependencies(void) const;

	const HANDLE&						GetFileHandle(void) const;

	bool								HasRead(void) const;
	bool								HasProcessed(void) const;

private:
	const std::filesystem::path			_filePath;
	char*								_readBuffer;
	DWORD								_bufferSize;

	//std::vector<size_t>				_dependencyFileKeys;

	HANDLE								_fileHandle;
	OVERLAPPED							_overlapped;

	bool								_bHasRead;
	bool								_bHasProcessed;
};