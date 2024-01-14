/*
Main program for the virtual memory project.
The header files page_table.h and disk.h explain
how to use the page table and disk interfaces.
*/

#include "page_table.h"
#include "disk.h"
#include "program.h"

#include <cassert>
#include <iostream>
#include <string.h>
#include <ctime>
#include <cstdlib>

using namespace std;


// Prototype for test program
typedef void (*program_f)(char *data, int length);

// Number of physical frames
int nframes;

// Stats counters
int current_frames = 0;
int page_faults = 0;
int disk_reads = 0;
int disk_writes = 0;

// FIFO frame index
// Keeps track of index in physical frame table in circular fashion
// Represents next frame to replace/evict
int fifo_index = 0;
// bififo_direction keeps track of which direction custom_handler (bi-directional fifo) is moving
// true = + direction
// false = - direction
bool bififo_direction = true;


// Pointer to disk for access from handlers
struct disk *disk = nullptr;

// Simple handler for pages == frames
void page_fault_handler_example(struct page_table *pt, int page)
{
    cout << "page fault on page #" << page << endl;

    // Print the page table contents
    cout << "Before ---------------------------" << endl;
    page_table_print(pt);
    cout << "----------------------------------" << endl;

    // Map the page to the same frame number and set to read/write
    page_table_set_entry(pt, page, page, PROT_READ | PROT_WRITE);

    // Print the page table contents
    cout << "After ----------------------------" << endl;
    page_table_print(pt);
    cout << "----------------------------------" << endl;
}

void random_handler(struct page_table *pt, int page) {
    int frame, bits;
    page_table_get_entry(pt, page, &frame, &bits);

    if (bits == 0) {
        page_faults++;
        // Full
        if (current_frames == page_table_get_nframes(pt)) {
            // Evict
            
            // Random eviction
            int epage = rand() % page_table_get_npages(pt);
            int eframe, ebits = 0;
            page_table_get_entry(pt, epage, &eframe, &ebits);

            // Pick a valid random page
            while (ebits == 0) {
                epage = rand() % page_table_get_npages(pt);
                page_table_get_entry(pt, epage, &eframe, &ebits);
            }

            // If dirty, write to disk
            if (ebits == (PROT_READ|PROT_WRITE)) {
                disk_write(disk, epage, page_table_get_physmem(pt) + eframe*BLOCK_SIZE);
                disk_writes++;
            }
            
            // Set the evicted page to empty
            page_table_set_entry(pt, epage, 0, 0);
            // Set the new page to the evicted frame
            page_table_set_entry(pt, page, eframe, PROT_READ);
            disk_read(disk, page, page_table_get_physmem(pt) + eframe*BLOCK_SIZE);
            disk_reads++;
        }
        else {
            // Not full, filling the frames
            page_table_set_entry(pt, page, current_frames, PROT_READ);
            disk_read(disk, page, page_table_get_physmem(pt) + current_frames*BLOCK_SIZE);
            disk_reads++;
            current_frames++;
        }
    }
    else if (bits == PROT_READ) {
        page_table_set_entry(pt,page,frame,PROT_READ|PROT_WRITE);
    }
}

void fifo_handler(struct page_table *pt, int page) {
    int frame, bits;
    page_table_get_entry(pt, page, &frame, &bits);

    if (bits == 0) {
        page_faults++;
        // Full
        if (current_frames == page_table_get_nframes(pt)) {
            // Evict
            
            // FIFO eviction
            // Work backwards to get page at frame
            int epage = 0;
            int eframe = 0;
            // Check each page mapping until we find the page that maps to the desired frame (fifo_index is the frame we want to replace)
            while ((epage <= page_table_get_npages(pt)) && (eframe = pt->page_mapping[epage]) != fifo_index) {
                epage++;
            }
            int ebits = 0;
            // Get ebits
            page_table_get_entry(pt, epage, &eframe, &ebits);
            // Pick a valid page
            while (ebits == 0) {
                epage = (epage + 1) % page_table_get_npages(pt);
                page_table_get_entry(pt, epage, &eframe, &ebits);
                // Increment FIFO frame index
                fifo_index = (fifo_index + 1) % page_table_get_nframes(pt);
            }

            // If dirty, write to disk
            if (ebits == (PROT_READ|PROT_WRITE)) {
                disk_write(disk, epage, page_table_get_physmem(pt) + eframe*BLOCK_SIZE);
                disk_writes++;
            }
            
            // Set the evicted page to empty
            page_table_set_entry(pt, epage, 0, 0);
            // Set the new page to the evicted frame
            page_table_set_entry(pt, page, eframe, PROT_READ);
            // Increment FIFO frame index
            fifo_index = (fifo_index + 1) % page_table_get_nframes(pt);
            disk_read(disk, page, page_table_get_physmem(pt) + eframe*BLOCK_SIZE);
            disk_reads++;
        }
        else {
            // Not full, filling the frames
            page_table_set_entry(pt, page, current_frames, PROT_READ);
            disk_read(disk, page, page_table_get_physmem(pt) + current_frames*BLOCK_SIZE);
            disk_reads++;
            current_frames++;
            // Increment FIFO frame index
            fifo_index = (fifo_index + 1) % page_table_get_nframes(pt);
        }
    }
    else if (bits == PROT_READ) {
        page_table_set_entry(pt,page,frame,PROT_READ|PROT_WRITE);
    }
}

