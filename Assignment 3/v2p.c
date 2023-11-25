#include <types.h>
#include <mmap.h>
#include <fork.h>
#include <v2p.h>
#include <page.h>
#define MY_PAGE_SIZE 4096

void flush_tlbs(void)
{
    u64 cr3_value;
    asm volatile(
        "mov %%cr3, %0" // Move the value from CR3 into the variable
        : "=r"(cr3_value));

    asm volatile(
        "mov %0, %%rax\n\t" // Move the CR3 value into RAX
        "mov %%rax, %%cr3"  // Move the content of RAX into CR3
        :
        : "r"(cr3_value)
        : "eax");
}

u64 page_aligned_len(u64 len)
{
    return ((len + MY_PAGE_SIZE - 1) / MY_PAGE_SIZE) * MY_PAGE_SIZE;
}

int modify_permission_page(u64 addr, int permission)
{
    struct exec_context *current = get_current_ctx();

    u64 *pgd_virtual = (u64 *)(osmap(current->pgd));
    u64 number = addr;
    u64 mask9 = (1 << 9) - 1;
    u64 mask12 = (1 << 12) - 1;

    u64 pfn_segment_va = number & mask12;
    number >>= 12;
    u64 pte_segment_va = number & mask9;
    number >>= 9;
    u64 pmd_segment_va = number & mask9;
    number >>= 9;
    u64 pud_segment_va = number & mask9;
    number >>= 9;
    u64 pgd_segment_va = number & mask9;
    number >>= 9;

    // pgd offset adding
    u64 *pgd_loc = pgd_virtual + pgd_segment_va;
    // printk("at pgd location %x\n", *pgd_loc);
    if ((*pgd_loc) & 1)
    {
        // present bit is 1
        if ((permission & PROT_WRITE) != 0)
        {
            *(pgd_loc) = (*pgd_loc) | 8;
        }
    }
    else
    {
        return 0; // present bit not found so nothing done
    }
    // printk("%x %x\n", pgd_loc, *pgd_loc);

    // pud offset adding
    u64 *pud_loc = (u64 *)osmap(((*pgd_loc) >> 12)) + pud_segment_va;
    // printk("at pud location %x\n", *pud_loc);
    if ((*pud_loc) & 1)
    {
        // present bit is 1
        if ((permission & PROT_WRITE) != 0)
        {
            *(pud_loc) = (*pud_loc) | 8;
        }
    }
    else
    {
        return 0; // present bit not found so nothing done
    }
    // printk("%x %x\n", pud_loc, *pud_loc);

    // pmd offset adding
    u64 *pmd_loc = (u64 *)osmap(((*pud_loc) >> 12)) + pmd_segment_va;
    // printk("at pmd location %x\n", *pmd_loc);
    if ((*pmd_loc) & 1)
    {
        // present bit is 1
        if ((permission & PROT_WRITE) != 0)
        {
            *(pmd_loc) = (*pmd_loc) | 8;
        }
    }
    else
    {
        return 0; // present bit not found so nothing done
    }
    // printk("%x %x\n", pmd_loc, *pmd_loc);

    // pte offset adding
    u64 *pte_loc = (u64 *)osmap(((*pmd_loc) >> 12)) + pte_segment_va;
    // printk("at pte location %x\n", *pte_loc);
    if ((*pte_loc) & 1)
    {
        // present bit is 1
        if ((permission & PROT_WRITE) != 0)
        {
            u64 shared_pfn = (*pte_loc);
            u32 shared_pfn_physical = (shared_pfn >> 12);
            // printk("ref count %d\n", get_pfn_refcount(shared_pfn_physical));
            if (get_pfn_refcount(shared_pfn_physical) == 1)
            {
                *(pte_loc) = (*pte_loc) | 8;
            }
        }
        else
        {
            u64 oneull = 1;
            u64 mask = ~(oneull << 3);
            *(pte_loc) = (*pte_loc) & mask;
        }
    }
    else
    {
        return 0; // present bit not found so nothing done
    }
    // printk("%x %x\n", pte_loc, *pte_loc);

    return 0;
}

int modify_permission(u64 start, u64 end, int permission)
{
    struct exec_context *current = get_current_ctx();
    struct vm_area *cur = NULL;
    for (; start < end; start += MY_PAGE_SIZE)
    {
        cur = current->vm_area->vm_next;
        while (cur != NULL)
        {
            if (cur->vm_start <= start && cur->vm_end >= end)
            {
                modify_permission_page(start, permission);
            }
            cur = cur->vm_next;
        }
    }

    // TLB flushing
    flush_tlbs();
    return 0;
}

