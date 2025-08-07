/* MIT License
 *
 * Copyright (c) 2025 8891689
 *                     https://github.com/8891689
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * Key Optimizations:
 * - Multi-threading for both sequential and random modes.
 * - Efficient two-pass file indexing with pre-calculated line info.
 * - High-speed Xoshiro256** PRNG instead of slow OpenSSL RAND_bytes.
 * - "Odometer" algorithm for sequential generation to avoid big integer math.
 * - Large, per-thread write buffers to minimize I/O syscalls and lock contention.
 *
 * Compile with:
 * gcc -O3 -march=native combination_dictionary.c -o cd -lpthread -static
 *
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>

// --- Platform-Specific Includes ---
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#endif


// =================================================================
//                      TYPE DEFINITIONS
// =================================================================
typedef unsigned __int128 u128;

typedef struct {
    char* start;
    size_t len;
} LineInfo;

typedef struct {
    char* data;
    size_t size;
    size_t line_count;
    LineInfo* lines;
#ifdef _WIN32
    HANDLE hFile;
    HANDLE hMapFile;
#endif
} MappedFile;

typedef struct { uint64_t s[4]; } Xoshiro256StarStar;
typedef enum { MODE_SEQUENTIAL, MODE_RANDOM } GenerationMode;
typedef struct {
    u128 start_index;
    u128 count;
    const MappedFile* prefix_file;
    const MappedFile* suffix_file;
    GenerationMode mode;
    bool infinite;
    FILE* output_file;
    pthread_mutex_t* mutex;
} ThreadData;


// =================================================================
//                      PRNG (Xoshiro256**)
// =================================================================
static inline uint64_t rotl(const uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

uint64_t xoshiro_next(Xoshiro256StarStar* g) {
    const uint64_t result = rotl(g->s[1] * 5, 7) * 9;
    const uint64_t t = g->s[1] << 17;
    g->s[2] ^= g->s[0]; g->s[3] ^= g->s[1]; g->s[1] ^= g->s[2]; g->s[0] ^= g->s[3];
    g->s[2] ^= t; g->s[3] = rotl(g->s[3], 45);
    return result;
}

void xoshiro_seed(Xoshiro256StarStar* g, uint64_t seed) {
    for (int i = 0; i < 4; ++i) {
        uint64_t x = (seed += 0x9e3779b97f4a7c15);
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9;
        x = (x ^ (x >> 27)) * 0x94d049bb133111eb;
        g->s[i] = x ^ (x >> 31);
    }
}

static inline size_t fast_map_to_range(uint64_t rand64, size_t range) {
    return (size_t)(((unsigned __int128)rand64 * range) >> 64);
}


// =================================================================
//                CROSS-PLATFORM FILE MAPPING
// =================================================================
#ifdef _WIN32
// --- Windows Implementation ---
MappedFile* map_and_index_file(const char *filename) {
    HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { return NULL; }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize)) { CloseHandle(hFile); return NULL; }
    if (fileSize.QuadPart == 0) { CloseHandle(hFile); return NULL; }

    HANDLE hMapFile = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hMapFile == NULL) { CloseHandle(hFile); return NULL; }

    char* data = (char*)MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, fileSize.QuadPart);
    if (data == NULL) { CloseHandle(hMapFile); CloseHandle(hFile); return NULL; }

    size_t line_count = 0;
    for (LONGLONG i = 0; i < fileSize.QuadPart; i++) {
        if (data[i] == '\n') line_count++;
    }
    if (fileSize.QuadPart > 0 && data[fileSize.QuadPart - 1] != '\n') line_count++;
    if (line_count == 0) { UnmapViewOfFile(data); CloseHandle(hMapFile); CloseHandle(hFile); return NULL; }

    MappedFile* mf = malloc(sizeof(MappedFile));
    mf->lines = malloc(line_count * sizeof(LineInfo));
    mf->data = data;
    mf->size = fileSize.QuadPart;
    mf->line_count = line_count;
    mf->hFile = hFile;
    mf->hMapFile = hMapFile;

    size_t current_line = 0;
    char* line_start = data;
    for (LONGLONG i = 0; i < fileSize.QuadPart; i++) {
        if (data[i] == '\n') {
            mf->lines[current_line].start = line_start;
            mf->lines[current_line].len = &data[i] - line_start;
            line_start = &data[i] + 1;
            current_line++;
        }
    }
    if (line_start < data + mf->size) {
        mf->lines[current_line].start = line_start;
        mf->lines[current_line].len = (data + mf->size) - line_start;
    }
    return mf;
}

void free_mapped_file(MappedFile *mf) {
    if (!mf) return;
    UnmapViewOfFile(mf->data);
    CloseHandle(mf->hMapFile);
    CloseHandle(mf->hFile);
    free(mf->lines);
    free(mf);
}

#else
// --- POSIX (Linux/macOS) Implementation ---
MappedFile* map_and_index_file(const char *filename) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) { return NULL; }

    struct stat st;
    if (fstat(fd, &st) == -1) { close(fd); return NULL; }
    if (st.st_size == 0) { close(fd); return NULL; }
    
    char* data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (data == MAP_FAILED) { return NULL; }

    size_t line_count = 0;
    for (size_t i = 0; i < st.st_size; i++) {
        if (data[i] == '\n') line_count++;
    }
    if (st.st_size > 0 && data[st.st_size - 1] != '\n') line_count++;
    if (line_count == 0) { munmap(data, st.st_size); return NULL; }

    MappedFile* mf = malloc(sizeof(MappedFile));
    if (!mf) { munmap(data, st.st_size); return NULL; }
    mf->lines = malloc(line_count * sizeof(LineInfo));
    if (!mf->lines) { free(mf); munmap(data, st.st_size); return NULL; }

    mf->data = data;
    mf->size = st.st_size;
    mf->line_count = line_count;

    size_t current_line = 0;
    char* line_start = data;
    for (size_t i = 0; i < st.st_size; i++) {
        if (data[i] == '\n') {
            mf->lines[current_line].start = line_start;
            mf->lines[current_line].len = &data[i] - line_start;
            line_start = &data[i] + 1;
            current_line++;
        }
    }
    if (line_start < data + st.st_size) {
        mf->lines[current_line].start = line_start;
        mf->lines[current_line].len = (data + st.st_size) - line_start;
    }
    return mf;
}

void free_mapped_file(MappedFile *mf) {
    if (!mf) return;
    munmap(mf->data, mf->size);
    free(mf->lines);
    free(mf);
}
#endif


// =================================================================
//                 CORE GENERATION WORKER THREAD
// =================================================================
void* generation_worker(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    const size_t P_COUNT = data->prefix_file->line_count;
    const size_t S_COUNT = data->suffix_file->line_count;

    const size_t BUFFER_SIZE = 4 * 1024 * 1024;
    char* write_buffer = malloc(BUFFER_SIZE);
    if (!write_buffer) return NULL;
    size_t buffer_offset = 0;
    
    if (data->mode == MODE_RANDOM) {
        Xoshiro256StarStar rng;
        xoshiro_seed(&rng, (uint64_t)time(NULL) ^ (uint64_t)pthread_self());
        for (u128 i = 0; data->infinite || i < data->count; ++i) {
            size_t p_idx = fast_map_to_range(xoshiro_next(&rng), P_COUNT);
            size_t s_idx = fast_map_to_range(xoshiro_next(&rng), S_COUNT);
            const LineInfo* p_line = &data->prefix_file->lines[p_idx];
            const LineInfo* s_line = &data->suffix_file->lines[s_idx];
            if (BUFFER_SIZE - buffer_offset < p_line->len + s_line->len + 1) {
                pthread_mutex_lock(data->mutex);
                fwrite(write_buffer, 1, buffer_offset, data->output_file);
                pthread_mutex_unlock(data->mutex);
                buffer_offset = 0;
            }
            memcpy(write_buffer + buffer_offset, p_line->start, p_line->len);
            buffer_offset += p_line->len;
            memcpy(write_buffer + buffer_offset, s_line->start, s_line->len);
            buffer_offset += s_line->len;
            write_buffer[buffer_offset++] = '\n';
        }
    } else {
        size_t p_idx = data->start_index / S_COUNT;
        size_t s_idx = data->start_index % S_COUNT;
        for (u128 i = 0; i < data->count; ++i) {
            const LineInfo* p_line = &data->prefix_file->lines[p_idx];
            const LineInfo* s_line = &data->suffix_file->lines[s_idx];
            if (BUFFER_SIZE - buffer_offset < p_line->len + s_line->len + 1) {
                pthread_mutex_lock(data->mutex);
                fwrite(write_buffer, 1, buffer_offset, data->output_file);
                pthread_mutex_unlock(data->mutex);
                buffer_offset = 0;
            }
            memcpy(write_buffer + buffer_offset, p_line->start, p_line->len);
            buffer_offset += p_line->len;
            memcpy(write_buffer + buffer_offset, s_line->start, s_line->len);
            buffer_offset += s_line->len;
            write_buffer[buffer_offset++] = '\n';
            if (++s_idx >= S_COUNT) { s_idx = 0; p_idx++; }
        }
    }
    
    if (buffer_offset > 0) {
        pthread_mutex_lock(data->mutex);
        fwrite(write_buffer, 1, buffer_offset, data->output_file);
        pthread_mutex_unlock(data->mutex);
    }
    free(write_buffer);
    return NULL;
}


// =================================================================
//                    MAIN FUNCTION & DISPATCHER
// =================================================================
void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s -c <prefix_file> -d <suffix_file> [OPTIONS]\n", program_name);
    fprintf(stderr, "A password generator that combines prefix and suffix passwords or words into a password.\n\n");
    fprintf(stderr, "Required:\n");
    fprintf(stderr, "  -c <file>   Path to the prefix file.\n");
    fprintf(stderr, "  -d <file>   Path to the suffix file.\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -R          Enable Random mode (runs infinitely). Default: sequential.\n");
    fprintf(stderr, "  -t <num>    Number of threads to use (default: 1).\n");
    fprintf(stderr, "  -h          Show this help message.\n");
    fprintf(stderr, "author:       https://github.com/8891689\n");
}

int main(int argc, char *argv[]) {
    const char *prefix_filename = NULL;
    const char *suffix_filename = NULL;
    bool random_mode = false;
    long num_threads = 1;

    // A simple manual parser for cross-platform compatibility (getopt is not standard on Windows)
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            prefix_filename = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            suffix_filename = argv[++i];
        } else if (strcmp(argv[i], "-R") == 0) {
            random_mode = true;
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            num_threads = atol(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (!prefix_filename || !suffix_filename) {
        print_usage(argv[0]);
        return 1;
    }
    if (num_threads <= 0) num_threads = 1;

    MappedFile *prefix = map_and_index_file(prefix_filename);
    if (!prefix) { fprintf(stderr, "Error: Failed to map and index prefix file: %s\n", prefix_filename); return 1; }
    
    MappedFile *suffix = map_and_index_file(suffix_filename);
    if (!suffix) { fprintf(stderr, "Error: Failed to map and index suffix file: %s\n", suffix_filename); free_mapped_file(prefix); return 1; }

    pthread_t* thread_ids = malloc(num_threads * sizeof(pthread_t));
    if (!thread_ids) goto cleanup_files;
    ThreadData* thread_data = malloc(num_threads * sizeof(ThreadData));
    if (!thread_data) { free(thread_ids); goto cleanup_files; }
    
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    
    GenerationMode mode = random_mode ? MODE_RANDOM : MODE_SEQUENTIAL;
    bool infinite_mode = (mode == MODE_RANDOM);
    u128 total_combinations = 0;
    
    if (mode == MODE_SEQUENTIAL) {
        total_combinations = (u128)prefix->line_count * suffix->line_count;
    }
    
    u128 per_thread = (infinite_mode || total_combinations == 0) ? 0 : total_combinations / num_threads;
    u128 remainder = (infinite_mode || total_combinations == 0) ? 0 : total_combinations % num_threads;
    u128 current_start_index = 0;

    for (int i = 0; i < num_threads; i++) {
        thread_data[i] = (ThreadData){
            .start_index = current_start_index,
            .count = per_thread + (i < remainder ? 1 : 0),
            .prefix_file = prefix,
            .suffix_file = suffix,
            .mode = mode,
            .infinite = infinite_mode,
            .output_file = stdout,
            .mutex = &mutex
        };
        if (mode == MODE_SEQUENTIAL) {
            current_start_index += thread_data[i].count;
        }
        if (pthread_create(&thread_ids[i], NULL, generation_worker, &thread_data[i]) != 0) {
            perror("pthread_create");
            goto cleanup_threads;
        }
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(thread_ids[i], NULL);
    }

cleanup_threads:
    pthread_mutex_destroy(&mutex);
    free(thread_ids);
    free(thread_data);
cleanup_files:
    free_mapped_file(prefix);
    free_mapped_file(suffix);
    return 0;
}
