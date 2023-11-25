#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

typedef unsigned long ul;

const long fourMB = (4 * 1024 * 1024);
long *memory = NULL;

long getnearest4mb(ul size)
{
    return ((size + 8 + fourMB - 1) / fourMB) * fourMB;
}

void *memalloc(unsigned long size)
{
    if (size <= 0)
    {
        return NULL;
    }
    ul num_moved = 3;
    if (num_moved < ((size + 15) / 8))
    {
        num_moved = ((size + 15) / 8);
    }
    ul aligned_size = num_moved * 8;
    // printf("%lu\n", aligned_size);
    if (memory == NULL)
    {
        memory = (long *)mmap(NULL, getnearest4mb(size), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        *(memory) = 8 * num_moved;
        long *toreturn = memory + 1;
        memory = memory + num_moved;
        if (getnearest4mb(size) > (num_moved * 8))
        {
            *(memory) = getnearest4mb(size) - (num_moved * 8);
            *(memory + 1) = (ul)(NULL);
            *(memory + 2) = (ul)(NULL);
        }
        else
        {
            memory = NULL;
        }
        // printf("%lu, %lu\n", *(memory+1), *(toreturn - 1));
        return (void *)toreturn;
    }
    long *tmp = memory;
    long *prev = NULL;
    // printf("%lu\n", aligned_size);
    // printf("%ld\n", *tmp);
    while (tmp)
    {
        // printf("loop ke andar\n");
        long *next = (long *)(*(tmp + 1));
        if (*tmp >= aligned_size + 24)
        {
            // printf("pehla if\n");
            ul remsize = *tmp - aligned_size;
            *tmp = aligned_size;
            long *toreturn = tmp + 1;
            if (next != NULL)
            {
                *(next + 2) = (ul)prev;
            }
            if (prev != NULL)
            {
                *(prev + 1) = (ul)next;
            }
            if (memory == tmp)
            {
                memory = tmp + num_moved;
                *memory = remsize;
                *(memory + 1) = *(tmp + 1);
                *(memory + 2) = (ul)(NULL);
            }
            else
            {
                tmp = tmp + num_moved;
                *(tmp + 1) = (ul)memory;
                *tmp = remsize;
                if (memory != NULL)
                {
                    *(memory + 2) = (ul)tmp;
                }
                memory = tmp;
            }
            return (void *)(toreturn);
        }
        else if (*tmp >= aligned_size)
        {
            // printf("dusraa if\n");
            if (memory == tmp)
            {
                memory = next;
                if (memory != NULL)
                {
                    *(memory + 2) = (ul)prev;
                }
            }
            if (prev != NULL)
            {
                *(prev + 1) = (ul)next;
            }
            if (next != NULL)
            {
                *(next + 2) = (ul)prev;
            }
            return (void *)(tmp + 1);
        }
        else
        {
            // printf("nhi mila size\n");
            prev = tmp;
            tmp = (long *)(*(tmp + 1));
        }
    }
    // printf("nayi memoery\n");
    long *new_chunk = (long *)mmap(NULL, getnearest4mb(size), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    *new_chunk = aligned_size;
    long *toreturn = new_chunk + 1;
    new_chunk = new_chunk + num_moved;
    *(new_chunk + 1) = (ul)(memory);
    *(new_chunk + 2) = (ul)(NULL);
    *new_chunk = getnearest4mb(size) - aligned_size;
    if (memory != NULL)
    {
        *(memory + 2) = (ul)(new_chunk);
    }
    memory = new_chunk;
    return (void *)toreturn;
}

int memfree(void *ptr)
{
    long *tmp = memory;
    // while (tmp)
    // {
    //     printf("%lu\n", *tmp);
    //     tmp = (long *)(*(tmp + 1));
    // }
    // tmp = memory;
    if (ptr == NULL)
    {
        return -1;
    }
    long *block = (long *)ptr;
    block--;
    ul size = *block;
    long *old_head = memory;
    long *right = NULL;
    long *left = NULL;
    tmp = memory;
    while (tmp)
    {

        if (tmp + (*tmp) / 8 == block)
        {
            // printf("left mil gya\n");
            left = tmp;
            break;
        }
        tmp = (long *)(*(tmp + 1));
    }
    tmp = memory;
    while (tmp)
    {
        if (block + (*block) / 8 == tmp)
        {
            // printf("right mil gya\n");
            right = tmp;
            break;
        }
        tmp = (long *)(*(tmp + 1));
    }
    if (right != NULL)
    {
        // printf("right merge\n");
        long *prev = (long *)(*(right + 2));
        long *next = (long *)(*(right + 1));
        if (right == memory)
        {
            memory = next;
            if (memory)
            {
                *(memory + 2) = (ul)prev;
            }
        }
        else
        {
            if (prev != NULL)
            {
                *(prev + 1) = (ul)next;
            }
            if (next != NULL)
            {
                *(next + 2) = (ul)prev;
            }
        }
        *block = *block + *right;
        *(block + 1) = *(right + 1);
    }
    if (left != NULL)
    {
        // printf("left merge\n");
        long *prev = (long *)(*(left + 2));
        long *next = (long *)(*(left + 1));
        if (left == memory)
        {
            memory = next;
            if (memory)
            {
                *(memory + 2) = (ul)prev;
            }
        }
        else
        {
            if (prev != NULL)
            {
                *(prev + 1) = (ul)next;
            }
            if (next != NULL)
            {
                *(next + 2) = (ul)prev;
            }
        }
        *left = *left + *block;
        block = left;
    }
    if (memory == NULL)
    {
        memory = block;
        *memory = *block;
        *(memory + 1) = (ul)(NULL);
        *(memory + 2) = (ul)(NULL);
        return 0;
    }
    *(block + 1) = (ul)memory;
    *(block + 2) = (ul)(NULL);
    *(memory + 2) = (ul)(block);
    memory = block;
    return 0;
}
