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
 * Compile with:
 * gcc -O3 -march=native generator.c -o generator -lpthread -lm -static
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <math.h>

// --- Type Definitions ---
#define MAX_WORD_LENGTH 256
typedef unsigned __int128 u128;

typedef struct {
    char *word;
    size_t len;
} WordInfo;

typedef struct {
    uint64_t s[4];
} Xoshiro256StarStar;

typedef enum {
    MODE_SEQUENTIAL,
    MODE_RANDOM
} GenerationMode;

typedef struct {
    u128 startIndex;
    u128 count;
    int length;
    int numWords;
    WordInfo *words;
    bool noSpaces;
    bool infinite;
    GenerationMode mode;
    FILE *file;
    pthread_mutex_t *mutex;
    bool is_power_of_two;
    int shift_bits;
    uint32_t mask;
} ThreadData;


// --- Xoshiro256 PRNG ---
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
static inline uint32_t fast_map_to_range(uint64_t r, uint32_t range) {
    return (uint32_t)(((u128)r * range) >> 64);
}

// --- Core Generation Logic ---
static inline size_t generate_random_line(char *target_buffer, Xoshiro256StarStar *rng, const ThreadData *data) {
    size_t offset = 0;
    for (int k = 0; k < data->length; ++k) {
        const WordInfo *wi = &data->words[fast_map_to_range(xoshiro_next(rng), data->numWords)];
        memcpy(target_buffer + offset, wi->word, wi->len);
        offset += wi->len;
        if (!data->noSpaces && k < data->length - 1) {
            target_buffer[offset++] = ' ';
        }
    }
    return offset;
}

void *generation_thread_worker(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    const size_t BUFFER_SIZE = 4 * 1024 * 1024;
    const size_t MAX_LINE_SIZE = 2048;
    char *write_buffer = (char *)malloc(BUFFER_SIZE);
    if (!write_buffer) return NULL;
    
    size_t buffer_offset = 0;

    if (data->mode == MODE_RANDOM) {
        Xoshiro256StarStar rng;
        uint64_t seed = (uint64_t)time(NULL) ^ (uint64_t)pthread_self() ^ (uintptr_t)data;
        xoshiro_seed(&rng, seed);
        for (u128 i = 0; data->infinite || i < data->count; ++i) {
            if (BUFFER_SIZE - buffer_offset < MAX_LINE_SIZE) {
                pthread_mutex_lock(data->mutex);
                fwrite(write_buffer, 1, buffer_offset, data->file);
                pthread_mutex_unlock(data->mutex);
                buffer_offset = 0;
            }
            size_t bytes_written = generate_random_line(write_buffer + buffer_offset, &rng, data);
            buffer_offset += bytes_written;
            write_buffer[buffer_offset++] = '\n';
        }
    } else { 
        int indices[MAX_WORD_LENGTH];
        u128 temp_index = data->startIndex;

        if (data->is_power_of_two) {
            for (int k = data->length - 1; k >= 0; --k) {
                indices[k] = temp_index & data->mask;
                temp_index >>= data->shift_bits;
            }
        } else {
            for (int k = data->length - 1; k >= 0; --k) {
                indices[k] = temp_index % data->numWords;
                temp_index /= data->numWords;
            }
        }
        
        for (u128 i = 0; i < data->count; ++i) {
            if (BUFFER_SIZE - buffer_offset < MAX_LINE_SIZE) {
                pthread_mutex_lock(data->mutex);
                fwrite(write_buffer, 1, buffer_offset, data->file);
                pthread_mutex_unlock(data->mutex);
                buffer_offset = 0;
            }

            size_t current_offset = buffer_offset;
            for (int k = 0; k < data->length; ++k) {
                const WordInfo* wi = &data->words[indices[k]];
                memcpy(write_buffer + current_offset, wi->word, wi->len);
                current_offset += wi->len;
                if (!data->noSpaces && k < data->length - 1) {
                    write_buffer[current_offset++] = ' ';
                }
            }
            write_buffer[current_offset++] = '\n';
            buffer_offset = current_offset;

            for (int k = data->length - 1; k >= 0; --k) {
                if (++indices[k] < data->numWords) break;
                indices[k] = 0;
            }
        }
    }

    if (buffer_offset > 0) {
        pthread_mutex_lock(data->mutex);
        fwrite(write_buffer, 1, buffer_offset, data->file);
        pthread_mutex_unlock(data->mutex);
    }
    free(write_buffer);
    return NULL;
}

// =================================================================
//                 UTILITY & MAIN FUNCTION
// =================================================================

void print_usage(const char *pName) {
    fprintf(stderr, "Usage: %s -i <dict> -l <len|s-e> [OPTIONS]\n\n", pName);
    fprintf(stderr, "A silent, high-performance password generator for pipelining.\n\n");
    fprintf(stderr, "Required:\n");
    fprintf(stderr, "  -i <path>      Path to a character set or dictionary word file.\n");
    fprintf(stderr, "  -l <len|s-e>   Password or word length for (e.g., '12' or a range like '8-12').\n\n");
    fprintf(stderr, "Modes:\n");
    fprintf(stderr, "  (default)      Sequential Generation.\n");
    fprintf(stderr, "  -R             Random Generation.\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -n <number>    Number of items for Random Mode default: infinite.\n");
    fprintf(stderr, "  -t <threads>   Number of threads to use (default: 1).\n");
    fprintf(stderr, "  -o <file>      Output file path (default: stdout).\n");
    fprintf(stderr, "  -k             There is no space between the word and the password. result\n");
    fprintf(stderr, "  -h             Show this help message.\n\n");
    fprintf(stderr, "author:          https://github.com/8891689\n");
    fprintf(stderr, "Speed test: %s -i bip39.txt -l 10 -t 8 | pv > /dev/null\n", pName);
}