int my_free_page(u64 addr)
{
    struct exec_context *current = get_current_ctx();

    u64 *pgd_virtual = (u64 *)(osmap(current->pgd));
    u64 number = addr;
    u64 mask9 = (1 << 9) - 1;
    u64 mask12 = (1 << 12) - 1;

    u64 pfn_segment_va = number & mask12;
    number >>= 12;
    u64 pte_segment_va = number & mask9;
    number >>= 9;
    u64 pmd_segment_va = number & mask9;
    number >>= 9;
    u64 pud_segment_va = number & mask9;
    number >>= 9;
    u64 pgd_segment_va = number & mask9;
    number >>= 9;

    // pgd offset adding
    u64 *pgd_loc = pgd_virtual + pgd_segment_va;
    // printk("at pgd location %x\n", *pgd_loc);
    if (!((*pgd_loc) & 1))
    {
        return 0;
    }

    // pud offset adding
    u64 *pud_loc = (u64 *)osmap(((*pgd_loc) >> 12)) + pud_segment_va;
    // printk("at pud location %x\n", *pud_loc);
    if (!((*pud_loc) & 1))
    {
        return 0;
    }
    // printk("%x %x\n", pud_loc, *pud_loc);

    // pmd offset adding
    u64 *pmd_loc = (u64 *)osmap(((*pud_loc) >> 12)) + pmd_segment_va;
    // printk("at pmd location %x\n", *pmd_loc);
    if (!((*pmd_loc) & 1))
    {
        return 0;
    }
    // printk("%x %x\n", pmd_loc, *pmd_loc);

    // pte offset adding
    u64 *pte_loc = (u64 *)osmap(((*pmd_loc) >> 12)) + pte_segment_va;
    // printk("at pte location %x\n", *pte_loc);
    if (!((*pte_loc) & 1))
    {
        return 0;
    }
    // printk("%x %x\n", pte_loc, *pte_loc);

    u64 pfn_to_free = (*pte_loc) >> 12;
    put_pfn(pfn_to_free);
    if (get_pfn_refcount(pfn_to_free) == 0)
    {
        os_pfn_free(USER_REG, pfn_to_free);
    }
    *(pte_loc) = 0x0;

    return 0;
}

int my_free_pfns(u64 start, u64 end)
{
    struct exec_context *current = get_current_ctx();
    for (; start < end; start += MY_PAGE_SIZE)
    {
        my_free_page(start);
    }

    // TLB flushing
    flush_tlbs();
    return 0;
}

int count_vm_areas(struct exec_context *current)
{
    struct vm_area *tmp = current->vm_area;
    int cnt = 0;
    while (tmp != NULL)
    {
        cnt++;
        tmp = tmp->vm_next;
    }
    return cnt;
}

void insertNodeSorted(struct vm_area **head, struct vm_area *newnode)
{

    if (*head == NULL || (*head)->vm_start >= newnode->vm_start)
    {
        newnode->vm_next = *head;
        *head = newnode;
    }
    else
    {
        struct vm_area *current = *head;
        while (current->vm_next != NULL && current->vm_next->vm_start < newnode->vm_start)
        {
            current = current->vm_next;
        }
        newnode->vm_next = current->vm_next;
        current->vm_next = newnode;
    }
}

void merge_vm_areas(struct exec_context *current)
{
    struct vm_area *cur = current->vm_area->vm_next;

    while (cur != NULL && cur->vm_next != NULL)
    {
        if (cur->access_flags == cur->vm_next->access_flags &&
            cur->vm_end == cur->vm_next->vm_start)
        {
            // merge the nodes
            // printk("merging at point %x\n", cur->vm_end);
            cur->vm_end = cur->vm_next->vm_end;
            struct vm_area *temp = cur->vm_next;
            cur->vm_next = cur->vm_next->vm_next;
            os_free(temp, sizeof(temp));
        }
        else
        {
            cur = cur->vm_next;
        }
    }
}

// function to delete nodes with the same start and end
void delete_same_start_end(struct exec_context *current)
{
    struct vm_area *curr = current->vm_area;
    struct vm_area *prev = NULL;

    while (curr != NULL)
    {
        if (curr->vm_start == curr->vm_end)
        {
            if (prev == NULL)
            {
                current->vm_area = curr->vm_next;
                os_free(curr, sizeof(curr));
                curr = current->vm_area;
            }
            else
            {
                prev->vm_next = curr->vm_next;
                os_free(curr, sizeof(curr));
                curr = prev->vm_next;
            }
        }
        else
        {
            prev = curr;
            curr = curr->vm_next;
        }
    }
}

void printvma(struct vm_area *v)
{
    struct vm_area *tmp = v;
    while (tmp)
    {
        printk("start: %x\t end: %x\n", tmp->vm_start, tmp->vm_end);
        tmp = tmp->vm_next;
    }
}

