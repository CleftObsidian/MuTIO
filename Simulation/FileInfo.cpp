#include "FileInfo.h"

FileInfo::FileInfo(std::filesystem::path filePath)
	: _filePath(filePath)
	, _readBuffer(nullptr)
	, _bufferSize(0)
	//, _dependencyFileKeys()
	, _fileHandle(nullptr)
	, _overlapped()
	, _bHasRead(false)
	, _bHasProcessed(false)
{
}

FileInfo::~FileInfo()
{
	if (nullptr != _readBuffer)
	{
		delete[] _readBuffer;
	}
}

void FileInfo::ReserveBuffer(LONGLONG size)
{
	if (nullptr == _readBuffer)
	{
		_readBuffer = new char[size];
		_bufferSize = static_cast<DWORD>(size);
	}
}

void FileInfo::ReleaseBuffer(void)
{
	if (nullptr != _readBuffer)
	{
		delete[] _readBuffer;
		_readBuffer = nullptr;
		_bufferSize = 0;
	}
}

//void FileInfo::AddDependency(size_t fileKey)
//{
//	_dependencyFileKeys.push_back(fileKey);
//}

void FileInfo::SetBoolHasRead(bool hasRead)
{
	_bHasRead = hasRead;
}

void FileInfo::SetBoolHasProcessed(bool hasProcessed)
{
	_bHasProcessed = hasProcessed;
}

void FileInfo::SetFileHandle(const HANDLE handle)
{
	_fileHandle = handle;
}

void FileInfo::CloseFileHandle(void)
{
	CloseHandle(_fileHandle);
}

OVERLAPPED& FileInfo::Overlapped(void)
{
	return _overlapped;
}

const std::filesystem::path& FileInfo::GetFilePath(void) const
{
	return _filePath;
}

char* FileInfo::GetBuffer(void)
{
	return _readBuffer;
}

DWORD FileInfo::GetBufferSize(void) const
{
	return _bufferSize;
}

//const std::vector<size_t>& FileInfo::GetDependencies(void) const
//{
//	return _dependencyFileKeys;
//}

const HANDLE& FileInfo::GetFileHandle(void) const
{
	return _fileHandle;
}

bool FileInfo::HasRead(void) const
{
	return _bHasRead;
}

bool FileInfo::HasProcessed(void) const
{
	return _bHasProcessed;
}
