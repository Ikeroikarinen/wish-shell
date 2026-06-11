# Project 3: Unix Shell

This repository contains my solution for the Unix Shell project. The program is a small shell called `wish`, implemented in C.

## Implemented features

- Interactive mode: `./wish`
- Batch mode: `./wish batch.txt`
- External commands with arguments, searched from the shell path
- Initial shell path: `/bin`
- Built-in commands: `exit`, `cd`, and `path`
- Output and error redirection with `>`
- Parallel commands with `&`
- Error handling with messages printed to `stderr`
- Source code comments for the main parts of the implementation

## Build

```bash
make
```

Or directly:

```bash
gcc -Wall -Werror -o wish wish.c
```

## Example tests

```bash
./wish
wish> echo hello
hello
wish> ls
wish  wish.c  Makefile  README.md
wish> cd /
wish> pwd
/
wish> exit
```

Batch mode:

```bash
printf "echo hello\npwd\n" > batch.txt
./wish batch.txt
```

Redirection:

```bash
printf "echo hello > output.txt\ncat output.txt\n" > batch.txt
./wish batch.txt
```

Parallel commands:

```bash
printf "echo first & echo second\n" > batch.txt
./wish batch.txt
```

Error cases:

```bash
printf "cd\nexit now\necho hello > a b\nunknowncommand\n" > batch.txt
./wish batch.txt
```