long vm_area_map(struct exec_context *current, u64 addr, int length, int prot, int flags)
{
    if (length < 0)
    {
        return -1;
    }
    if (prot != PROT_READ && prot != (PROT_WRITE | PROT_READ))
    {
        return -1;
    }
    if (flags != 0 && flags != MAP_FIXED)
    {
        return -1;
    }
    if (addr % MY_PAGE_SIZE != 0)
    {
        return -1;
    }
    long return_to_user = -1;
    struct vm_area *head_vmar = current->vm_area;
    if (head_vmar == NULL)
    {
        head_vmar = (struct vm_area *)os_alloc(sizeof(struct vm_area));
        if (head_vmar == NULL)
        {
            return -1; // fault in os allocation
        }
        head_vmar->vm_start = MMAP_AREA_START;
        head_vmar->vm_end = MMAP_AREA_START + MY_PAGE_SIZE;
        head_vmar->access_flags = 0x0;
        head_vmar->vm_next = NULL;
        current->vm_area = head_vmar;
    }
    u64 len_to_be_allocated = page_aligned_len(length);
    if (len_to_be_allocated > 2 * 1024 * 1024)
    {
        return -1;
    }
    if ((void *)addr == NULL) // no hint addr given
    {
        if (flags == MAP_FIXED)
        {
            return -1;
        }
        struct vm_area *head_vm = current->vm_area;
        struct vm_area *newnode = NULL;
        int inserted_in_between = 0;
        while (head_vm != NULL && head_vm->vm_next != NULL)
        {
            u64 nextarea_start_aligned = (head_vm->vm_next->vm_start / MY_PAGE_SIZE) * MY_PAGE_SIZE;
            u64 prevarea_end_aligned = ((head_vm->vm_end + MY_PAGE_SIZE - 1) / MY_PAGE_SIZE) * MY_PAGE_SIZE;
            if (nextarea_start_aligned - prevarea_end_aligned >= len_to_be_allocated)
            {
                newnode = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                if (newnode == NULL)
                {
                    return -1; // fault in os allocation
                }
                newnode->vm_start = prevarea_end_aligned;
                newnode->access_flags = prot;
                newnode->vm_end = prevarea_end_aligned + len_to_be_allocated;
                newnode->vm_next = head_vm->vm_next;
                head_vm->vm_next = newnode;
                inserted_in_between = 1;
                return_to_user = newnode->vm_start; // assign what is to be returned
                break;
            }
            head_vm = head_vm->vm_next;
        }
        if (!inserted_in_between) // no suitable memory found in between
        {
            // printk("first time should be here %d\n", len_to_be_allocated);
            u64 prevarea_end_aligned = ((head_vm->vm_end + MY_PAGE_SIZE - 1) / MY_PAGE_SIZE) * MY_PAGE_SIZE;
            // printk("end %x\n" , prevarea_end_aligned + len_to_be_allocated);
            if (prevarea_end_aligned + len_to_be_allocated > MMAP_AREA_END)
            {
                return -1;
            }
            newnode = (struct vm_area *)os_alloc(sizeof(struct vm_area));
            if (newnode == NULL)
            {
                return -1; // fault in os allocation
            }
            newnode->vm_start = prevarea_end_aligned;
            newnode->access_flags = prot;
            newnode->vm_end = prevarea_end_aligned + len_to_be_allocated;
            newnode->vm_next = head_vm->vm_next;
            head_vm->vm_next = newnode;
            return_to_user = newnode->vm_start; // assign what is to be returned
        }
        struct vm_area *tmp = current->vm_area;
        // mergeNodes(current, (u64)(newnode)); // merge if possible
        merge_vm_areas(current);
    }
    else // hint addr given
    {
        if (!(MMAP_AREA_START <= addr && addr + len_to_be_allocated <= MMAP_AREA_END))
        {
            return -1; // not in limit
        }
        int overlap_found = 0;
        struct vm_area *tmp = current->vm_area->vm_next; // first useful node
        while (tmp != NULL)
        {
            if (tmp->vm_end > addr && addr + len_to_be_allocated > tmp->vm_start)
            {
                overlap_found = 1;
                break;
            }
            tmp = tmp->vm_next;
        }
        if (overlap_found == 1)
        {
            if (flags == MAP_FIXED) // fixed addr
            {
                return -1;
            }
            else // insert at the lowest possible position
            {
                return_to_user = vm_area_map(current, 0, len_to_be_allocated, prot, 0);
            }
        }
        else
        {
            struct vm_area *newnode = (struct vm_area *)os_alloc(sizeof(struct vm_area));
            if (newnode == NULL)
            {
                return -1; // fault in os allocation
            }
            newnode->vm_start = addr;
            newnode->access_flags = prot;
            newnode->vm_end = addr + len_to_be_allocated;
            newnode->vm_next = NULL;
            return_to_user = newnode->vm_start;
            insertNodeSorted(&head_vmar, newnode);
            merge_vm_areas(current);
        }
    }
    delete_same_start_end(current);
    stats->num_vm_area = count_vm_areas(current);
    // printk("mmap calling printvma\n");
    // printvma(current->vm_area);
    return return_to_user;
}

/**
 * munmap system call implemenations
 */

void printvm(struct vm_area *v)
{
    printk("node func start: %x\t end: %x\n", v->vm_start, v->vm_end);
}

