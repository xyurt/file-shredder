#include <stdio.h>
#include <inttypes.h>
#include <windows.h>

typedef struct {
    uint64_t *i;
    uint64_t i_len;
    uint64_t chunk_count;
    uint64_t chunk_size;
    uint64_t remainder_bytes;
    uint32_t gran;
    HANDLE hMap;
} OverwriteParams;

DWORD WINAPI OverwriteThread(LPVOID lpParam) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);

    OverwriteParams *params = (OverwriteParams *)lpParam;

    uint64_t *i = params->i;
    uint64_t i_len = params->i_len;
    uint64_t chunk_count = params->chunk_count;
    uint64_t chunk_size = params->chunk_size;
    uint64_t remainder_bytes = params->remainder_bytes;
    uint32_t gran = params->gran;
    HANDLE hMap = params->hMap;

    VirtualFree(params, NULL, MEM_RELEASE);

    static const size_t zeroBufSize = 1024 * 1024;
    static char zeroBuf[1024 * 1024] = { 0 };


    for (int k = 0; k < i_len; k++) {
        uint64_t startOffset = (uint64_t)chunk_size * i[k];
        uint64_t currentChunkSize = chunk_size;

        if (i[k] == chunk_count - 1 && remainder_bytes != 0) {
            currentChunkSize = remainder_bytes;
        }

        ULONGLONG alignedStart = startOffset - (startOffset % gran);
        uint64_t offsetDelta = (uint64_t)(startOffset - alignedStart);
        uint64_t totalMapSize = currentChunkSize + offsetDelta;

        char *pData = NULL;
        LPVOID pMap = MapViewOfFile(hMap, FILE_MAP_WRITE, (DWORD)(alignedStart >> 32), (DWORD)(alignedStart & 0xFFFFFFFF), totalMapSize);
        if (pMap == NULL) {
            printf("[ERROR] Address of the mapping was zero.\n");
            printf("[INFO] Last error: (%d)\n", GetLastError());
            VirtualFree(i, NULL, MEM_RELEASE);
            return FALSE;
        }
        pData = (char *)pMap + offsetDelta;
        printf("[INFO] Mapping chunk %" PRIu64 " at file offset 0x%llx with base address: (0x%p)\n", i[k], startOffset, pMap);

        size_t bytesLeft = currentChunkSize;
        char *dst = pData;
        while (bytesLeft > 0) {
            size_t chunk = (bytesLeft > zeroBufSize) ? zeroBufSize : bytesLeft;
            memcpy(dst, zeroBuf, chunk);
            dst += chunk;
            bytesLeft -= chunk;
        }
        //printf("[INFO] Set all the bytes to 0x00000000.\n");

        if (UnmapViewOfFile(pMap) == FALSE) {
            printf("[WARNING] Unmapping operation of the chunk returned false.\n");
            printf("[INFO] Last error: (%d)\n", GetLastError());
        }
        else {
            //printf("[INFO] The chunk map has been unmapped.\n");
        }

        Sleep(0);
    }

    VirtualFree(i, NULL, MEM_RELEASE);

    return TRUE;
}

