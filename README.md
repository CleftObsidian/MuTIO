# MuTIO
Windows File I/O Performance Simulation with Multithreading  
It is tested on Windows 10, Visual Studio 19, C++17  
*[Concurrency Visualizer for Visual Studio 2019 SDK](https://learn.microsoft.com/en-us/visualstudio/profiling/concurrency-visualizer)* is used to check performance  
  
If you want more information go to the **[Wiki](https://github.com/CleftObsidian/MuTIO/wiki)**  

## Make dummy files before simulation
Need dummy files for simulation. You can make it with my project, [DummyFileGenerator](https://github.com/CleftObsidian/DummyFileGenerator).

## Arguments
* `NUM_THREADS` is number of threads for IOThreadPool class. The threads work differently depending on the THREAD_MODE.
* `NUM_FILES_TO_READ` is number of files to read.  
  If you use a value larger than the one created and retrieved from *[DummyFileGenerator](https://github.com/CleftObsidian/DummyFileGenerator)*, an infinite loop occurs.
* `MAX_READ_PROC_COUNT` determines how many times to read in succession and perform the operation.  
  This is only associated with `ThreadMode::READ_N_AND_PROCESS_N`.
* `THREAD_MODE` decides how IOThreadPool class will read the files. Please see below for details.

## ThreadMode
```
// Open and read all files, and then process
READ_ALL_AND_PROCESS_ALL,

// Alway check both queue to read, and to process
READ_CHECK_PROCESS_CHECK,

// Repeat send read call for N files, and process N files
READ_N_AND_PROCESS_N,

// Open all files first, and read all files, and then process
CREATE_ALL_AND_READ_ALL_AND_PROCESS_ALL,

// Open all files first, and check both queue to read, and to process
CREATE_ALL_AND_READ_CHECK_PROCESS_CHECK,

// One thread only sends read call, other threads only process
SINGLE_READ_ONLY_ELSE_PROCESS_ONLY,

// One thread sends read call first and then process, other threads only process
SINGLE_READ_ALL_AND_PROCESS_ELSE_PROCESS_ONLY,

// NOT overlapped I/O, each thread open, read, and process each file
SYNC_IO,
```