long vm_area_mprotect(struct exec_context *current, u64 addr, int length, int prot)
{
    if (length < 0)
    {
        return -1;
    }
    if (prot != PROT_READ && prot != (PROT_WRITE | PROT_READ))
    {
        return -1;
    }
    if (addr % MY_PAGE_SIZE != 0)
    {
        return -1;
    }
    // printvma(current->vm_area);
    u64 len_to_be_removed = page_aligned_len(length);
    u64 start = addr;
    u64 end = addr + len_to_be_removed;
    // printk("Start %x\tend%x\n", start, end);
    struct vm_area *head_vm = current->vm_area->vm_next;
    struct vm_area *prev = current->vm_area;
    while (head_vm != NULL)
    {
        // printvm(head_vm);
        if (head_vm->vm_start <= start && head_vm->vm_end >= end)
        {
            if (start == head_vm->vm_start)
            {
                if (end == head_vm->vm_end)
                {
                    head_vm->access_flags = prot;
                    break;
                }
                else
                {
                    // printk("should be here\n ");
                    u64 tmpend = head_vm->vm_end;
                    head_vm->vm_end = end;
                    struct vm_area *newnode = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                    if (newnode == NULL)
                    {
                        return -1; // fault in os allocation
                    }
                    newnode->vm_start = end;
                    newnode->access_flags = head_vm->access_flags;
                    newnode->vm_end = tmpend;
                    newnode->vm_next = head_vm->vm_next;
                    head_vm->vm_next = newnode;
                    head_vm->access_flags = prot;
                    break;
                    // printvm(head_vm);
                    // printvm(newnode);
                }
            }
            else if (end == head_vm->vm_end)
            {
                u64 tmpstart = head_vm->vm_start;
                head_vm->vm_start = start;
                struct vm_area *newnode = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                if (newnode == NULL)
                {
                    return -1; // fault in os allocation
                }
                newnode->vm_start = tmpstart;
                newnode->access_flags = head_vm->access_flags;
                prev->vm_next = newnode;
                newnode->vm_end = head_vm->vm_start;
                newnode->vm_next = head_vm;
                head_vm->access_flags = prot;
                break;
            }
            else
            {
                struct vm_area *newnode = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                if (newnode == NULL)
                {
                    return -1; // fault in os allocation
                }
                newnode->vm_start = start; // 4
                newnode->vm_end = end;     // 5
                newnode->access_flags = prot;

                struct vm_area *newnode2 = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                if (newnode2 == NULL)
                {
                    return -1; // fault in os allocation
                }
                newnode2->vm_start = end; // 5
                newnode2->vm_next = head_vm->vm_next;
                newnode2->access_flags = head_vm->access_flags;
                newnode2->vm_end = head_vm->vm_end;
                head_vm->vm_end = start; // 4
                newnode->vm_next = newnode2;
                head_vm->vm_next = newnode;
                break;
            }
        }
        else if (start <= head_vm->vm_start && head_vm->vm_end <= end)
        {
            head_vm->access_flags = prot;
        }
        else if (start <= head_vm->vm_start && head_vm->vm_end >= end && head_vm->vm_start <= end)
        {
            struct vm_area *newnode = (struct vm_area *)os_alloc(sizeof(struct vm_area));
            if (newnode == NULL)
            {
                return -1; // fault in os allocation
            }
            newnode->access_flags = head_vm->access_flags;
            newnode->vm_start = end;
            newnode->vm_end = head_vm->vm_end;
            newnode->vm_next = head_vm->vm_next;
            head_vm->vm_end = end;
            head_vm->access_flags = prot;
            head_vm->vm_next = newnode;
            prev = newnode;
            head_vm = newnode->vm_next;
            continue;
        }
        else if (start >= head_vm->vm_start && head_vm->vm_end >= start && end >= head_vm->vm_end)
        {
            // printvm(head_vm);
            struct vm_area *newnode = (struct vm_area *)os_alloc(sizeof(struct vm_area));
            if (newnode == NULL)
            {
                return -1; // fault in os allocation
            }
            newnode->vm_start = start;
            newnode->access_flags = prot;
            newnode->vm_end = head_vm->vm_end;
            newnode->vm_next = head_vm->vm_next;
            head_vm->vm_end = start;
            head_vm->vm_next = newnode;
            // printvm(newnode);
            // printvm(head_vm);
            prev = newnode;
            head_vm = newnode->vm_next;
            continue;
        }
        prev = head_vm;
        head_vm = head_vm->vm_next;
    }
    // printvma(current->vm_area);
    delete_same_start_end(current);
    merge_vm_areas(current);
    stats->num_vm_area = count_vm_areas(current);

    // pte modifications
    modify_permission(start, end, prot);
    // printk("stats in protect %d\n", stats->num_vm_area);
    return 0;
}