// Custom handler function: Bi-Directional FIFO
// It works similarly to FIFO, except it changes directions when it reaches an end of the frame table
// Example: On first pass through the frames it acts like normal FIFO. It starts at frame 0 and 
//          iterates to frame #frames-1. Then, it changes direction, and goes from frame #frames-2
//          back to frame 0
void custom_handler(struct page_table *pt, int page) {
    int frame, bits;
    page_table_get_entry(pt, page, &frame, &bits);

    if(bits == 0) {
        page_faults++;
        // Full
        if(current_frames == page_table_get_nframes(pt)) {
            // Evict
            
            // FIFO eviction
            // Work backwards to get page at frame
            int epage = 0;
            int eframe = 0;
            // Check each page mapping until we find the page that maps to the desired frame (fifo_index is the frame we want to replace)
            while ((epage <= page_table_get_npages(pt)) && (eframe = pt->page_mapping[epage]) != fifo_index) {
                epage++;
            }
            int ebits = 0;
            // Get ebits
            page_table_get_entry(pt, epage, &eframe, &ebits);
            // Pick a valid page
            while (ebits == 0) {
                epage = (epage + 1) % page_table_get_npages(pt);
                page_table_get_entry(pt, epage, &eframe, &ebits);
                // Update fifo_index
                if (bififo_direction) {
                    // Moving in positive direction, increment FIFO frame index
                    fifo_index = (fifo_index + 1) % page_table_get_nframes(pt);
                } else {
                    // Moving in negative direction, decrement FIFO frame index
                    fifo_index = (fifo_index - 1) % page_table_get_nframes(pt);
                }
                // If we've reached an end index, reverse direction
                if (fifo_index == 0 || fifo_index == page_table_get_nframes(pt) - 1) {
                    bififo_direction = !bififo_direction;
                }
            }

            // If dirty, write to disk
            if (ebits == (PROT_READ|PROT_WRITE)) {
                disk_write(disk, epage, page_table_get_physmem(pt) + eframe*BLOCK_SIZE);
                disk_writes++;
            }
            
            // Set the evicted page to empty
            page_table_set_entry(pt, epage, 0, 0);
            // Set the new page to the evicted frame
            page_table_set_entry(pt, page, eframe, PROT_READ);
            // Update fifo_index
            if (bififo_direction) {
                // Moving in positive direction, increment FIFO frame index
                fifo_index = (fifo_index + 1) % page_table_get_nframes(pt);
            } else {
                // Moving in negative direction, decrement FIFO frame index
                fifo_index = (fifo_index - 1) % page_table_get_nframes(pt);
            }
            // If we've reached an end index, reverse direction
            if (fifo_index == 0 || fifo_index == page_table_get_nframes(pt) - 1) {
                bififo_direction = !bififo_direction;
            }
            disk_read(disk, page, page_table_get_physmem(pt) + eframe*BLOCK_SIZE);
            disk_reads++;
        }
        else {
            // Not full, filling the frames
            page_table_set_entry(pt, page, current_frames, PROT_READ);
            disk_read(disk, page, page_table_get_physmem(pt) + current_frames*BLOCK_SIZE);
            disk_reads++;
            current_frames++;
        }
    }
    else if (bits == PROT_READ) {
        page_table_set_entry(pt,page,frame,PROT_READ|PROT_WRITE);
    }
}

