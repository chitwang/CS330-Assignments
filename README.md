# Repository Overview

This repository contains my solutions to the assignments undertaken during the Operating Systems Course (CS330) in the 5th semester at IITK, guided by **Prof. Debadatta Mishra** from July 2023 to November 2023.

## Assignments Overview:

### Assignment 1
Implemented API functions for dynamic memory allocation and deallocation, along with a feature to calculate the size of any directory or file provided as arguments.

- **Grading:** Received 97 out of 100 marks, with a minor issue in the free API implementation leading to an infinite loop in one testcase.

### Assignment 2
Introduced trace buffer functionality (circular queue) in GemOS, implementing strace and ftrace system calls. Also, integrated a fault handler used in the ftrace implementation.

- **Grading:** Achieved full marks - 100/100.

### Assignment 3
Focused on kernel-level implementation of mmap, munmap, and mprotect system calls, incorporating lazy physical allocation through a page fault handler. Additionally, implemented the cfork syscall with a CoW fault handler.

- **Grading:** Attained full marks - 100/100.

**Note:** In Assignment 2 and 3, we were given the object files for various other files and we had to modify the relevant files (tracer.c in 2 and v2p.c in 3).
  
**PS:** All assignments were individual, and the evaluation test cases were not disclosed beforehand during grading :)
