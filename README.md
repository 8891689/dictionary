# Password Dictionary

It generates word and password combinations based on a provided dictionary or character set file. Its strength depends on the text file you provide. Assuming your file contains every word or character set in the world, it can generate any password. For example, using the character table unicode.txt, it is theoretically possible to generate any password from known characters.

## Features

*   Reads a word list including a character set from the specified dictionary file.
*   Generates word or password combinations of a specified length (or range of lengths).
*   Optionally removes spaces between words or password combinations in the output.
*   Optionally selects a random word from the dictionary for each output (while still iterating over the combinations internally to determine the total number of outputs).

## Compilation

Assuming your C source code file is named `generator.c`, you can compile it using GCC (or another C compiler):

```bash
gcc -O3 -march=native generator.c -o generator -lpthread -lm -static

or

gcc -O3 -march=native combination_dictionary.c -o cd -lpthread -static

```

This will generate an executable file named generator.

# Usage
```
./generator -i <dictionary_path> -l <length_specifier> [-k] [-R]

or

./cd -c <prefix_file> -d <suffix_file> [-R] Enable random mode
```

# Options:
```
./generator -h
Usage: ./generator -i <dict> -l <len|s-e> [OPTIONS]

A silent, high-performance password generator for pipelining.

Required:
  -i <path>      Path to a character set or dictionary word file.
  -l <len|s-e>   Password or word length for (e.g., '12' or a range like '8-12').

Modes:
  (default)      Sequential Generation.
  -R             Random Generation.

Options:
  -n <number>    Number of items for Random Mode default: infinite.
  -t <threads>   Number of threads to use (default: 1).
  -o <file>      Output file path (default: stdout).
  -k             There is no space between the word and the password. result
  -h             Show this help message.

 author：        https://github.com/8891689
Speed test: ./generator -i bip39.txt -l 10 -t 8 | pv > /dev/null
```
```

./cd -h
Usage: ./cd -c <prefix_file> -d <suffix_file> [OPTIONS]
A password generator that combines prefix and suffix passwords or words into a password.

Required:
  -c <file>   Path to the prefix file.
  -d <file>   Path to the suffix file.

Options:
  -R          Enable Random mode (runs infinitely). Default: sequential.
  -t <num>    Number of threads to use (default: 1).
  -h          Show this help message.
 author：     https://github.com/8891689
```


-i <dictionary_path>: (Required) Specifies a dictionary file containing a list of words or character sets. This file should be plain text, with one word per line.

-l <length_specifier>: (Required) Specifies the length of the combination to generate (number of words or passwords).

Can be a single number, such as 12.

Can be a range, such as 3-5 (which will generate combinations of lengths 3, 4, and 5).

-k: (Optional) Removes spaces between words in the output.

-R: (Optional) Random read mode. For each output generated, a word (or character) is randomly selected from the dictionary, rather than sequentially. Note: The program still iterates over the combination index internally to determine when to stop.

-c Prefix vocabulary

-d Suffix vocabulary

# Combining Character Sets for Advanced Use 

./generator -i my_dictionary.txt -l 3-5 | ./brainflayer -v -b hash160.blf -f hash160.bin -t priv -x -c uce > key.txt

./cd -c my_dictionary.txt -d my_dictionary.txt -R | ./brainflayer -v -b hash160.blf -f hash160.bin -t priv -x -c uce > key.txt

# Examples

Generate word combinations of length 12:
Assuming your dictionary file is bip39_words.txt.
```
./generator -i bip39_words.txt -l 12
```

Generate word combinations for lengths 3 through 5:
```
./generator -i my_dictionary.txt -l 3-5
```

Generate length 12 combinations without spaces:
```
./generator -i bip39_words.txt -l 12 -k
```


Generate length 4 combinations, selecting words randomly:
```
./generator -i my_dictionary.txt -l 4 -R
```

# E5 2697 V4 2.3 single-threaded test

```
./generator -i Taiwan.txt -l 5 -R | pv > /dev/null
57GiB 0:00:08 [ 202MiB/s] [                  <=>                                                                                               ]
^C

./generator -i Taiwan.txt -l 5 | pv > /dev/null
C20GiB 0:00:14 [ 306MiB/s] [                               <=>                                                                                  ]
^

./cd -c 7000zhongwen.txt -d 41975unicode.txt | pv > /dev/null
1.90GiB 0:00:06 [ 313MiB/s] [               <=>                                                                                                  ]

./cd -c 7000zhongwen.txt -d 41975unicode.txt -R | pv > /dev/null
25GiB 0:00:15 [ 249MiB/s] [                                 <=>                                                                                ]
^C

```
Not fast enough? You can enable 10 threads, which would increase the speed tenfold. Several gigabytes of data per second can fill a multi-terabyte hard drive in 1,000 seconds.


Words that lack prefixes or suffixes can be created using the above method or my other library called wandian.（https://github.com/8891689/Password-Dictionary-Generator ）。
These methods can create any password dictionary. For a comprehensive dictionary containing all human characters, download the Unicode character table. which includes all known character sets. Alternatively, simpler dictionaries can be created, such as those with English letters as prefixes and numbers as suffixes, using a combination password dictionary.


# You can use a larger -l range to generate more simulated keys.

Dictionary file format: Make sure the file specified with -i is a plain text file containing only one word or character set per line.

# Sponsorship
If this project was helpful to you, please buy me a coffee. Your support is greatly appreciated. Thank you!
```
BTC: bc1qt3nh2e6gjsfkfacnkglt5uqghzvlrr6jahyj2k
ETH: 0xD6503e5994bF46052338a9286Bc43bC1c3811Fa1
DOGE: DTszb9cPALbG9ESNJMFJt4ECqWGRCgucky
TRX: TAHUmjyzg7B3Nndv264zWYUhQ9HUmX4Xu4
```

# 📜 Disclaimer
Reminder: Do not input real private keys on connected devices!

This tool is provided for learning and research purposes only. Please use it with an understanding of the relevant risks. The developers are not responsible for financial losses or legal liability -caused by the use of this tool.

