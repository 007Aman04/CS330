# CS330
This repository contains my solutions for all the assignments of the course CS330 (Operating Systems) taken by Prof. Debadatta Mishra, IIT Kanpur. 

## Assignment 1
### Part 1
This part has the basic implementation of unary chaining operations using system calls like "exec".

### Part 2
This part calculates the space used by a directory and its child directories and files recursively using system calls like "fork".

### Part 3
This part has implementation of dynamic memory allocation/deallocation in address space.

## Assignment 2
In this assignment, we implemented the Trace Buffer, a functionality similar to pipe for input/output. Using the same trace buffer, we implemeted a functionality named "strace" to trace all the system calls and "ftrace" to stack backtrace all the function calls made.

## Assignment 3
In this assignment, we implemented system calls "mmap", "munmap", "mprotect" for memory allocation, deallocation, protection and managed a 4-level page table using lazy allocation strategy. Finally, we designed a system call "cfork" to copy parent process' PCB into child process PCB. We also implemented copy-on-write (CoW) fault.