long vm_area_unmap(struct exec_context *current, u64 addr, int length)
{
    if (length < 0)
    {
        return -1;
    }
    if (addr % MY_PAGE_SIZE != 0)
    {
        return -1;
    }
    u64 len_to_be_removed = page_aligned_len(length);
    u64 start = addr;
    u64 end = addr + len_to_be_removed;
    // printk("start %x\tend %x\n", start, end);
    struct vm_area *curr = current->vm_area->vm_next; // first useful node
    struct vm_area *prev = NULL;
    while (curr != NULL)
    {
        // printvm(curr);
        if (curr->vm_start >= start && curr->vm_end <= end)
        {
            // printk("first\n");
            // printvm(curr);
            // printvm(prev);
            // printk("first end \n");
            if (prev == NULL)
            {
                current->vm_area->vm_next = curr->vm_next;
                curr = curr->vm_next;
                continue;
            }
            else
            {
                prev->vm_next = curr->vm_next;
                curr = curr->vm_next;
                continue;
            }
        }
        else if (curr->vm_end <= end && curr->vm_end >= start)
        {
            // printk("second\n");
            curr->vm_end = start;
        }
        else if (curr->vm_start >= start && curr->vm_start <= end)
        {
            // printk("third\n");
            curr->vm_start = end;
        }
        else if (curr->vm_start <= start && curr->vm_end >= end)
        {
            u64 tmpend = curr->vm_end;
            curr->vm_end = start;
            if (tmpend != end)
            {
                struct vm_area *newnode = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                if (newnode == NULL)
                {
                    return -1; // fault in os allocation
                }
                newnode->vm_start = end;
                newnode->access_flags = curr->access_flags;
                newnode->vm_end = tmpend;
                newnode->vm_next = curr->vm_next;
                curr->vm_next = newnode;
            }
            break;
        }
        prev = curr;
        curr = curr->vm_next;
    }
    delete_same_start_end(current);
    merge_vm_areas(current);
    stats->num_vm_area = count_vm_areas(current);
    // printvma(current->vm_area);

    // pte modifications
    my_free_pfns(start, end);
    return 0;
}

/**
 * Function will invoked whenever there is page fault for an address in the vm area region
 * created using mmap
 */

long vm_area_pagefault(struct exec_context *current, u64 addr, int error_code)
{
    struct vm_area *head_vm = current->vm_area->vm_next;
    while (head_vm != NULL)
    {
        if (head_vm->vm_start <= addr && addr < head_vm->vm_end)
        {
            break;
        }
        head_vm = head_vm->vm_next;
    }
    // printvm(head_vm);
    if (head_vm == NULL || (head_vm->access_flags == PROT_READ && ((error_code >> 1) & 1)))
    {
        return -1; // invalid access
    }
    if (error_code == 0x7)
    {
        return handle_cow_fault(current, addr, head_vm->access_flags);
    }
    u64 *pgd_virtual = (u64 *)(osmap(current->pgd));
    u64 number = addr;
    u64 mask9 = (1 << 9) - 1;
    u64 mask12 = (1 << 12) - 1;

    u64 pfn_segment_va = number & mask12;
    number >>= 12;
    u64 pte_segment_va = number & mask9;
    number >>= 9;
    u64 pmd_segment_va = number & mask9;
    number >>= 9;
    u64 pud_segment_va = number & mask9;
    number >>= 9;
    u64 pgd_segment_va = number & mask9;
    number >>= 9;

    // printk("virtual addr segments %d\t%d\t%d\t%d\t%d\n", pgd_segment_va, pud_segment_va, pmd_segment_va, pte_segment_va, pfn_segment_va);
    // printk("error code %x\n", error_code);

    // pgd offset adding
    u64 *pgd_loc = pgd_virtual + pgd_segment_va;
    // printk("at pgd location %x\n", *pgd_loc);
    if ((*pgd_loc) & 1)
    {
        // present bit is 1
        if ((head_vm->access_flags & PROT_WRITE) != 0)
        {
            *(pgd_loc) = (*pgd_loc) | 8;
        }
    }
    else
    {
        u64 pud_allocated = (u64)(osmap(os_pfn_alloc(OS_PT_REG)));
        *(pgd_loc) = ((pud_allocated >> 12) << 12) | ((head_vm->access_flags & PROT_WRITE) ? 9 : 1);
        *(pgd_loc) = (*pgd_loc) | 16;
    }
    // printk("%x %x\n", pgd_loc, *pgd_loc);

    // pud offset adding
    u64 *pud_loc = (u64 *)osmap(((*pgd_loc) >> 12)) + pud_segment_va;
    // printk("at pud location %x\n", *pud_loc);
    if ((*pud_loc) & 1)
    {
        // present bit is 1
        if ((head_vm->access_flags & PROT_WRITE) != 0)
        {
            *(pud_loc) = (*pud_loc) | 8;
        }
    }
    else
    {
        u64 pmd_allocated = (u64)(osmap(os_pfn_alloc(OS_PT_REG)));
        *(pud_loc) = ((pmd_allocated >> 12) << 12) | ((head_vm->access_flags & PROT_WRITE) ? 9 : 1);
        *(pud_loc) = (*pud_loc) | 16;
    }
    // printk("%x %x\n", pud_loc, *pud_loc);

    // pmd offset adding
    u64 *pmd_loc = (u64 *)osmap(((*pud_loc) >> 12)) + pmd_segment_va;
    // printk("at pmd location %x\n", *pmd_loc);
    if ((*pmd_loc) & 1)
    {
        // present bit is 1
        if ((head_vm->access_flags & PROT_WRITE) != 0)
        {
            *(pmd_loc) = (*pmd_loc) | 8;
        }
    }
    else
    {
        // printk("yaha h kya \n");
        u64 pte_allocated = (u64)(osmap(os_pfn_alloc(OS_PT_REG)));
        // printk("pte alloc %x\n", pte_allocated);
        *(pmd_loc) = ((pte_allocated >> 12) << 12) | ((head_vm->access_flags & PROT_WRITE) ? 9 : 1);
        *(pmd_loc) = (*pmd_loc) | 16;
    }
    // printk("%x %x\n", pmd_loc, *pmd_loc);

    // pte offset adding
    u64 *pte_loc = (u64 *)osmap(((*pmd_loc) >> 12)) + pte_segment_va;
    // printk("at pte location %x\n", *pte_loc);
    if ((*pte_loc) & 1)
    {
        // present bit is 1
        if ((head_vm->access_flags & PROT_WRITE) != 0)
        {
            *(pte_loc) = (*pte_loc) | 8;
        }
    }
    else
    {
        u64 pfn_allocated = (u64)osmap(os_pfn_alloc(USER_REG));
        *(pte_loc) = ((pfn_allocated >> 12) << 12) | ((head_vm->access_flags & PROT_WRITE) ? 9 : 1);
        *(pte_loc) = (*pte_loc) | 16;
    }
    // printk("%x %x\n", pte_loc, *pte_loc);
    return 1;
}

