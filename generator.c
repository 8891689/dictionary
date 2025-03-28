//author：8891689
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void printUsage(const char *programName) {
    fprintf(stderr, "usage: %s -i <Dictionary file path> -l <Digit range> [-k] [-R] [-p]\n", programName);
}

/* Parse a digit range string, such as "3-5" or "4", 
and return a dynamically allocated array with the number of digits stored in *count. */
int* parseLengthRange(const char *range, int *count) {
    int start = 0, end = 0;
    char *dash = strchr(range, '-');
    if (dash) {
        char temp[32];
        size_t len = dash - range;
        if (len >= sizeof(temp))
            len = sizeof(temp) - 1;
        strncpy(temp, range, len);
        temp[len] = '\0';
        start = atoi(temp);
        end = atoi(dash + 1);
    } else {
        start = end = atoi(range);
    }

    if (end < start) {
        fprintf(stderr, "Error: Invalid range of digits.\n");
        *count = 0;
        return NULL;
    }

    int num = end - start + 1;
    int *lengths = malloc(num * sizeof(int));
    if (!lengths) {
        fprintf(stderr, "Memory allocation failed。\n");
        exit(1);
    }
    for (int i = 0; i < num; i++) {
        lengths[i] = start + i;
    }
    *count = num;
    return lengths;
}

/* Generate a Bitcoin private key (32-byte random number converted to a 64-bit hexadecimal string).
Note: Random numbers are generated using rand(), which is not cryptographically secure. */
char* generateBTCPrivateKey(void) {
    unsigned char priv_key[32];
    for (int i = 0; i < 32; i++) {
        priv_key[i] = rand() % 256;
    }
    char *hex = malloc(65); // 64 characters + '\0'
    if (!hex) {
        fprintf(stderr, "Memory allocation failed。\n");
        exit(1);
    }
    for (int i = 0; i < 32; i++) {
        sprintf(hex + i * 2, "%02X", priv_key[i]);
    }
    hex[64] = '\0';
    return hex;
}

/* Generates mnemonic combination or private key output
Parameters:
words - array of words in dictionary
numWords - total number of words
length - combination length (number of digits)
noSpaces - if non-zero, do not add spaces between words
hexOutput - if non-zero, output private key (ignore mnemonic)
randomRead - if non-zero, randomly select words each time (but still update exit condition based on combination)
*/
void generateMnemonics(char **words, int numWords, int length, int noSpaces, int hexOutput, int randomRead) {
    int *indices = malloc(length * sizeof(int));
    if (!indices) {
        fprintf(stderr, "Memory allocation failed。\n");
        exit(1);
    }
    // Initialize index
    if (!randomRead) {
        if (length > numWords) {
            free(indices);
            return;
        }
        for (int i = 0; i < length; i++) {
            indices[i] = i;
        }
    } else {
        for (int i = 0; i < length; i++) {
            indices[i] = 0;
        }
    }

    while (1) {
        if (hexOutput) {
            char *btcKey = generateBTCPrivateKey();
            printf("%s\n", btcKey);
            free(btcKey);
        } else {
            for (int i = 0; i < length; i++) {
                int index;
                if (randomRead)
                    index = rand() % numWords;
                else
                    index = indices[i];
                printf("%s", words[index]);
                if (!noSpaces && i != length - 1)
                    printf(" ");
            }
            printf("\n");
        }

        int i;
        if (randomRead) {
            /* Use combined update logic (although the words are randomly selected each time they are output, 
            the index is still updated in the combined order to determine the exit timing) */
            for (i = length - 1; i >= 0; i--) {
                if (indices[i] < numWords - 1) {
                    indices[i]++;
                    for (int j = i + 1; j < length; j++) {
                        indices[j] = indices[j - 1] + 1;
                    }
                    break;
                }
            }
            if (i < 0)
                break;
        } else {
            for (i = length - 1; i >= 0; i--) {
                if (indices[i] < numWords - (length - i)) {
                    indices[i]++;
                    for (int j = i + 1; j < length; j++) {
                        indices[j] = indices[j - 1] + 1;
                    }
                    break;
                }
            }
            if (i < 0)
                break;
        }
    }
    free(indices);
}

int main(int argc, char* argv[]) {
    char *dictionaryFilePath = NULL;
    int *lengths = NULL;
    int lengthsCount = 0;
    int removeSpaces = 0;
    int randomRead = 0;
    int hexOutput = 0;

    if (argc < 5) {
        printUsage(argv[0]);
        return 1;
    }

    // Parsing command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            dictionaryFilePath = argv[++i];
        } else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            lengths = parseLengthRange(argv[++i], &lengthsCount);
            if (!lengths || lengthsCount == 0) {
                fprintf(stderr, "Error: Invalid digit range。\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-k") == 0) {
            removeSpaces = 1;
        } else if (strcmp(argv[i], "-R") == 0) {
            randomRead = 1;
        } else if (strcmp(argv[i], "-p") == 0) {
            hexOutput = 1;
        } else {
            printUsage(argv[0]);
            return 1;
        }
    }

    if (dictionaryFilePath == NULL || lengthsCount == 0) {
        printUsage(argv[0]);
        return 1;
    }

    // Open dictionary file
    FILE *fp = fopen(dictionaryFilePath, "r");
    if (!fp) {
        fprintf(stderr, "Unable to open dictionary file: %s\n", dictionaryFilePath);
        return 1;
    }

    // Read words from a file and store them in a dynamically allocated string array
    char **words = NULL;
    int wordsCount = 0;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), fp)) {
        size_t len = strlen(buffer);
        // Remove line breaks
        if (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
            buffer[len - 1] = '\0';
            len--;
            if (len > 0 && buffer[len - 1] == '\r') {
                buffer[len - 1] = '\0';
                len--;
            }
        }
        char *word = malloc(len + 1);
        if (!word) {
            fprintf(stderr, "Memory allocation failed。\n");
            exit(1);
        }
        strcpy(word, buffer);
        char **temp = realloc(words, (wordsCount + 1) * sizeof(char *));
        if (!temp) {
            fprintf(stderr, "Memory allocation failed。\n");
            exit(1);
        }
        words = temp;
        words[wordsCount++] = word;
    }
    fclose(fp);

    /* Initialize random number seed */
    srand((unsigned)time(NULL));

    /* For each specified number of digits, generate a mnemonic combination or private key */
    for (int i = 0; i < lengthsCount; i++) {
        generateMnemonics(words, wordsCount, lengths[i], removeSpaces, hexOutput, randomRead);
    }

    // Freeing up memory
    for (int i = 0; i < wordsCount; i++) {
        free(words[i]);
    }
    free(words);
    free(lengths);

    return 0;
}

