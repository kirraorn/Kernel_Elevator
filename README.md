# Kernel Elevator
Developing a kernel module and then using it to implement a scheduling algorithm for a pet elevator.

## Group Members
- **Kirra Orndorff**
- **Kate XXX**
- **Ludginie XXX**
## Division of Labor

### Part 1: System Call Tracing
- **Responsibilities**: Adding system calls to a C program and verifying their correctness using the strace tool to gain hands-on experience with system call integration and learn about the available system calls
- **Assigned to**: Kirra Orndorff

### Part 2: Timer Kernel Module
- **Responsibilities**: A kernel module called my_timer that retrieves and stores the current time using the ktime_get_real_ts64() function. This module creates a proc entry and allows you to read the current time and elapsed time since the last call
- **Assigned to**: Kirra Orndorff, Kate

### Part 3a:  Adding System Calls 
- **Responsibilities**: Add calls to module
- **Assigned to**: Ludginie, Kate

### Part 3b: Kernel Compilation 
- **Responsibilities**: Compile the kernel with the new system calls
- **Assigned to**: Kirra Orndorff

### Part 3c: Threads
- **Responsibilities**: Test if you successfully added the system calls to your installed kernel with the provided tests in your starter file in the directory
- **Assigned to**: Ludigine 

### Part 3d:  Linked List
- **Responsibilities**: Add linked list to module
- **Assigned to**: Kirra Orndorff

### Part 3e: Mutexes
- **Responsibilities**: control shared data access between floor and elevators.
- **Assigned to**: Ludginie 

### Part 3f: Scheduling Algorithms
- **Responsibilities**: manage what type of algoritm used to schedule elevator with pets
- **Assigned to**: Kate 

## File Listing
```
Project2/
├── part 1/
    ├── empty.c
    └── empty.trace
    └── part1.c
    └── part1.trace
    └── Makefile
├── part 2/
    ├── src/
        ├──my_timer.c
    └── Makefile
├── part 3/
    ├── src/
        ├──elevator.c
        └── Makefile
    └── Makefile
    └── syscalls.c
├──Makefile
├──README.md

```
## How to Compile & Execute

### Requirements
- **Compiler**: e.g., `gcc` for C/C++

### Compilation
For a C/C++ example:
```bash
make
```
This will build the executable in ...
### Execution
```bash
sudo insmod elevator.ko
watch -n1 cat /proc/elevator

```
In another terminal (an example)...
```bash
./producer 3
./consumer --start
./producer 1
./consumer --stop
```

## Development Log
Each member records their contributions here.

### Kirra Orndorff

| Date       | Work Completed / Notes |
|------------|------------------------|
| 2025-10-10 | Completed Part One     |
| 2025-10-19  | Began work on part 2   |
| 2025-10-22 | Completed part two   |
| 2025-10-24 | began adding sys calls to files |
| 2025-10-24    | created syscalls.c + wrote code |
| 2025-10-24 | began compliling process |
| 2025-10-25 | fixed errors with compiling - kernel compiled |
| 2025-10-26 | begun general structure for elevator.c |
| 2025-10-28| updated github |
| 2025-10-29| began testing of elevator module |
| 2025-11-1 | began trying to fix bugs in module |
| 2025-11-4 | final testing |


## Meetings
Document in-person meetings, their purpose, and what was discussed.

| Date       | Attendees            | Topics Discussed | Outcomes / Decisions |
|------------|----------------------|------------------|-----------------------|
| 2025-10-15 | Kirra, Ludginie, Kate  | Determine DOL   | DOL completed, each member knows parts |
| 2025-10-22 | Kirra, Ludginie, Kate| Discuss when to compile kernel/part completion  | finish kernel compilation by weekend/begin elevator.c |\
| 2025-10-27 | Kirra, Ludginie, Kate| Shared what we completed/still needed to do | all ready to start work on elevator.c |
| 2025-10-29 | Kirra, Ludginie, Kate| discussed how our parts combine on elevator.c| worked on optimizing code |

## Bugs
1. Missing dwarf.h file for kernel compilation - FIXED
2. Loading pets loop when max capacity reached - FIXED

## Considerations
- Demo to show how module works


