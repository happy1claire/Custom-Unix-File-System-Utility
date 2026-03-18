# Custom-Unix-File-System-Utility

## Introduction
This project is a custom implementation of the Unix `ls` command-line utility written in C. It is capable of reading and listing directory and file information, and it supports several commonly used `ls` flags, including listing hidden files, displaying detailed file information, recursively searching directories, and counting the number of files.

## Features & Supported Flags
This program supports the following command-line arguments and allows combining multiple flags (e.g., `-al`, `-lR`, `-alnR`):

* `-a` (all): List all entries including those starting with a dot (`.`).
* `-l` (long format): Use a long listing format. Displays file type and permissions, number of hard links, owner, group, file size (in bytes), time of last modification, and filename.
* `-R` (recursive): List subdirectories recursively.
* `-n` (count): Count and output the total number of files in the specified directory or search results.
* `--help`: Display the help message and usage information.

## Environment & Dependencies
* **Compiler**: A C compiler supporting the C11 standard (e.g., GCC).
* **Build Tool**: Make.
* **Testing Framework**: Bats (Bash Automated Testing System) for running automated tests.

## Build Instructions

### Compiling the Program
Open your terminal in the root directory of the project and run the following command to compile:
```bash
make