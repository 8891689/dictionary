/* password_generator.c
 * https://github.com/8891689
 * gcc combination_dictionary.c -march=native -O3 -o cd
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

typedef struct {
    char *data;         // 內存映射指針
    size_t size;        // 文件大小
    size_t line_count;  // 行數
    size_t *offsets;    // 行偏移數組
} MappedFile;

// 創建內存映射文件
MappedFile* map_file(const char *filename) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("open");
        return NULL;
    }

    MappedFile *mf = malloc(sizeof(MappedFile));
    if (!mf) {
        close(fd);
        return NULL;
    }

    // 獲取文件大小
    mf->size = lseek(fd, 0, SEEK_END);
    if (mf->size == (size_t)-1) {
        perror("lseek");
        close(fd);
        free(mf);
        return NULL;
    }

    // 文件大小為 0，特殊處理
    if (mf->size == 0) {
        close(fd);
        mf->data = NULL; // 沒有數據
        mf->line_count = 0;
        mf->offsets = NULL; // 沒有偏移量
        return mf; // 成功，但文件為空
    }


    // 內存映射
    mf->data = mmap(NULL, mf->size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mf->data == MAP_FAILED) {
        perror("mmap");
        close(fd);
        free(mf);
        return NULL;
    }

    close(fd);

    // 建立行索引
    size_t capacity = 0;
    mf->line_count = 0;
    mf->offsets = NULL;

    // offsets 數組存儲每行結束的位置（換行符的位置）
    // 行的起始位置是前一行的結束位置 + 1，或第一行的 0。
    for (size_t i = 0; i < mf->size; i++) {
        if (mf->data[i] == '\n') {
            // 擴展數組
            if (mf->line_count >= capacity) {
                capacity = capacity ? capacity * 2 : 256; // 初始容量 256，然後翻倍
                size_t *new_offsets = realloc(mf->offsets, capacity * sizeof(size_t));
                if (!new_offsets) {
                    munmap(mf->data, mf->size);
                    free(mf);
                    return NULL; // 表示失敗
                }
                mf->offsets = new_offsets;
            }
            mf->offsets[mf->line_count++] = i;
        }
    }

    // 處理文件末尾沒有換行符的情況。最後一行將不會被計數。
    // 如果文件不為空且最後一個字符不是換行符，將文件結尾作為最後一個偏移量添加。
    if (mf->size > 0 && mf->data[mf->size - 1] != '\n') {
         if (mf->line_count >= capacity) {
            capacity = capacity ? capacity * 2 : 256; // 確保容量
            size_t *new_offsets = realloc(mf->offsets, capacity * sizeof(size_t));
            if (!new_offsets) {
                munmap(mf->data, mf->size);
                free(mf);
                return NULL; // 表示失敗
            }
            mf->offsets = new_offsets;
        }
        mf->offsets[mf->line_count++] = mf->size; // 使用文件大小作為最後一行的結束偏移量
    }
    return mf;
}

// 使用本地標準庫的隨機數生成器 (rand/srand)
// 生成一個 [0, max-1] 範圍內的隨機索引
size_t generate_random_index(size_t max) {
    if (max == 0) {
        return 0; // 無法為空範圍生成索引
    }
    if (max == 1) {
        return 0; // 只有一個可能的索引
    }

    size_t random_val = 0;
    int bits_in_size_t = sizeof(size_t) * 8;
    int bits_per_rand_call = 0; // 一次 rand() 調用提供的有用位數

    unsigned int temp_rand_max = RAND_MAX;
    while(temp_rand_max > 0) {
        bits_per_rand_call++;
        temp_rand_max >>= 1;
    }
    if (bits_per_rand_call == 0) bits_per_rand_call = 1; // 處理 RAND_MAX = 0 的情況？ (不太可能/錯誤)

    for (int i = 0; i < bits_in_size_t; i += bits_per_rand_call) {
         // 獲取一個隨機數
         unsigned int r = rand();
         // 如果 size_t 中剩餘的位數少於一次 rand() 調用提供的位數，則對 rand() 結果進行掩碼
         int current_bits_to_use = (bits_in_size_t - i < bits_per_rand_call) ? (bits_in_size_t - i) : bits_per_rand_call;
         
         random_val |= (size_t)r << i;
     }

    return random_val % max;

}

void generate_sequential_combinations(MappedFile *prefix, MappedFile *suffix) {
    const size_t P = prefix->line_count;
    const size_t S = suffix->line_count;

    if (P == 0 || S == 0) {

        return;
    }

    for (size_t p_idx = 0; p_idx < P; p_idx++) {
        for (size_t s_idx = 0; s_idx < S; s_idx++) {

            size_t p_start = (p_idx > 0) ? prefix->offsets[p_idx - 1] + 1 : 0;
            size_t p_end = prefix->offsets[p_idx];
            size_t p_len = p_end - p_start;

            size_t s_start = (s_idx > 0) ? suffix->offsets[s_idx - 1] + 1 : 0;
            size_t s_end = suffix->offsets[s_idx];
            size_t s_len = s_end - s_start;

            // 輸出組合
            fwrite(prefix->data + p_start, 1, p_len, stdout);
            fwrite(suffix->data + s_start, 1, s_len, stdout);
            fputc('\n', stdout);
        }
    }
}

// 隨機生成組合
void generate_random_combinations(MappedFile *prefix, MappedFile *suffix) {
    const size_t P = prefix->line_count;
    const size_t S = suffix->line_count;

     if (P == 0 || S == 0) {
        // fprintf(stderr, "Error: Empty input files\n"); // 已在 main 中檢查並處理
        return;
    }
    // srand(time(NULL)) 種子設置已在 main 函數調用此函數前完成。

    while (1) {  // 無限循環，直到外部程序終止
        // 生成隨機索引
        size_t p_idx = generate_random_index(P);
        size_t s_idx = generate_random_index(S);

        size_t p_start = (p_idx > 0) ? prefix->offsets[p_idx - 1] + 1 : 0;
        size_t p_end = prefix->offsets[p_idx];
        size_t p_len = p_end - p_start;

        size_t s_start = (s_idx > 0) ? suffix->offsets[s_idx - 1] + 1 : 0;
        size_t s_end = suffix->offsets[s_idx];
        size_t s_len = s_end - s_start;

        // 輸出組合
        fwrite(prefix->data + p_start, 1, p_len, stdout);
        fwrite(suffix->data + s_start, 1, s_len, stdout);
        fputc('\n', stdout);
    }
}

void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s -q <prefix_file> -h <suffix_file> [-R]\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -q <prefix_file>       Path to prefix file\n");
    fprintf(stderr, "  -h <suffix_file>       Path to suffix file\n");
    fprintf(stderr, "  -R                     Enable random mode\n");
}

int main(int argc, char *argv[]) {
    const char *prefix_file = NULL;
    const char *suffix_file = NULL;
    bool random_mode = false;

    // 解析命令行參數
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0 && i + 1 < argc) {
            prefix_file = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            suffix_file = argv[++i];
        } else if (strcmp(argv[i], "-R") == 0) {
            random_mode = true;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!prefix_file || !suffix_file) {
        print_usage(argv[0]);
        return 1;
    }

    // 映射文件
    MappedFile *prefix = map_file(prefix_file);
    if (!prefix) {
        fprintf(stderr, "Failed to map prefix file: %s\n", prefix_file);
        return 1;
    }

    MappedFile *suffix = map_file(suffix_file);
    if (!suffix) {
        fprintf(stderr, "Failed to map suffix file: %s\n", suffix_file);
        // 如果 suffix 映射失敗，清理 prefix 的資源
        if (prefix->data) munmap(prefix->data, prefix->size); // 只有成功映射才需要 munmap
        free(prefix->offsets); // 先釋放 offsets
        free(prefix); // 再釋放結構體
        return 1;
    }

    // 在隨機模式下，初始化隨機數生成器種子 (只需要執行一次)
    if (random_mode) {
        srand(time(NULL)); // 使用當前時間作為隨機數生成器的種子
    }

    // 檢查文件是否為空（沒有行）
    if (prefix->line_count == 0 || suffix->line_count == 0) {
         fprintf(stderr, "Error: Input files are empty or contain no lines.\n");
         // 即使文件為空，資源也需要清理
         if (prefix->data) munmap(prefix->data, prefix->size);
         if (suffix->data) munmap(suffix->data, suffix->size);
         free(prefix->offsets);
         free(suffix->offsets);
         free(prefix);
         free(suffix);
         return 1; // 以錯誤狀態退出
    }


    // 根據模式生成組合
    if (random_mode) {
        generate_random_combinations(prefix, suffix);
    } else {
        generate_sequential_combinations(prefix, suffix);
    }

    // 清理資源
    if (prefix->data) munmap(prefix->data, prefix->size); // 只有成功映射才需要 munmap
    if (suffix->data) munmap(suffix->data, suffix->size);
    free(prefix->offsets);
    free(suffix->offsets);
    free(prefix);
    free(suffix);

    return 0; 
    
}