void create_page_table_parallel(struct exec_context *parent, struct exec_context *child, u64 vaddr, int permission)
{
    u64 number = vaddr;
    u64 mask9 = (1 << 9) - 1;
    u64 mask12 = (1 << 12) - 1;

    u64 pfn_segment_va = number & mask12;
    number >>= 12;
    u64 pte_segment_va = number & mask9;
    number >>= 9;
    u64 pmd_segment_va = number & mask9;
    number >>= 9;
    u64 pud_segment_va = number & mask9;
    number >>= 9;
    u64 pgd_segment_va = number & mask9;
    number >>= 9;


    u64 *pgd_virtual_parent = (u64 *)(osmap(parent->pgd));
    u64 *pgd_virtual_child = (u64 *)(osmap(child->pgd));

    u64 *pgd_loc_parent = pgd_virtual_parent + pgd_segment_va;
    u64 *pgd_loc_child = pgd_virtual_child + pgd_segment_va;
    // printk("at pgd location %x\n", *pgd_loc);
    if ((*pgd_loc_child) & 1)
    {
        // present bit is 1
        if ((permission & PROT_WRITE) != 0)
        {
            *(pgd_loc_child) = (*pgd_loc_child) | 8;
        }
    }
    else
    {
        if((*pgd_loc_parent) & 1)
        {
            u64 pud_allocated = (u64)(osmap(os_pfn_alloc(OS_PT_REG)));
            *(pgd_loc_child) = ((pud_allocated >> 12) << 12) | ((permission & PROT_WRITE) ? 9 : 1);
            *(pgd_loc_child) = (*pgd_loc_child) | 16;
        }
        else
        {
            return;
        }
    }
    // printk("%x %x\n", pgd_loc, *pgd_loc);

    // pud offset adding
    u64 *pud_loc_parent = (u64 *)osmap(((*pgd_loc_parent) >> 12)) + pud_segment_va;
    u64 *pud_loc_child = (u64 *)osmap(((*pgd_loc_child) >> 12)) + pud_segment_va;
    // printk("at pud location %x\n", *pud_loc);
    if ((*pud_loc_child) & 1)
    {
        // present bit is 1
        if ((permission & PROT_WRITE) != 0)
        {
            *(pud_loc_child) = (*pud_loc_child) | 8;
        }
    }
    else
    {
        if((*pud_loc_parent) & 1)
        {
            u64 pmd_allocated = (u64)(osmap(os_pfn_alloc(OS_PT_REG)));
            *(pud_loc_child) = ((pmd_allocated >> 12) << 12) | ((permission & PROT_WRITE) ? 9 : 1);
            *(pud_loc_child) = (*pud_loc_child) | 16;
        }
        else
        {
            return;
        }
    }
    // printk("%x %x\n", pud_loc, *pud_loc);

    // pmd offset adding
    u64 *pmd_loc_parent = (u64 *)osmap(((*pud_loc_parent) >> 12)) + pmd_segment_va;
    u64 *pmd_loc_child = (u64 *)osmap(((*pud_loc_child) >> 12)) + pmd_segment_va;
    // printk("at pmd location %x\n", *pmd_loc);
    if ((*pmd_loc_child) & 1)
    {
        // present bit is 1
        if ((permission & PROT_WRITE) != 0)
        {
            *(pmd_loc_child) = (*pmd_loc_child) | 8;
        }
    }
    else
    {
        if((*pmd_loc_parent) & 1)
        {
            u64 pte_allocated = (u64)(osmap(os_pfn_alloc(OS_PT_REG)));
            // printk("pte alloc %x\n", pte_allocated);
            *(pmd_loc_child) = ((pte_allocated >> 12) << 12) | ((permission & PROT_WRITE) ? 9 : 1);
            *(pmd_loc_child) = (*pmd_loc_child) | 16;
        }
        else
        {
            return;
        }

    }
    // printk("%x %x\n", pmd_loc, *pmd_loc);

    // pte offset adding
    u64 *pte_loc_parent = (u64 *)osmap(((*pmd_loc_parent) >> 12)) + pte_segment_va;
    u64 *pte_loc_child = (u64 *)osmap(((*pmd_loc_child) >> 12)) + pte_segment_va;
    if((*pte_loc_parent) & 1)
    {
        u64 oneull = 1;
        u64 mask = ~(oneull << 3);
        *(pte_loc_parent) = (*pte_loc_parent) & mask;
        *(pte_loc_child) = (*pte_loc_parent);
        get_pfn((*pte_loc_parent)>>12);
    }
    else
    {
        *(pte_loc_child) = 0x0;
    }
    // printk("at pte location %x\n", *pte_loc);
    // printk("%x %x\n", pte_loc, *pte_loc);

    return; 
}