void read_dictionary(const char *filepath, WordInfo **words_ptr, int *count_ptr) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        exit(EXIT_FAILURE);
    }

    int capacity = 2048; 
    *count_ptr = 0;
    *words_ptr = (WordInfo *)malloc(capacity * sizeof(WordInfo));
    if (!*words_ptr) {
        exit(EXIT_FAILURE);
    }

    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), fp)) {
        size_t len = strcspn(buffer, "\r\n"); 
        if (len == 0) continue; 
        buffer[len] = '\0';

        if (*count_ptr >= capacity) {
            capacity *= 2;
            WordInfo *temp = (WordInfo *)realloc(*words_ptr, capacity * sizeof(WordInfo));
            if (!temp) {
                exit(EXIT_FAILURE);
            }
            *words_ptr = temp;
        }

        (*words_ptr)[*count_ptr].word = strdup(buffer);
        (*words_ptr)[*count_ptr].len = len;
        (*count_ptr)++;
    }
    fclose(fp);
}

void free_words(WordInfo *words, int count) {
    if (!words) return;
    for (int i = 0; i < count; i++) {
        free(words[i].word);
    }
    free(words);
}

static inline bool is_power_of_two(int n) {
    return (n > 0) && ((n & (n - 1)) == 0);
}

u128 int_pow128(uint64_t base, int exp) {
    u128 result = 1;
    for (int i = 0; i < exp; i++) {
        if (__builtin_mul_overflow(result, base, &result)) {
            return 0;
        }
    }
    return result;
}


int main(int argc, char *argv[]) {
    char *dict_path = NULL;
    char *output_path = NULL;
    int start_length = 0, end_length = 0;
    long num_threads = 1;
    bool no_spaces = false;
    bool n_specified = false;
    u128 num_to_generate = 0;
    GenerationMode mode = MODE_SEQUENTIAL;

    int opt;
    while ((opt = getopt(argc, argv, "i:l:o:t:n:kRh")) != -1) {
        switch (opt) {
            case 'i': dict_path = optarg; break;
            case 'l': 
                if (strchr(optarg, '-')) {
                    sscanf(optarg, "%d-%d", &start_length, &end_length);
                } else {
                    start_length = end_length = atoi(optarg);
                }
                break;
            case 'o': output_path = optarg; break;
            case 't': num_threads = atol(optarg); break;
            case 'n': 
                sscanf(optarg, "%llu", (unsigned long long*)&num_to_generate);
                n_specified = true; 
                break;
            case 'k': no_spaces = true; break;
            case 'R': mode = MODE_RANDOM; break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    if (!dict_path || start_length <= 0) {
        print_usage(argv[0]);
        return 1;
    }
    if (start_length > end_length) {
        int temp = start_length;
        start_length = end_length;
        end_length = temp;
    }
    if (num_threads <= 0) {
        num_threads = 1;
    }
    
    WordInfo *words = NULL;
    int num_words = 0;
    read_dictionary(dict_path, &words, &num_words);
    
    bool use_power_of_two_opt = is_power_of_two(num_words);
    int shift_bits = use_power_of_two_opt ? log2(num_words) : 0;
    
    pthread_t *thread_ids = (pthread_t*)malloc(num_threads * sizeof(pthread_t));
    ThreadData *thread_data = (ThreadData*)malloc(num_threads * sizeof(ThreadData));
    if (!thread_ids || !thread_data) {
        free_words(words, num_words);
        return 1;
    }

    FILE *output_file = stdout;
    if (output_path) {
        output_file = fopen(output_path, "w");
        if (!output_file) {
            free(thread_ids);
            free(thread_data);
            free_words(words, num_words);
            return 1;
        }
    }

    pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
    
    for (int len = start_length; len <= end_length; ++len) {
        if (len > MAX_WORD_LENGTH) continue; 

        bool infinite_mode = (mode == MODE_RANDOM) && !n_specified;
        u128 total_combinations = 0;

        if (mode == MODE_SEQUENTIAL) {
            total_combinations = int_pow128(num_words, len);
            if (total_combinations == 0 && (num_words > 1 || len > 0)) {
                continue;
            }
        } else if (!infinite_mode) {
            total_combinations = num_to_generate;
        }

        u128 combinations_per_thread = (infinite_mode || total_combinations == 0) ? 0 : total_combinations / num_threads;
        u128 remainder = (infinite_mode || total_combinations == 0) ? 0 : total_combinations % num_threads;
        u128 current_start_index = 0;

        for (int i = 0; i < num_threads; i++) {
            thread_data[i] = (ThreadData){
                .startIndex = current_start_index,
                .count = combinations_per_thread + (i < remainder ? 1 : 0),
                .length = len,
                .numWords = num_words,
                .words = words,
                .noSpaces = no_spaces,
                .infinite = infinite_mode,
                .mode = mode,
                .file = output_file,
                .mutex = &file_mutex,
                .is_power_of_two = use_power_of_two_opt,
                .shift_bits = shift_bits,
                .mask = num_words - 1
            };
            if (mode == MODE_SEQUENTIAL) {
                current_start_index += thread_data[i].count;
            }
            if (pthread_create(&thread_ids[i], NULL, generation_thread_worker, &thread_data[i]) != 0) {
                goto cleanup; 
            }
        }

        for (int i = 0; i < num_threads; i++) {
            pthread_join(thread_ids[i], NULL);
        }
    }

cleanup:
    if (output_file != stdout) {
        fclose(output_file);
    }
    pthread_mutex_destroy(&file_mutex);
    free_words(words, num_words);
    free(thread_ids);
    free(thread_data);

    return 0;
}
