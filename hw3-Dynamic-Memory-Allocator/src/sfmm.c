/**
 * All functions you make for the assignment must be implemented in this file.
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"
void insert_into_free_list(sf_block* free_list_head, sf_block* block_to_insert);
int get_free_list_index(int size);
sf_block* search_free_lists_and_disconnect_block(int blockSize, sf_block* free_list_head);
sf_block* free_block_allocate(int size_aligned, sf_block* freeBlockAvailable);
sf_block* merge_adjacent_free_blocks(sf_block* currentFreeBlock);
sf_block* extend_wilderness_block(int pagesSuccessfullyCreated);
void remove_block_from_free_lists(sf_block* freeBlock);
int is_allocated_block(sf_block* allocatedblock);
int is_wilderness_block(sf_block* freeBlock);
sf_block* free_block_split(sf_block* allocatedBlock, int remainder);
int isPowerOfTwo(int x);
int padTo64Multiple(int x);

sf_block* prologue;
sf_block* epilogue;

void *sf_malloc(size_t size) {
    if (size == 0) {
        return 0;
    }

    int size_and_header = size + 8;     // Need to account for header taking up 8 bytes
    int size_and_header_aligned = padTo64Multiple(size_and_header);  // size_and_header is padded to a multiple of 64 bytes (after the following if block is executed)

    // If the heap has not been initialized yet
    if (sf_mem_start() == sf_mem_end()) {
        for (int i = 0; i < NUM_FREE_LISTS; ++i) {
            sf_free_list_heads[i].header = 0;
            sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
            sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
        }
        sf_mem_grow();

        prologue = (sf_block*) (sf_mem_start() + (6 * 8));    // The Prologue starts at (heap start address) + (7 rows of padding * 8 bytes per row)
        prologue->header = 64;                              // Prologue is by default 8 rows (64 bytes) large with itself and "prev" being allocated
        prologue->header |= THIS_BLOCK_ALLOCATED;
        prologue->header |= PREV_BLOCK_ALLOCATED;

        sf_block* firstBlock = sf_mem_start() + (64 + 48);     // Prev_Footer of first free block (Prologue Footer) starts (8 + 6) = 14 rows after start of heap
        firstBlock->prev_footer = prologue->header;

        int totalAdditionalPages = 0;
        int prologue_padding_from_start = 7*8 + 8*8;   // This includes size of prologue and padding
        int epilogue_size = 8;
        int currentFreeBytes = PAGE_SZ - (prologue_padding_from_start + epilogue_size);

        if (currentFreeBytes < size_and_header_aligned) {   // This whole block will grow the heap because we do not have enough space
            int bytesNeeded = size_and_header_aligned - currentFreeBytes;
            int pages;
            if (bytesNeeded % PAGE_SZ == 0) {
                pages = bytesNeeded / PAGE_SZ;
            } else {
                pages = (int) (bytesNeeded / PAGE_SZ) + 1;  // We round up to the next page when deciding how many bytes we need
            }

            for (int i = 0; i < pages; ++i) {
                if (sf_mem_grow() == NULL) {    // If we cannot grow the heap anymore, we need to atleast properly set up the wilderness block, and epilogue
                    firstBlock->header = currentFreeBytes + (PAGE_SZ * i);
                    firstBlock->header |= PREV_BLOCK_ALLOCATED;

                    epilogue = (sf_block*) sf_mem_end() - 16;
                    epilogue->prev_footer = firstBlock->header;
                    epilogue->header = THIS_BLOCK_ALLOCATED;

                    insert_into_free_list(&sf_free_list_heads[9], firstBlock);
                    sf_errno = ENOMEM;
                    return NULL;
                }
            }
            totalAdditionalPages = pages;     // We have successfully created the total amount of pages
        }

        // After we successfully grow the heap, we need to allocate the required block and free block

        // firstBlock IS BECOMING AN ALLOCATED BLOCK NOW
        firstBlock->header = size_and_header_aligned;
        firstBlock->header |= PREV_BLOCK_ALLOCATED;
        firstBlock->header |= THIS_BLOCK_ALLOCATED;

        int freeBytesAfterAlloc = (PAGE_SZ * totalAdditionalPages + currentFreeBytes) - size_and_header_aligned;
        if (freeBytesAfterAlloc) {  // We need to check if there is any space left to allocate a free block

            sf_block* afterFirstBlock = (sf_block*) ((void*) firstBlock + size_and_header_aligned);

            afterFirstBlock->header = freeBytesAfterAlloc;
            afterFirstBlock->header |= PREV_BLOCK_ALLOCATED;

            // We also need to set the epilogue block
            epilogue = (sf_block*) ((void*) sf_mem_end() - 16);
            epilogue->prev_footer = afterFirstBlock->header;
            epilogue->header = THIS_BLOCK_ALLOCATED;

            // Put the new wilderness block in the last free list
            insert_into_free_list(&sf_free_list_heads[9], afterFirstBlock);
        } else {
            epilogue = (sf_block*) ((void*) sf_mem_end() - 16);
            epilogue->header = PREV_BLOCK_ALLOCATED;
            epilogue->header |= THIS_BLOCK_ALLOCATED;
        }
        return ((void*) firstBlock) + 16;     // (+16) moves us from start of footer to start of payload
    }

    sf_block* freeBlockAvailable = NULL;
    for (int i = get_free_list_index(size_and_header_aligned); i < NUM_FREE_LISTS; ++i) {
        freeBlockAvailable = search_free_lists_and_disconnect_block(size_and_header_aligned, &sf_free_list_heads[i]);
        if (freeBlockAvailable != NULL) {   // If we have successfully found a free block, we turn it into an allocated block
            freeBlockAvailable = free_block_allocate(size_and_header_aligned, freeBlockAvailable);
            return ((void*) freeBlockAvailable) + 16;
        }
    }

    if (!(freeBlockAvailable)) {
        int wildernessBlockSize = 0;
        if (!(epilogue->header & PREV_BLOCK_ALLOCATED)) {
            wildernessBlockSize = epilogue->prev_footer & BLOCK_SIZE_MASK;
        }

        int bytesNeeded = size_and_header_aligned - wildernessBlockSize;
        int pages;
        if (bytesNeeded % PAGE_SZ == 0) {
            pages = bytesNeeded / PAGE_SZ;
        } else {
            pages = (int) (bytesNeeded / PAGE_SZ) + 1;  // We round up to the next page when deciding how many bytes we need
        }

        int pagesSuccessfullyCreated = 0;
        for (int i = 0; i < pages; ++i) {
            if (sf_mem_grow() == NULL) {    // If we cannot grow the heap anymore after the heap has properly been initialized
                freeBlockAvailable = extend_wilderness_block(pagesSuccessfullyCreated);
                int freeBlockAvailableIndex = get_free_list_index(freeBlockAvailable->header & BLOCK_SIZE_MASK);
                insert_into_free_list(&sf_free_list_heads[freeBlockAvailableIndex], freeBlockAvailable);
                sf_errno = ENOMEM;
                return NULL;
            }
            pagesSuccessfullyCreated += 1;
        }

        sf_block* extendedWildernessBlock = extend_wilderness_block(pagesSuccessfullyCreated);
        remove_block_from_free_lists(extendedWildernessBlock);
        sf_block* allocatedBlock = free_block_allocate(size_and_header_aligned, extendedWildernessBlock);
        return ((void*) allocatedBlock + 16);
    }

    return NULL;
}

void sf_free(void *pp) {
    if (!pp) {
        abort();
    }
    if ((uintptr_t) pp % 64 != 0) {  // If the payload address is not a multiple of 64, then it cannot be a valid payload address
        abort();
    }
    sf_block* allocatedBlock = (sf_block*) (pp - 16);
    if (!(is_allocated_block(allocatedBlock))) {
        abort();
    }

    allocatedBlock->header -= 1;    // Setting the allocated bit to 0 and this "allocatedBlock" to a freeBlock

    int allocatedBlockSize = allocatedBlock->header & BLOCK_SIZE_MASK;
    sf_block* nextBlock = (sf_block*) ((void*) allocatedBlock + allocatedBlockSize);
    nextBlock->header -= nextBlock->header & PREV_BLOCK_ALLOCATED;
    nextBlock->prev_footer = allocatedBlock->header;
    if (nextBlock != epilogue) {
        if (!(nextBlock->header & THIS_BLOCK_ALLOCATED)) {
            sf_block* nextNextBlock = (sf_block*) ((void*) nextBlock + (nextBlock->header & BLOCK_SIZE_MASK));
            nextNextBlock->prev_footer = nextBlock->header;
        }
    }

    allocatedBlock = merge_adjacent_free_blocks(allocatedBlock);
    return;
}

void *sf_realloc(void *pp, size_t rsize) {
    if (!pp) {
        sf_errno = EINVAL;
        return NULL;
    }
    sf_block* block = (sf_block*) (pp - 16);
    if (!is_allocated_block(block)) {
        sf_errno = EINVAL;
        return NULL;
    }
    if (!(rsize)) {
        sf_free(pp);
        return NULL;
    }
    int size_and_header = rsize + 8;
    int padding = 0;
    if (size_and_header % 64 != 0) {
        padding = 64 - (size_and_header % 64);
    }
    int size_and_header_aligned = size_and_header + padding;

    int blockSize = block->header & BLOCK_SIZE_MASK;
    if (blockSize == size_and_header_aligned) {
        return pp;
    } else if (blockSize > size_and_header_aligned) {
        block = free_block_allocate(size_and_header_aligned, block);
        return ((void*) block + 16);
    } else if (blockSize < size_and_header_aligned) {
        void* newBlock = sf_malloc(rsize);
        if (!(newBlock)) {
            return NULL;
        }
        memcpy(newBlock, pp, (blockSize - 8));
        sf_free(pp);
        return newBlock;
    }
    return NULL;
}

// We are malloc'ing size and want to ensure the payload address is aligned (multiple of align)
void *sf_memalign(size_t size, size_t align) {
    if (!(size)) {
        return NULL;
    }
    if (!(isPowerOfTwo(align)) || (align < 64)) {
        sf_errno = EINVAL;
        return NULL;
    }
    void* alignedBlockPayload = sf_malloc(size + align + 64);
    sf_block* firstBlock = (sf_block*) ((void*) alignedBlockPayload - 16);  // This may or may not be the free block that we need to have before the aligned allocated block
    int alignedBlockSize = padTo64Multiple(size + align + 64 + 8);
    int memalignMalloced = padTo64Multiple(size + 8);
    int prevBytesToFree = 0;
    while ((uintptr_t) alignedBlockPayload % align != 0) {
        alignedBlockPayload += 64;
        prevBytesToFree += 64;
    }
    sf_block* memalignMallocBlock = (sf_block*) ((void*) alignedBlockPayload - 16);
    memalignMallocBlock->header = (memalignMallocBlock->header & PREV_BLOCK_ALLOCATED);
    memalignMallocBlock->header += memalignMalloced;
    memalignMallocBlock->header |= THIS_BLOCK_ALLOCATED;

    if (prevBytesToFree) {  // THIS MEANS THAT THE PAYLOAD WAS NOT ALIGNED TO BEGIN WITH. WE NEED TO CREATE A FREE BLOCK
        firstBlock->header = firstBlock->header & PREV_BLOCK_ALLOCATED;
        firstBlock->header |= THIS_BLOCK_ALLOCATED;
        firstBlock->header += prevBytesToFree;
        sf_free((void*) firstBlock + 16);
    }

    int lastBlockToFreeSize = alignedBlockSize - prevBytesToFree - memalignMalloced;
    if (lastBlockToFreeSize) {  // If there is a block we need to free in front
        sf_block* lastBlock = (sf_block*) ((void*) memalignMallocBlock + (memalignMallocBlock->header & BLOCK_SIZE_MASK));
        lastBlock->header = PREV_BLOCK_ALLOCATED;
        lastBlock->header |= THIS_BLOCK_ALLOCATED;
        lastBlock->header += lastBlockToFreeSize;
        sf_free((void*) lastBlock + 16);
    }

    return ((void*) memalignMallocBlock + 16);
}

int padTo64Multiple(int x) {
    if (x % 64 != 0) {
        int padding  = 64 - (x % 64);
        x = x + padding;
        return x;
    } else {
        return x;
    }
}

int isPowerOfTwo(int x) {
    return (x != 0) && ((x & (x - 1)) == 0);
}


sf_block* free_block_split(sf_block* allocatedBlock, int remainder) {
    int allocatedBlockSize = allocatedBlock->header & BLOCK_SIZE_MASK;
    sf_block* splittedFreeBlock = (sf_block*) ((void*) allocatedBlock + allocatedBlockSize);
    splittedFreeBlock->header = remainder;
    splittedFreeBlock->header |= PREV_BLOCK_ALLOCATED;

    sf_block* nextBlockAfterSplit = (sf_block*) ((void*) splittedFreeBlock + remainder);
    nextBlockAfterSplit->prev_footer = splittedFreeBlock->header;
    if (nextBlockAfterSplit == epilogue) {
        nextBlockAfterSplit->header = 1;    // Setting the epilogue to allocated and prev-allocated to 0
    } else {
        nextBlockAfterSplit->header -= splittedFreeBlock->header & PREV_BLOCK_ALLOCATED;
        if (!(nextBlockAfterSplit->header & THIS_BLOCK_ALLOCATED)) {
            sf_block* nextNextBlockAfterSplit = (sf_block*) ((void*) nextBlockAfterSplit + (nextBlockAfterSplit->header & BLOCK_SIZE_MASK));
            nextNextBlockAfterSplit->prev_footer = nextBlockAfterSplit->header;
        }
    }
    return splittedFreeBlock;
}

void remove_block_from_free_lists(sf_block* freeBlock) {
    freeBlock->body.links.prev->body.links.next = freeBlock->body.links.next;
    freeBlock->body.links.next->body.links.prev = freeBlock->body.links.prev;
}

int is_wilderness_block(sf_block* freeBlock) {
    int freeBlockSize = freeBlock->header & BLOCK_SIZE_MASK;
    sf_block* nextBlock = (sf_block*) ((void*) freeBlock + freeBlockSize);
    return (nextBlock == epilogue) ? 1 : 0;
}

int is_allocated_block(sf_block* allocatedBlock) {
    // Block not allocated check
    if (!(allocatedBlock->header & THIS_BLOCK_ALLOCATED)) {
        return 0;
    }

    // Blocksize is not a multiple of 64 check
    int allocatedBlockSize = allocatedBlock->header & BLOCK_SIZE_MASK;
    if (allocatedBlockSize % 64 != 0) {
        return 0;
    }

    // Out of bounds check
    sf_block* allocatedBlockHeaderAddress = (sf_block*) ((void*) allocatedBlock + 8);
    sf_block* allocatedBlockFooterAddress = (sf_block*) ((void*) allocatedBlock + allocatedBlockSize);
    if ((allocatedBlockHeaderAddress < (sf_block*) ((void*) prologue + 72)) || (allocatedBlockFooterAddress > (sf_block*) (void*) epilogue - 8)) {
        return 0;
    }

    int isPrevAllocated = (allocatedBlock->header & PREV_BLOCK_ALLOCATED);
    if (isPrevAllocated) {
        int prevBlockSize = (allocatedBlock->prev_footer & BLOCK_SIZE_MASK);
        sf_block* prevBlock = (sf_block*) ((void*) allocatedBlock - prevBlockSize);
        int isPrevActuallyAllocated = prevBlock->header & THIS_BLOCK_ALLOCATED;
        if (!isPrevActuallyAllocated) {
            return 0;
        }
    }
    return 1;
}

sf_block* extend_wilderness_block(int pagesSuccessfullyCreated) {
    sf_block* newWildernessBlock = epilogue;
    newWildernessBlock->header = (newWildernessBlock->header & PREV_BLOCK_ALLOCATED);
    newWildernessBlock->header += PAGE_SZ * pagesSuccessfullyCreated;

    epilogue = (sf_block*) ((void*) sf_mem_end() - 16);
    epilogue->prev_footer = newWildernessBlock->header;
    epilogue->header = THIS_BLOCK_ALLOCATED;
    newWildernessBlock = merge_adjacent_free_blocks(newWildernessBlock);
    return newWildernessBlock;
}

sf_block* merge_adjacent_free_blocks(sf_block* freeBlock) {
    int freeBlockSize = freeBlock->header & BLOCK_SIZE_MASK;
    sf_block* prevBlock = (sf_block*) (((void*) freeBlock) - (freeBlock->prev_footer & BLOCK_SIZE_MASK));
    int prevBlockAllocated = freeBlock->header & PREV_BLOCK_ALLOCATED;
    //int prevBlockSize = freeBlock->prev_footer & BLOCK_SIZE_MASK;
    sf_block* nextBlock = (sf_block*) (((void*) freeBlock) + freeBlockSize);
    int nextBlockAllocated = nextBlock->header & THIS_BLOCK_ALLOCATED;
    int nextBlockSize = nextBlock->header & BLOCK_SIZE_MASK;

    if (!(nextBlockAllocated)) {
        remove_block_from_free_lists(nextBlock);
        freeBlockSize += nextBlockSize;
        freeBlock->header = freeBlockSize;
        sf_block* nextBlockAfterMerge = (((void*) nextBlock) + nextBlockSize);
        nextBlockAfterMerge->prev_footer = freeBlock->header;
        nextBlock = nextBlockAfterMerge;
    }
    if (!(prevBlockAllocated)) {
        remove_block_from_free_lists(prevBlock);
        prevBlock->header += freeBlockSize;
        freeBlock = prevBlock;
        nextBlock->prev_footer = freeBlock->header;
    }
    if (is_wilderness_block(freeBlock)) {
        insert_into_free_list(&sf_free_list_heads[9], freeBlock);
    } else {
        int freeBlockListIndex = get_free_list_index(freeBlock->header & BLOCK_SIZE_MASK);  // We add the merged block back into the free list
        insert_into_free_list(&sf_free_list_heads[freeBlockListIndex], freeBlock);
    }
    return freeBlock;
}

sf_block* free_block_allocate(int size_aligned, sf_block* freeBlockAvailable) {
    int freeBlockSize = freeBlockAvailable->header & BLOCK_SIZE_MASK;
    int remainder = freeBlockSize - size_aligned;
    if (!(remainder)) {     // If the size of the block that we need to allocate perfectly fits the freeBlock that we obtained from the freeLists
        freeBlockAvailable->header |= THIS_BLOCK_ALLOCATED;
        sf_block* nextBlock = (sf_block*) (((void*) freeBlockAvailable) + freeBlockSize);
        nextBlock->header |= PREV_BLOCK_ALLOCATED;
        return freeBlockAvailable;
    } else {
        freeBlockAvailable->header = (freeBlockAvailable->header & PREV_BLOCK_ALLOCATED);
        freeBlockAvailable->header |= THIS_BLOCK_ALLOCATED;
        freeBlockAvailable->header += size_aligned;

        sf_block* newFreeSplittedBlock = free_block_split(freeBlockAvailable, remainder);
        newFreeSplittedBlock = merge_adjacent_free_blocks(newFreeSplittedBlock);
        return freeBlockAvailable;
    }
    return NULL;
}

// Searches the free lists for the best possible fit available block. If we find one, we disconnect it's links to next and prev free blocks
sf_block* search_free_lists_and_disconnect_block(int blockSize, sf_block* free_list_head) {
    // If the free list is empty
    if (free_list_head->body.links.next == free_list_head && free_list_head->body.links.prev == free_list_head) {
        return NULL;
    }
    sf_block* curs = free_list_head;
    while (curs->body.links.next != free_list_head) {
        curs = curs->body.links.next;
        if ((curs->header & BLOCK_SIZE_MASK) >= blockSize) {
            curs->body.links.prev->body.links.next = curs->body.links.next;
            curs->body.links.next->body.links.prev = curs->body.links.prev;
            return curs;
        }
    }
    return NULL;
}

void insert_into_free_list(sf_block* free_list_head, sf_block* block_to_insert) {
    // If the free list is empty
    if (free_list_head->body.links.prev == free_list_head && free_list_head->body.links.next == free_list_head) {
        free_list_head->body.links.next = block_to_insert;
        block_to_insert->body.links.prev = free_list_head;
        free_list_head->body.links.prev = block_to_insert;
        block_to_insert->body.links.next = free_list_head;
    } else {
        free_list_head->body.links.next->body.links.prev = block_to_insert;
        block_to_insert->body.links.next = free_list_head->body.links.next;
        free_list_head->body.links.next = block_to_insert;
        block_to_insert->body.links.prev = free_list_head;
    }
}

int get_free_list_index(int size) {
    int M = 64;
    if (size == M) { return 0; }
    else if (size>M && size<=2*M) { return 1; }
    else if (size>2*M && size<=3*M) { return 2; }
    else if (size>3*M && size<=5*M) { return 3; }
    else if (size>5*M && size<=8*M) { return 4; }
    else if (size>8*M && size<=13*M) { return 5; }
    else if (size>13*M && size<=21*M) { return 6;}
    else if (size>21*M && size<=34*M) { return 7; }
    else if (size>34*M) { return 8; }
    else { return -1; }
}