long do_cfork()
{
    u32 pid;
    struct exec_context *new_ctx = get_new_ctx();
    struct exec_context *ctx = get_current_ctx();
    /* Do not modify above lines
     *
     * */
    /*--------------------- Your code [start]---------------*/
    pid = new_ctx->pid;
    new_ctx->ppid = ctx->pid;
    new_ctx->ticks_to_alarm = ctx->ticks_to_alarm;
    new_ctx->alarm_config_time = ctx->alarm_config_time;
    new_ctx->ticks_to_sleep = ctx->ticks_to_sleep;
    new_ctx->pending_signal_bitmap = ctx->pending_signal_bitmap;
    new_ctx->regs = ctx->regs;
    new_ctx->state = ctx->state;
    new_ctx->used_mem = ctx->used_mem;
    new_ctx->type = ctx->type;

    // printk("new rip %x\n", new_ctx->regs.entry_rip);

    // copying vm area
    struct vm_area *head = NULL;
    struct vm_area *curr = NULL;
    struct vm_area *old_head = ctx->vm_area;
    while (old_head != NULL)
    {
        // printk("%d\n", stats->num_vm_area);
        struct vm_area *newnode = (struct vm_area *)os_alloc(sizeof(struct vm_area));
        newnode->access_flags = old_head->access_flags;
        newnode->vm_end = old_head->vm_end;
        newnode->vm_next = NULL;
        newnode->vm_start = old_head->vm_start;
        if (head == NULL)
        {
            head = newnode;
            curr = newnode;
        }
        else
        {
            curr->vm_next = newnode;
            curr = curr->vm_next;
        }
        old_head = old_head->vm_next;
    }
    new_ctx->vm_area = head;

    // copying name
    for (int i = 0; i < CNAME_MAX; i++)
    {
        new_ctx->name[i] = ctx->name[i];
    }

    // copying stack segments
    for (int i = 0; i < MAX_MM_SEGS; i++)
    {
        new_ctx->mms[i] = ctx->mms[i];
    }

    // copying file_descriptors
    for (int i = 0; i < MAX_OPEN_FILES; i++)
    {
        new_ctx->files[i] = ctx->files[i];
    }

    // copying sighandlers
    for (int i = 0; i < MAX_SIGNALS; i++)
    {
        new_ctx->sighandlers[i] = ctx->sighandlers[i];
    }

    // is this a linked list? nothing about its next or...
    struct ctx_thread_info *ctx_threads;

    /****************************************************************************************/
    /***************************************************************************************/
    /************************ making the user addresses page tables  **********************/
    // allocate PGD
    new_ctx->pgd = os_pfn_alloc(OS_PT_REG);

    // vm area page tables
    struct vm_area *tmp = new_ctx->vm_area;
    u64 start = 0;
    u64 end = 0;
    while (tmp != NULL)
    {
        start = tmp->vm_start;
        end = tmp->vm_end;
        for (; start < end; start += MY_PAGE_SIZE)
        {
            create_page_table_parallel(ctx, new_ctx, start, tmp->access_flags);
        }
        tmp = tmp->vm_next;
    }

    // stack segments page table
    for (int i = 0; i < MAX_MM_SEGS; i++)
    {
        start = new_ctx->mms[i].start;
        if (i == MM_SEG_STACK)
        {
            end = new_ctx->mms[i].end;
        }
        else
        {
            end = new_ctx->mms[i].next_free;
        }
        for (; start < end; start += MY_PAGE_SIZE)
        {
            create_page_table_parallel(ctx, new_ctx, start, new_ctx->mms[i].access_flags);
        }
    }

    // copy the kernel stack
    // new_ctx->os_stack_pfn = os_pfn_alloc(OS_PT_REG);
    // u32 *new_os_stack_virtual = (u32 *)osmap(new_ctx->os_stack_pfn);
    // u32 *old_os_stack_virtual = (u32 *)osmap(ctx->os_stack_pfn);
    // for(int i=0;i<(MY_PAGE_SIZE/(sizeof(u32)));i++)
    // {
    //     if(*(old_os_stack_virtual + i) != 0)
    //     printk("%d \t %x\n", i, *(old_os_stack_virtual + i));
    //     *(new_os_stack_virtual + i) = *(old_os_stack_virtual + i);
    // }

    // flush the TLBs
    flush_tlbs();
    
    /*--------------------- Your code [end] ----------------*/

    /*
     * The remaining part must not be changed
     */
    copy_os_pts(ctx->pgd, new_ctx->pgd);
    do_file_fork(new_ctx);
    setup_child_context(new_ctx);
    return pid;
}