void page_fault_handler(struct page_table *pt, int page)
{
    int frame, bits;
    page_table_get_entry(pt, page, &frame, &bits);

    if(bits == 0) {
        // Full
        if(current_frames == page_table_get_nframes(pt)) {
            // Evict
            
            // Random eviction
            int epage = rand() % page_table_get_npages(pt);
            int eframe, ebits = 0;
            page_table_get_entry(pt, epage, &eframe, &ebits);

            // Pick a valid random page
            while (ebits == 0) {
                epage = rand() % page_table_get_npages(pt);
                page_table_get_entry(pt, epage, &eframe, &ebits);
            }

            // If dirty, write to disk
            if (ebits == (PROT_READ|PROT_WRITE)) {
                disk_write(disk, epage, page_table_get_physmem(pt) + eframe*BLOCK_SIZE);
                disk_writes++;
            }
            
            // Set the evicted page to empty
            page_table_set_entry(pt, epage, 0, 0);
            // Set the new page to the evicted frame
            page_table_set_entry(pt, page, eframe, PROT_READ);
            disk_read(disk, page, page_table_get_physmem(pt) + eframe*BLOCK_SIZE);
        }
        else {
            // Not full, filling the frames
            page_table_set_entry(pt, page, current_frames, PROT_READ);
            current_frames++;
            disk_reads++;
            page_faults++;
        }
    }
    else if (bits == PROT_READ) {
        page_table_set_entry(pt,page,frame,PROT_READ|PROT_WRITE);
    }
}

int main(int argc, char *argv[])
{
    // Check argument count
    if (argc != 5)
    {
        cerr << "Usage: virtmem <npages> <nframes> <rand|fifo|custom> <sort|scan|focus>" << endl;
        exit(1);
    }

    // Parse command line arguments
    int npages = atoi(argv[1]);
    nframes = atoi(argv[2]);
    const char *algorithm = argv[3];
    const char *program_name = argv[4];

    // Validate the algorithm specified
    if ((strcmp(algorithm, "rand") != 0) &&
        (strcmp(algorithm, "fifo") != 0) &&
        (strcmp(algorithm, "custom") != 0))
    {
        cerr << "ERROR: Unknown algorithm: " << algorithm << endl;
        exit(1);
    }

    // Validate the program specified
    program_f program = NULL;
    if (!strcmp(program_name, "sort"))
    {
        if (nframes < 2)
        {
            cerr << "ERROR: nFrames >= 2 for sort program" << endl;
            exit(1);
        }

        program = sort_program;
    }
    else if (!strcmp(program_name, "scan"))
    {
        program = scan_program;
    }
    else if (!strcmp(program_name, "focus"))
    {
        program = focus_program;
    }
    else
    {
        cerr << "ERROR: Unknown program: " << program_name << endl;
        exit(1);
    }

    // Create a virtual disk
    disk = disk_open("myvirtualdisk", npages);
    if (!disk)
    {
        cerr << "ERROR: Couldn't create virtual disk: " << strerror(errno) << endl;
        return 1;
    }

    // Create a page table
    struct page_table *pt;
    if (strcmp(algorithm, "rand") == 0) {
        pt = page_table_create(npages, nframes, random_handler);
    } else if ((strcmp(algorithm, "fifo") == 0)) {
        pt = page_table_create(npages, nframes, fifo_handler);
    } else if ((strcmp(algorithm, "custom") == 0)){
        pt = page_table_create(npages, nframes, custom_handler);
    }
    if (!pt)
    {
        cerr << "ERROR: Couldn't create page table: " << strerror(errno) << endl;
        return 1;
    }

    // Run the specified program

    char *virtmem = page_table_get_virtmem(pt);
    program(virtmem, npages * PAGE_SIZE);

    // Print stats
    cout << "Status ---------------------------" << endl;
    cout << "Page Faults: " << page_faults << endl;
    cout << "Disk Reads: " << disk_reads << endl;
    cout << "Disk Writes: " << disk_writes << endl;
    cout << "----------------------------------" << endl;


    // Clean up the page table and disk
    page_table_delete(pt);
    disk_close(disk);

    return 0;
}