int wmain(int argc, wchar_t *argv[]) {
    if (argc != 4) {
        wprintf(L"Usage: %s *[filename] *[max_thread] *[chunk_size_mb]\n\tmax_thread: Max thread count the program can use [max] for maximum.\n\tchunk_size_mb: Chunk size to overwrite in megabytes", argv[0]);
        return -1;
    }

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    uint64_t max_thread = 1;
    if (_wcsnicmp(argv[2], L"max", 3) == 0) {
        max_thread = sysInfo.dwNumberOfProcessors;
    }
    else {
        max_thread = _wcstoui64(argv[2], NULL, 10);
    }

    printf("[INFO] Max threads set to %llu\n", max_thread);


    uint64_t chunk_size = 0;
    if (_wcsnicmp(argv[3], L"max", 3) != 0) {
        chunk_size = 1024 * 1024 * _wcstoui64(argv[3], NULL, 10);
    }
    else {
        chunk_size = 1024 * 1024 * 512;
    }

    printf("[INFO] Chunk size set to %llu\n", chunk_size);

    if (max_thread * chunk_size > 1024 * 1024 * 512) {
        printf("[INFO] The program will use more than 512MB of virtual memory at best. This could potentially crash your system.\nPress any key to continue...\n");
        getchar();
    }

    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    printf("[INFO] The filename is %ls\n", argv[1]);

    HANDLE hFile = CreateFile(argv[1], GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        printf("[ERROR] Handle of the file was invalid.\n");
        printf("[INFO] Last error: (%d)\n", GetLastError());
        return -1;
    }
    printf("[INFO] Opened the file handle: (0x%p)\n", (void *)hFile);

    LARGE_INTEGER fileSize;
    GetFileSizeEx(hFile, &fileSize);

    if (fileSize.QuadPart != 0) {
        printf("[INFO] File size is %llu.\n", fileSize.QuadPart);

        HANDLE hMap = CreateFileMapping(hFile, NULL, PAGE_READWRITE, 0, 0, NULL);
        if (hMap == NULL) {
            printf("[ERROR] Handle of the mapping was invalid.\n");
            printf("[INFO] Last error: (%d)\n", GetLastError());
            return -1;
        }
        printf("[INFO] Created a file mapping handle: (0x%p)\n", (void *)hMap);

        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        DWORD gran = sysInfo.dwAllocationGranularity;

        uint64_t chunk_count = (uint64_t)(fileSize.QuadPart / chunk_size);
        uint64_t remainder_bytes = (uint64_t)(fileSize.QuadPart - (uint64_t)(chunk_count * chunk_size));

        printf("[DEBUG] Chunk size is %llu bytes.\n", chunk_size);
        printf("[DEBUG] Chunk count is %llu.\n", chunk_count);
        printf("[DEBUG] There are total %llu remainder bytes to be handled.\n", remainder_bytes);

        if (remainder_bytes != 0) {
            chunk_count++;
        }

        uint64_t used_threads = max_thread > chunk_count ? chunk_count : max_thread;
        uint64_t thread_chunk_count = (uint64_t)(chunk_count / used_threads);
        uint64_t remainder_chunk_count = chunk_count - (thread_chunk_count * used_threads);
        int remainder_added = 0;
        if (remainder_chunk_count != 0 && used_threads < max_thread) {
            used_threads++;
            remainder_added = 1;
        }

        printf("[DEBUG] max_thread = %llu\n", max_thread);
        printf("[DEBUG] used_threads = %llu\n", used_threads);
        printf("[DEBUG] thread_chunk_count = %llu\n", thread_chunk_count);
        printf("[DEBUG] remainder_chunk_count = %llu\n", remainder_chunk_count);

        HANDLE *thread_list = malloc(sizeof(HANDLE) * used_threads);
        if (thread_list == NULL) {
            printf("[ERROR] Failed to allocate memory.\n");
            printf("[INFO] Last error: (%d)\n", GetLastError());
            return -1;
        }

        uint64_t chunk_index = 0;
        uint64_t i = 0;
        while (i < used_threads && chunk_index < chunk_count) {
            // the thread will do [thread_chunk_count] amount of chunks
            OverwriteParams *params = VirtualAlloc(NULL, sizeof(OverwriteParams), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (params == NULL) {
                printf("[ERROR] Failed to allocate memory.\n");
                printf("[INFO] Last error: (%d)\n", GetLastError());
                i++;
                continue;
            }

            uint64_t current_tc_count = thread_chunk_count;
            if (i == used_threads - 1 && remainder_added == 0) {
                current_tc_count += remainder_chunk_count;
            }

            params->i = VirtualAlloc(NULL, current_tc_count * sizeof(uint64_t), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (params->i == NULL) {
                free(params);
                printf("[ERROR] Failed to allocate memory.\n");
                printf("[INFO] Last error: (%d)\n", GetLastError());
                i++;
                continue;
            }

            for (int k = 0; k < current_tc_count; k++) {
                params->i[k] = chunk_index++;
                if (chunk_index >= chunk_count) {
                    current_tc_count = k + 1;
                    break;
                }
            }

            params->i_len = current_tc_count;
            params->chunk_count = chunk_count;
            params->chunk_size = chunk_size;
            params->remainder_bytes = remainder_bytes;
            params->gran = gran;
            params->hMap = hMap;

            thread_list[i] = CreateThread(NULL, 0, OverwriteThread, params, CREATE_SUSPENDED, NULL);
            if (thread_list[i] == NULL) {
                printf("[ERROR] Failed to create thread %llu\n", i);
                printf("[INFO] Last error: (%d)\n", GetLastError());
                free(params->i);
                free(params);
                Sleep(10);
                continue;
            }

            i++;
        }

        for (uint64_t i = 0; i < used_threads; i++) {
            ResumeThread(thread_list[i]);
        }

        for (uint64_t i = 0; i < used_threads; i++) {
            WaitForSingleObject(thread_list[i], INFINITE);
            CloseHandle(thread_list[i]);
        }

        free(thread_list);

        printf("[INFO] File overwriting operation ended.\n");

        if (CloseHandle(hMap) == FALSE) {
            printf("[WARNING] Close operation of the map handle returned false.\n");
            printf("[INFO] Last error: (%d)\n", GetLastError());
        } else {
            printf("[INFO] Map handle has been closed.\n");
        }

        //if (FlushFileBuffers(hFile) == FALSE) {
        //    printf("[WARNING] FlushFileBuffers failed: (%d)\n", GetLastError());
        //}
        printf("[INFO] Flushed the file buffers.\n");
    } else {
        printf("[INFO] The file is zero bytes in size, skipping the overwriting phase.\n");
    }

    if (CloseHandle(hFile) == FALSE) {
        printf("[WARNING] Close operation of the file handle returned false.\n");
        printf("[INFO] Last error: (%d)\n", GetLastError());
    } else {
        printf("[INFO] The file handle has been closed.\n");
    }

    if (DeleteFile(argv[1])) {
        printf("[INFO] File deleted.\n");
    } else {
        printf("[ERROR] Failed to delete the file.\n");
        printf("[INFO] Last error: (%d)\n", GetLastError());
    }

    QueryPerformanceCounter(&end);

    double performance_ms = 1000.0 * (end.QuadPart - start.QuadPart) / freq.QuadPart;
    printf("[PERF] Operation finished in %.2f ms\n", performance_ms);

	return 0;
}