/* Cow fault handling, for the entire user address space
 * For address belonging to memory segments (i.e., stack, data)
 * it is called when there is a CoW violation in these areas.
 *
 * For vm areas, your fault handler 'vm_area_pagefault'
 * should invoke this function
 * */

long handle_cow_fault(struct exec_context *current, u64 vaddr, int access_flags)
{
    u64 *pgd_virtual = (u64 *)(osmap(current->pgd));
    u64 number = vaddr;
    u64 mask9 = (1 << 9) - 1;
    u64 mask12 = (1 << 12) - 1;

    u64 pfn_segment_va = number & mask12;
    number >>= 12;
    u64 pte_segment_va = number & mask9;
    number >>= 9;
    u64 pmd_segment_va = number & mask9;
    number >>= 9;
    u64 pud_segment_va = number & mask9;
    number >>= 9;
    u64 pgd_segment_va = number & mask9;
    number >>= 9;

    // pgd offset adding
    u64 *pgd_loc = pgd_virtual + pgd_segment_va;
    if ((*pgd_loc) & 1)
    {
        // present bit is 1
        if ((access_flags & PROT_WRITE) != 0)
        {
            *(pgd_loc) = (*pgd_loc) | 8;
        }
    }
    else
    {
        u64 pud_allocated = (u64)(osmap(os_pfn_alloc(OS_PT_REG)));
        *(pgd_loc) = ((pud_allocated >> 12) << 12) | ((access_flags & PROT_WRITE) ? 9 : 1);
        *(pgd_loc) = (*pgd_loc) | 16;
    }

    // pud offset adding
    u64 *pud_loc = (u64 *)osmap(((*pgd_loc) >> 12)) + pud_segment_va;
    if ((*pud_loc) & 1)
    {
        // present bit is 1
        if ((access_flags & PROT_WRITE) != 0)
        {
            *(pud_loc) = (*pud_loc) | 8;
        }
    }
    else
    {
        u64 pmd_allocated = (u64)(osmap(os_pfn_alloc(OS_PT_REG)));
        *(pud_loc) = ((pmd_allocated >> 12) << 12) | ((access_flags & PROT_WRITE) ? 9 : 1);
        *(pud_loc) = (*pud_loc) | 16;
    }

    // pmd offset adding
    u64 *pmd_loc = (u64 *)osmap(((*pud_loc) >> 12)) + pmd_segment_va;
    if ((*pmd_loc) & 1)
    {
        // present bit is 1
        if ((access_flags & PROT_WRITE) != 0)
        {
            *(pmd_loc) = (*pmd_loc) | 8;
        }
    }
    else
    {
        u64 pte_allocated = (u64)(osmap(os_pfn_alloc(OS_PT_REG)));
        *(pmd_loc) = ((pte_allocated >> 12) << 12) | ((access_flags & PROT_WRITE) ? 9 : 1);
        *(pmd_loc) = (*pmd_loc) | 16;
    }

    // pte offset adding
    u64 *pte_loc = (u64 *)osmap(((*pmd_loc) >> 12)) + pte_segment_va;
    if ((*pte_loc) & 1)
    {
        u64 shared_pfn = (*pte_loc);
        u32 shared_pfn_physical = (shared_pfn >> 12);
        if (get_pfn_refcount(shared_pfn_physical) >= 2)
        {
            // printk("pid of former %d\n", current->pid);
            put_pfn(shared_pfn_physical);
            u64 pfn_allocated = (u64)osmap(os_pfn_alloc(USER_REG));
            *(pte_loc) = ((pfn_allocated >> 12) << 12) | ((access_flags & PROT_WRITE) ? 9 : 1);
            *(pte_loc) = (*pte_loc) | 16;
            memcpy(osmap(((*pte_loc) >> 12)), osmap(shared_pfn_physical), MY_PAGE_SIZE);
        }
        else if (get_pfn_refcount(shared_pfn_physical) == 1)
        {
            // printk("pid of later %d\n", current->pid);
            if ((access_flags & PROT_WRITE) != 0)
            {
                *(pte_loc) = (*pte_loc) | 8;
            }
        }
    }
    else
    {
        u64 pfn_allocated = (u64)osmap(os_pfn_alloc(USER_REG));
        *(pte_loc) = ((pfn_allocated >> 12) << 12) | ((access_flags & PROT_WRITE) ? 9 : 1);
        *(pte_loc) = (*pte_loc) | 16;
    }

    // flush the TLBs
    flush_tlbs();

    // printk("%x %x\n", pte_loc, *pte_loc);
    return 1;
}