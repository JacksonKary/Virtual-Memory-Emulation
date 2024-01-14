# Virtual-Memory-Emulation



## Description
This project is a simple, yet fully functional demand paged virtual memory. Although virtual memory is
normally implemented at the kernel level, it can also be implemented at the user level, which is a
technique used by modern virtual machines.

The following figure gives an overview of the components:

![image](https://github.com/JacksonKary/Virtual-Memory-Emulation/assets/117691954/b0d10e1d-2cdc-4b9e-9031-f7e9bbc48125)


The virtual page table will create a small virtual and a small physical memory, along with the methods for updating the
page table entries and protection bits. When an application uses the virtual memory, it can result in a page
fault that calls a custom handler. I've implemented a page fault handler that traps page faults and
triggers a series of actions, including updating the page table and moving data back and forth between
disk and physical memory. I implemented multiple different page replacement algorithms to be used
in the handler as well.



## Virtual Memory Emulation - Using mmap
Read <i>page_table.cpp/h</i> and <i>disk.cpp/h</i> to learn how the various virtual memory library calls are
used. This code will also give an understanding of how virtual memory is emulated
in our user-space program. To accomplish this, <i>page_table_create()</i> creates a file that will act as
physical memory. We then use <i>mmap</i> to create an address space in the program that maps to that file
(refer to the man page for more details).

Memory-mapped files are used to provide a virtual memory address space (virtmem) and physical
memory address space (physmem) to the physical memory file. The physical memory address space
always maps directly to the physical memory file. The virtual memory address space will have indirect
mappings from pages in the virtually memory address space to frames in the physical memory file. This is
achieved using the <i>remap_file_pages</i> system call (refer to the man page for more details).
This function allows us to create a non-sequential mapping of the memory-mapped address space to the
underlying file. By using <i>remap_file_pages</i> with a page size granularity, we can create a virtual
memory address space that indirectly maps pages to different frames in the physical memory file.

Access to memory-mapped files can also be protected using the <i>mprotect</i> system call (refer to the man
page for more details). This will allow us to set read and write permissions on regions of the virtual
memory address space. That way, for example, we can set up a non-resident page in the virtual address
space to have no permissions so that if the user attempts to access that memory region it will result in a
page fault. Page faults are caught in our user space library by setting up a signal handler for the
segmentation fault signal (SIGSEGV). When user code attempts to use a portion of the virtual address
space that it does not have permission to use, a signal will be sent by the operating system and caught by
<i>internal_fault_hanlder()</i> within <i>page_fault.cpp</i>. This function will then call the page fault
handlers.



## Page Fault Handling
The virtual page table is very similar to other common implementations, except that it does not have a
referenced or dirty bit for each page. The system supports a read bit (PROT_READ) and a write bit
(PROT_WRITE). When neither the read bit nor the write bit is set, the page should not be resident in
physical memory.
The following state machine should be used to handle page faults and page table management.

![image](https://github.com/JacksonKary/Virtual-Memory-Emulation/assets/117691954/1390ca4e-c57e-4330-8a87-3141ea1120da)

Upon completion, the main program will print the number of page faults, disk reads, and disk writes
over the course of the program. Only the transition from NOT_RESIDENT to READ_ONLY is considered as a page
fault. The transition from READ_ONLY to READ_WRITE is technically resulting in a page fault in our
user-space program, but it used to emulate a dirty bit that would usually be handled by hardware and not
require a page fault. For this reason, only the number of page faults due to not resident pages is counted.



## Example Operations
Let's work through a concrete example, starting with
the figure on the right side. Suppose we begin with
nothing in physical memory. If the application begins
by trying to read page 2, this will result in a page
fault. The page fault handler chooses a free frame, say
frame 3. It then adjusts the page table to map page 2
to frame 3, with read permissions. Then, it loads page
2 from disk into frame 3. On the first page fault, you
can assume the disk block corresponding to this page
is appropriately zeroed out. When the page fault
handler completes, the read operation is automatically
re-attempted by the system and succeeds.

![image](https://github.com/JacksonKary/Virtual-Memory-Emulation/assets/117691954/46cb98c5-99d0-4a05-8010-31c8de35c0b1)

The application continues to perform read
operations. Suppose that it reads pages 3, 5, 6, and
7. Each read operation results in a page fault,
which triggers a memory loading as in the
previous step. After this step physical memory is
full.

![image](https://github.com/JacksonKary/Virtual-Memory-Emulation/assets/117691954/8b94fecd-05f3-4502-9c76-5c99f75bb613)

Now suppose that the application attempts to
write to page 5. Because this page only has the
PROT_READ bit set, a page fault will occur. The
page fault handler checks page 5’s current page
bits and adds the PROT_WRITE bit. When the
page fault handler returns, the write operation is
automatically re-attempted by the system and
succeeds. Page 5, frame 0 is modified.

![image](https://github.com/JacksonKary/Virtual-Memory-Emulation/assets/117691954/61e1bb03-ea92-4690-953e-946779dd5e88)

Now suppose that the application reads page 1.
Page 1 is not currently paged into physical
memory. The page fault handler must decide
which frame to evict. Suppose that it picks page 5,
frame 0. Because page 5 has the PROT_WRITE
bit set, it is dirty. The page fault handler writes
page 5 back to the disk and reads page 1 to frame
0. Two entries in the page table are updated to
reflect the new state.

![image](https://github.com/JacksonKary/Virtual-Memory-Emulation/assets/117691954/723848d2-6da8-47f3-807d-b69a6940b083)



## Running the program
First, open a linux terminal with in the project directory. Then, run:

<code>make</code>

to build the program. It can now be invoked as follows:

<code>./virtmem npages nframes rand|fifo|custom scan|sort|focus</code>

<i>npages</i> is the number of pages and <i>nframes</i> is the number of frames to create in the system.
The third argument is the page replacement algorithm. The options are <i>rand</i> (random replacement),
<i>fifo</i> (first-in-first-out), and <i>custom</i>, which is a silly twist on fifo. The last argument
specifies which benchmark program to run: <i>scan</i>, <i>sort</i>, or <i>focus</i>.
Each test program accesses memory using a slightly different pattern.
It's a good idea to test with more pages than frames. This will result in more page faults and evictions.



## What's in this directory?
- <code>disk.cpp/h</code> : Disk management functions.
- <code>page_table.cpp/h</code> : Page table management functions.
- <code>program.cpp/h</code> : A set of test programs for testing the virtual memory. These functions are called from the main driver.
- <code>main.cpp</code> : Implementation of page fault hander and page replacement algorithms here. Includes a main driver that will run the test program specified on the command line
- <code>makefile</code> : Run <code>make</code> to build the program.
