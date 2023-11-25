#include <context.h>
#include <memory.h>
#include <lib.h>
#include <entry.h>
#include <file.h>
#include <tracer.h>

///////////////////////////////////////////////////////////////////////////
//// 		Start of Trace buffer functionality 		      /////
///////////////////////////////////////////////////////////////////////////

int is_valid_mem_range(unsigned long buff, u32 count, int access_bit)
{
	struct exec_context *current_pcb = get_current_ctx();
	struct mm_segment *current_pcb_mms = current_pcb->mms;
	unsigned long start = buff;
	unsigned long end = buff + count;
	int flag = 0;
	if (start >= current_pcb_mms[MM_SEG_CODE].start && end <= current_pcb_mms[MM_SEG_CODE].next_free )
	{
		// printk("segcode\n");
		flag = flag || ((access_bit & 1) != 0); // only read access so access bit should be 1
	}
	if (start >= current_pcb_mms[MM_SEG_RODATA].start && end <= current_pcb_mms[MM_SEG_RODATA].next_free )
	{
		// printk("segrodat\n");
		flag = flag || ((access_bit & 1) != 0); // only read access so access bit should be 1
	}
	if (start >= current_pcb_mms[MM_SEG_DATA].start && end <= current_pcb_mms[MM_SEG_CODE].next_free )
	{
		// printk("segadata\n");
		flag = flag || ((access_bit & 1) != 0) || ((access_bit & 2) != 0); // both read and write
	}
	if (start >= current_pcb_mms[MM_SEG_STACK].start && end <= current_pcb_mms[MM_SEG_STACK].end )
	{
		// printk("segstacki\n");
		flag = flag || ((access_bit & 1) != 0) || ((access_bit & 2) != 0); // both read and write
	}
	struct vm_area *current_pcb_vm = current_pcb->vm_area;
	while (current_pcb_vm != NULL)
	{
		if (start >= current_pcb_vm->vm_start && end <= current_pcb_vm->vm_end)
		{
			// printk("vmar\n");
			// printk("%d   %d\n", current_pcb_vm->access_flags, access_bit);
			// printk("%x   %x\n", current_pcb_vm->vm_start, current_pcb_vm->vm_start);
			flag = flag || ((access_bit & current_pcb_vm->access_flags) != 0);
		}
		current_pcb_vm = current_pcb_vm->vm_next;
	}
	return flag;
}


long trace_buffer_close(struct file *filep)
{
	if(filep == NULL)
	{
		return -EINVAL;
	}
	if(filep->fops == NULL)
	{
		return -EINVAL;
	}
	os_free(filep->fops, sizeof(filep->fops));
	// os_page_free(USER_REG, filep->fops);
	if(filep->trace_buffer->buffer_space == NULL)
	{
		return -EINVAL;
	}
	os_page_free(USER_REG, filep->trace_buffer->buffer_space);
	if(filep->trace_buffer == NULL)
	{
		return -EINVAL;
	}
	os_free(filep->trace_buffer, sizeof(filep->trace_buffer));
	// os_page_free(USER_REG, filep->trace_buffer);
	struct exec_context *current_pcb = get_current_ctx();
	for (int i = 0; i < MAX_OPEN_FILES; i++)
	{
		if (current_pcb->files[i] == filep)
		{
			// printk("closing\n");
			current_pcb->files[i] = NULL;
			break;
		}
	}
	// os_page_free(USER_REG, filep);
	
	os_free(filep, sizeof(filep));
	return 0;
}

int trace_buffer_read(struct file *filep, char *buff, u32 count)
{
	if (!is_valid_mem_range((unsigned long)(buff), count, 2))
	{
		return -EBADMEM;
	}
	if(filep == NULL || count < 0)
	{
		return -EINVAL;
	}
	if (filep->mode == O_WRITE)
	{
		return -EINVAL;
	}
	if (filep->trace_buffer->is_buffer_full == 0 && filep->trace_buffer->read_offset == filep->trace_buffer->write_offset)
	{
		return 0;
	}
	struct trace_buffer_info *tmp_trace_buffer = filep->trace_buffer;
	if(tmp_trace_buffer == NULL)
	{
		return -EINVAL;
	}
	int num_bytes_read = count;
	int tmp_read_offset = tmp_trace_buffer->read_offset;
	int tmp_write_offset = tmp_trace_buffer->write_offset;
	int diff = tmp_write_offset - tmp_read_offset;
	if (diff <= 0)
	{
		diff += TRACE_BUFFER_MAX_SIZE;
	}
	if (count > diff)
	{
		num_bytes_read = diff;
	}
	for (int i = 0; i < num_bytes_read; i++)
	{
		buff[i] = tmp_trace_buffer->buffer_space[(tmp_read_offset + i) % TRACE_BUFFER_MAX_SIZE];
		// printk("os read %d\n", buff[i]);
	}
	filep->trace_buffer->read_offset = (tmp_read_offset + num_bytes_read) % TRACE_BUFFER_MAX_SIZE;
	filep->offp = filep->trace_buffer->read_offset;
	if (num_bytes_read != 0 && filep->trace_buffer->is_buffer_full == 1)
	{
		filep->trace_buffer->is_buffer_full = 0;
	}
	return num_bytes_read;
}

int trace_buffer_write(struct file *filep, char *buff, u32 count)
{
	if (!is_valid_mem_range((unsigned long)(buff), count, 1))
	{
		// printk("invalidmem\n");
		return -EBADMEM;
	}
	if(filep == NULL || count < 0)
	{
		return -EINVAL;
	}
	if (filep->mode == O_READ)
	{
		// printk("invalidmode\n");
		return -EINVAL;
	}
	if (filep->trace_buffer->is_buffer_full == 1)
	{
		// printk("full buff\n");
		return 0;
	}
	struct trace_buffer_info *tmp_trace_buffer = filep->trace_buffer;
	if(tmp_trace_buffer == NULL)
	{
		return -EINVAL;
	}
	int num_bytes_write = count;
	int tmp_read_offset = tmp_trace_buffer->read_offset;
	int tmp_write_offset = tmp_trace_buffer->write_offset;
	int diff = tmp_read_offset - tmp_write_offset;
	if (diff <= 0)
	{
		diff += TRACE_BUFFER_MAX_SIZE;
	}
	if (count > diff)
	{
		num_bytes_write = diff;
	}
	for (int i = 0; i < num_bytes_write; i++)
	{
		filep->trace_buffer->buffer_space[(tmp_write_offset + i) % TRACE_BUFFER_MAX_SIZE] = buff[i];
		// printk("%d\n", buff[i]);
	}
	filep->trace_buffer->write_offset = (tmp_write_offset + num_bytes_write) % TRACE_BUFFER_MAX_SIZE;
	filep->offp = filep->trace_buffer->write_offset;
	if (!(tmp_trace_buffer->is_buffer_full) && filep->trace_buffer->write_offset == filep->trace_buffer->read_offset && num_bytes_write > 0)
	{
		tmp_trace_buffer->is_buffer_full = 1;
	}
	return num_bytes_write;
}

int sys_create_trace_buffer(struct exec_context *current, int mode)
{
	if(mode != O_READ && mode != O_WRITE && mode != O_RDWR)
	{
		return -EINVAL;
	}
	if(current == NULL)
	{
		return -EINVAL;
	}
	struct exec_context *current_pcb = current;
	struct file *free_file_fd = NULL;
	int free_descriptor = -1;

	// find the free descriptor
	for (int i = 0; i < MAX_OPEN_FILES; i++)
	{
		if (current_pcb->files[i] == NULL)
		{
			free_descriptor = i;
			break;
		}
	}

	// if no free fd found
	if (free_descriptor == -1)
	{
		return -EINVAL;
	}

	// allocate trace buffer functions
	struct fileops *trace_buffer_operations = (struct fileops *)os_alloc(sizeof(struct fileops));
	// struct fileops *trace_buffer_operations = (struct fileops *)os_page_alloc(USER_REG);
	if (trace_buffer_operations == NULL)
	{
		return -ENOMEM;
	}
	trace_buffer_operations->read = trace_buffer_read;
	trace_buffer_operations->write = trace_buffer_write;
	trace_buffer_operations->close = trace_buffer_close;

	// create file object and initialise fields
	// struct file *file_object = (struct file *)os_page_alloc(USER_REG);
	struct file *file_object = (struct file *)os_alloc(sizeof(struct file));
	if (file_object == NULL)
	{
		return -ENOMEM;
	}
	file_object->type = TRACE_BUFFER;
	file_object->mode = mode;
	file_object->offp = 0;
	file_object->ref_count = 0;
	file_object->inode = NULL;
	file_object->fops = trace_buffer_operations;

	// creating trace buffer object
	struct trace_buffer_info *my_trace_buffer = (struct trace_buffer_info *)os_alloc(sizeof(struct trace_buffer_info));
	// struct trace_buffer_info *my_trace_buffer = (struct trace_buffer_info *)os_page_alloc(USER_REG);
	if (my_trace_buffer == NULL)
	{
		return -ENOMEM;
	}
	my_trace_buffer->trace_buffer_mode = mode;
	my_trace_buffer->read_offset = 0;
	my_trace_buffer->write_offset = 0;
	my_trace_buffer->is_buffer_full = 0;
	my_trace_buffer->buffer_space = (char *)os_page_alloc(USER_REG);
	if (my_trace_buffer->buffer_space == NULL)
	{
		return -ENOMEM;
	}

	// update the trace buffer pointer in the file object
	file_object->trace_buffer = my_trace_buffer;

	// changing the file object in the fd array
	current->files[free_descriptor] = file_object;
	return free_descriptor;
}

///////////////////////////////////////////////////////////////////////////
//// 		Start of strace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////

void helperToConvertLongToChar(char *byteArray, unsigned long num)
{
	byteArray[0] = (num >> 56) & 0xFF;
	byteArray[1] = (num >> 48) & 0xFF;
	byteArray[2] = (num >> 40) & 0xFF;
	byteArray[3] = (num >> 32) & 0xFF;
	byteArray[4] = (num >> 24) & 0xFF;
	byteArray[5] = (num >> 16) & 0xFF;
	byteArray[6] = (num >> 8) & 0xFF;
	byteArray[7] = num & 0xFF;
	return;
}

int helperToWriteToBuffer(struct file *filep, char *buff, int count)
{
	if(filep == NULL || count < 0)
	{
		return -EINVAL;
	}
	if (filep->mode == O_READ)
	{
		// printk("invalidmode\n");
		return -EINVAL;
	}
	if (filep->trace_buffer->is_buffer_full == 1)
	{
		// printk("full buff\n");
		return 0;
	}
	struct trace_buffer_info *tmp_trace_buffer = filep->trace_buffer;
	if(tmp_trace_buffer == NULL)
	{
		return -EINVAL;
	}
	int num_bytes_write = count;
	int tmp_read_offset = tmp_trace_buffer->read_offset;
	int tmp_write_offset = tmp_trace_buffer->write_offset;
	int diff = tmp_read_offset - tmp_write_offset;
	if (diff <= 0)
	{
		diff += TRACE_BUFFER_MAX_SIZE;
	}
	if (count > diff)
	{
		num_bytes_write = diff;
	}
	// printk("writing in os buffer\n");
	for (int i = 0; i < num_bytes_write; i++)
	{
		filep->trace_buffer->buffer_space[(tmp_write_offset + i) % TRACE_BUFFER_MAX_SIZE] = buff[i];
		// printk("os mode %d\n", filep->trace_buffer->buffer_space[(tmp_write_offset + i)%TRACE_BUFFER_MAX_SIZE]);
	}

	filep->trace_buffer->write_offset = (tmp_write_offset + num_bytes_write) % TRACE_BUFFER_MAX_SIZE;
	if (!(tmp_trace_buffer->is_buffer_full) && filep->trace_buffer->write_offset == filep->trace_buffer->read_offset && num_bytes_write > 0)
	{
		tmp_trace_buffer->is_buffer_full = 1;
	}
	// printk("write ho rha h \n");
	return num_bytes_write;
}

int helperToReadFromBuffer(struct file *filep, char *buff, int count)
{
	if (!is_valid_mem_range((unsigned long)(buff), count, 2))
	{
		return -EBADMEM;
	}
	if(filep == NULL || count < 0)
	{
		return -EINVAL;
	}
	if (filep->mode == O_WRITE)
	{
		return -EINVAL;
	}
	if (filep->trace_buffer->is_buffer_full == 0 && filep->trace_buffer->read_offset == filep->trace_buffer->write_offset)
	{
		return 98;
	}
	struct trace_buffer_info *tmp_trace_buffer = filep->trace_buffer;
	if(tmp_trace_buffer == NULL)
	{
		return -EINVAL;
	}
	int num_bytes_read = count;
	int tmp_read_offset = tmp_trace_buffer->read_offset;
	int tmp_write_offset = tmp_trace_buffer->write_offset;
	int diff = tmp_write_offset - tmp_read_offset;
	if (diff <= 0)
	{
		diff += TRACE_BUFFER_MAX_SIZE;
	}
	if (count > diff)
	{
		num_bytes_read = diff;
	}
	for (int i = 0; i < num_bytes_read; i++)
	{
		buff[i] = tmp_trace_buffer->buffer_space[(tmp_read_offset + i) % TRACE_BUFFER_MAX_SIZE];
		// printk("os read %d\n", buff[i]);
	}
	filep->trace_buffer->read_offset = (tmp_read_offset + num_bytes_read) % TRACE_BUFFER_MAX_SIZE;
	if (num_bytes_read != 0 && filep->trace_buffer->is_buffer_full == 1)
	{
		filep->trace_buffer->is_buffer_full = 0;
	}
	return num_bytes_read;
}

int getNumArgs(u64 syscall_num)
{
	switch (syscall_num)
	{
	case SYSCALL_EXIT:
		return 0;
	case SYSCALL_GETPID:
		return 0;
	case SYSCALL_EXPAND:
		return 2;
	case SYSCALL_SHRINK:
		return 0;
	case SYSCALL_ALARM:
		return 0;
	case SYSCALL_SLEEP:
		return 1;
	case SYSCALL_SIGNAL:
		return 2;
	case SYSCALL_CLONE:
		return 2;
	case SYSCALL_FORK:
		return 0;
	case SYSCALL_STATS:
		return 0;
	case SYSCALL_CONFIGURE:
		return 1;
	case SYSCALL_PHYS_INFO:
		return 0;
	case SYSCALL_DUMP_PTT:
		return 1;
	case SYSCALL_CFORK:
		return 0;
	case SYSCALL_MMAP:
		return 4;
	case SYSCALL_MUNMAP:
		return 2;
	case SYSCALL_MPROTECT:
		return 3;
	case SYSCALL_PMAP:
		return 1;
	case SYSCALL_VFORK:
		return 0;
	case SYSCALL_GET_USER_P:
		return 0;
	case SYSCALL_GET_COW_F:
		return 0;
	case SYSCALL_OPEN:
		return 2;
	case SYSCALL_READ:
		return 3;
	case SYSCALL_WRITE:
		return 3;
	case SYSCALL_DUP:
		return 1;
	case SYSCALL_DUP2:
		return 2;
	case SYSCALL_CLOSE:
		return 1;
	case SYSCALL_LSEEK:
		return 3;
	case SYSCALL_FTRACE:
		return 4;
	case SYSCALL_TRACE_BUFFER:
		return 1;
	case SYSCALL_START_STRACE:
		return 2;
	case SYSCALL_END_STRACE:
		return 0;
	case SYSCALL_READ_STRACE:
		return 3;
	case SYSCALL_STRACE:
		return 2;
	case SYSCALL_READ_FTRACE:
		return 3;
	case SYSCALL_GETPPID:
		return 0;
	default:
		return 0;
	}
}

int perform_tracing(u64 syscall_num, u64 param1, u64 param2, u64 param3, u64 param4)
{
	if (syscall_num == 1 || syscall_num == 38 || syscall_num == 37)
	{
		return 0;
	}
	struct exec_context *current_pcb = get_current_ctx();
	if (current_pcb == NULL)
	{
		return 0;
	}
	if (current_pcb->st_md_base->is_traced == 0)
	{
		return 0;
	}
	if (current_pcb->st_md_base == NULL)
	{
		return 0;
	}
	// printk("a gya \n");
	// struct strace_info *tmphead = current_pcb->st_md_base->next;
	// while (tmphead != NULL)
	// {
	// 	printk("%d ", tmphead->syscall_num);
	// 	if (tmphead->syscall_num == syscall_num)
	// 	{
	// 		// printk("sycall mili %d\n", syscall_num);
	// 	}
	// 	tmphead = tmphead->next;
	// }
	// printk("\n");
	int flag = (current_pcb->st_md_base->tracing_mode == FULL_TRACING);
	if (current_pcb->st_md_base->tracing_mode == FILTERED_TRACING)
	{
		struct strace_info *tmphead = current_pcb->st_md_base->next;
		while (tmphead != NULL)
		{
			if (tmphead->syscall_num == syscall_num)
			{
				// printk("sycall mili %d\n", syscall_num);
				flag = 1;
				break;
			}
			tmphead = tmphead->next;
		}
	}
	if (!flag)
	{
		return 0;
	}
	struct strace_head *headstrace = current_pcb->st_md_base;
	int fdstrace = headstrace->strace_fd;
	int numargs = getNumArgs(syscall_num);
	// printk("perform kr %d %d\n", syscall_num, numargs);
	if (numargs == 0)
	{
		// u64 *towrite = (u64 *)os_alloc(1 * sizeof(u64));
		u64 towrite[1];
		towrite[0] = syscall_num;
		int x = helperToWriteToBuffer(current_pcb->files[fdstrace], (char *)(towrite), 8);
		if (x != 8)
		{
			return -EINVAL;
		}
		// os_free(towrite, sizeof(towrite));
	}
	else if (numargs == 1)
	{
		// u64 *towrite = (u64 *)os_alloc(2 * sizeof(u64));
		u64 towrite[2];
		towrite[0] = syscall_num;
		towrite[1] = param1;
		int x = helperToWriteToBuffer(current_pcb->files[fdstrace], (char *)(towrite), 16);
		if (x != 16)
		{
			return -EINVAL;
		}
	}
	else if (numargs == 2)
	{
		// u64 *towrite = (u64 *)os_alloc(3 * sizeof(u64));
		u64 towrite[3];
		towrite[0] = syscall_num;
		towrite[1] = param1;
		towrite[2] = param2;
		int x = helperToWriteToBuffer(current_pcb->files[fdstrace], (char *)(towrite), 24);
		if (x != 24)
		{
			return -EINVAL;
		}
		// printk("ho gya write\n");
	}
	else if (numargs == 3)
	{
		u64 towrite[4];
		// u64 *towrite = (u64 *)os_alloc(4 * sizeof(u64));
		towrite[0] = syscall_num;
		towrite[1] = param1;
		towrite[2] = param2;
		towrite[3] = param3;
		int x = helperToWriteToBuffer(current_pcb->files[fdstrace], (char *)(towrite), 32);
		if (x != 32)
		{
			// printk("Hona cahiye %d\n", x);
			return -EINVAL;
		}
		// os_free(towrite, sizeof(towrite));
	}
	else if (numargs == 4)
	{
		u64 towrite[5];
		// u64 *towrite = (u64 *)os_alloc(5 * sizeof(u64));
		towrite[0] = syscall_num;
		towrite[1] = param1;
		towrite[2] = param2;
		towrite[3] = param3;
		towrite[4] = param4;
		int x = helperToWriteToBuffer(current_pcb->files[fdstrace], (char *)(towrite), 40);
		if (x != 40)
		{
			return -EINVAL;
		}
		// printk("write ho gya\n");
	}
	return 0;
}

int sys_strace(struct exec_context *current, int syscall_num, int action)
{
	if (action == ADD_STRACE)
	{
		if(current == NULL)
		{
			return -EINVAL;
		}
		// check if the st_md_base is NULL, so we have to allocate it first but the tracing will be off
		if (current->st_md_base == NULL)
		{
			struct strace_head *strace_struct = (struct strace_head *)os_alloc(sizeof(struct strace_head));
			// struct strace_head *strace_struct = (struct strace_head *)os_page_alloc(USER_REG);
			if (strace_struct == NULL)
			{
				return -EINVAL;
			}
			strace_struct->count = 0;
			strace_struct->is_traced = 0;
			strace_struct->strace_fd = -1;
			strace_struct->tracing_mode = 0;
			strace_struct->next = NULL;
			strace_struct->last = NULL;
			current->st_md_base = strace_struct;
		}

		// check if already added 
		struct strace_info *tmp = current->st_md_base->next;
		while(tmp != NULL)
		{
			if(tmp->syscall_num == syscall_num)
			{
				return -EINVAL;
			}
			tmp = tmp->next;
		}

		if (current->st_md_base->count == STRACE_MAX)
		{
			// printk("%d\n", syscall_num);
			return -EINVAL;
		}

		// making new node for the syscall
		struct strace_info *newnode = (struct strace_info *)os_alloc(sizeof(struct strace_info));
		newnode->syscall_num = syscall_num;
		newnode->next = NULL;
		if (current->st_md_base->last == NULL && current->st_md_base->next == NULL)
		{
			// list empty
			// printk("nya bnara%d\n", syscall_num);
			current->st_md_base->last = newnode;
			current->st_md_base->next = newnode;
			current->st_md_base->count += 1;
		}
		else
		{
			current->st_md_base->last->next = newnode;
			current->st_md_base->last = newnode;
			current->st_md_base->count += 1;
		}
		// struct strace_info *tmp = current->st_md_base->next;
		// while(tmp)
		// {
		// 	printk("list me %d\n", tmp->syscall_num);
		// 	tmp = tmp->next;
		// }
	}
	else if (action == REMOVE_STRACE)
	{
		if (current == NULL || current->st_md_base == NULL)
		{
			return -EINVAL;
		}

		struct strace_head *basestrace = current->st_md_base;
		struct strace_info *current_node = basestrace->next;
		struct strace_info *prev_node = NULL;

		// Traverse the list to find the node with syscall_num matching x
		while (current_node != NULL && current_node->syscall_num != syscall_num)
		{
			prev_node = current_node;
			current_node = current_node->next;
		}

		if (current_node == NULL)
		{
			return -EINVAL;
		}

		current->st_md_base->count -= 1;
		if (prev_node != NULL)
		{
			prev_node->next = current_node->next;
		}

		// Update the first and last pointers of the list if necessary
		if (current_node == current->st_md_base->next)
		{
			current->st_md_base->next = current_node->next;
		}
		if (current_node == current->st_md_base->last)
		{
			current->st_md_base->last = prev_node;
		}
		os_free(current_node, sizeof(current_node));
	}
	return 0;
}

int sys_read_strace(struct file *filep, char *buff, u64 count)
{
	if(filep == NULL || count < 0)
	{
		return -EINVAL;
	}
	int total_bytes_read = 0;
	int numargs = 0;
	for (int i = 0; i < count; i++)
	{
		int x = helperToReadFromBuffer(filep, (char *)(buff + total_bytes_read), 8);
		if (x == 98)
		{
			break;
		}
		if (x != 8)
		{
			// printk("first error\n");
			return -EINVAL;
		}
		u64 syscallNo = ((u64 *)buff)[total_bytes_read / 8];
		total_bytes_read += 8;
		int numargs = getNumArgs(syscallNo);
		// printk("read %d %d\n", syscallNo, numargs);
		for (int j = 0; j < numargs; j++)
		{
			int x = helperToReadFromBuffer(filep, (char *)(buff + total_bytes_read), 8);
			if (x != 8)
			{
				// printk("second error\n");
				return -EINVAL;
			}
			total_bytes_read += 8;
		}
	}
	return total_bytes_read;
}

int sys_start_strace(struct exec_context *current, int fd, int tracing_mode)
{
	if(current == NULL || fd < 0 || (tracing_mode != FULL_TRACING && tracing_mode != FILTERED_TRACING))
	{
		return -EINVAL;
	}
	if (current->st_md_base != NULL)
	{
		current->st_md_base->strace_fd = fd;
		current->st_md_base->tracing_mode = tracing_mode;
		current->st_md_base->is_traced = 1;
		return 0;
	}
	struct strace_head *strace_struct = (struct strace_head *)os_alloc(sizeof(struct strace_head));
	// struct strace_head *strace_struct = (struct strace_head *)os_page_alloc(USER_REG);
	if (strace_struct == NULL)
	{
		return -EINVAL;
	}
	strace_struct->count = 0;
	strace_struct->is_traced = 1;
	strace_struct->strace_fd = fd;
	strace_struct->tracing_mode = tracing_mode;
	strace_struct->next = NULL;
	strace_struct->last = NULL;
	current->st_md_base = strace_struct;
	return 0;
}

int sys_end_strace(struct exec_context *current)
{
	if(current == NULL)
	{
		return -EINVAL;
	}
	if(current->st_md_base == NULL)
	{
		return -EINVAL;
	}
	struct strace_info *tmp = current->st_md_base->next;
	while (tmp != NULL)
	{
		struct strace_info *ptr = tmp;
		tmp = tmp->next;
		os_free(ptr, sizeof(ptr));
	}
	current->st_md_base->is_traced = 0;
	current->st_md_base->count = 0;
	current->st_md_base->next = NULL;
	current->st_md_base->last = NULL;
	struct strace_head *temp = current->st_md_base;
	os_free(temp, sizeof(temp));
	current->st_md_base = NULL;
	return 0;
}

///////////////////////////////////////////////////////////////////////////
//// 		Start of ftrace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////

long do_ftrace(struct exec_context *current, unsigned long faddr, long action, long nargs, int fd_trace_buffer)
{
	if (action == ADD_FTRACE)
	{
		// check if the ft_md_base is NULL, so we have to allocate it first
		if (current->ft_md_base == NULL)
		{
			struct ftrace_head *ftrace_struct = (struct ftrace_head *)os_alloc(sizeof(struct ftrace_head));
			// struct ftrace_head *ftrace_struct = (struct ftrace_head *)os_page_alloc(USER_REG);
			if (ftrace_struct == NULL)
			{
				return -EINVAL;
			}
			ftrace_struct->count = 0;
			ftrace_struct->next = NULL;
			ftrace_struct->last = NULL;
			current->ft_md_base = ftrace_struct;
		}
		
		// check if already present
		struct ftrace_info *tmp = current->ft_md_base->next;
		while (tmp != NULL)
		{
			if (tmp->faddr == faddr)
			{
				return -EINVAL;
			}
			tmp = tmp->next;
		}
		
		if (current->ft_md_base->count == FTRACE_MAX)
		{
			return -EINVAL;
		}

		// making new node for the function
		struct ftrace_info *func_struct = (struct ftrace_info *)os_alloc(sizeof(struct ftrace_info));
		// struct ftrace_info *func_struct = (struct ftrace_info *)os_page_alloc(USER_REG);
		func_struct->capture_backtrace = 0;
		func_struct->faddr = faddr;
		func_struct->fd = fd_trace_buffer;
		func_struct->next = NULL;
		func_struct->num_args = nargs;

		if (current->ft_md_base->last == NULL && current->ft_md_base->next == NULL)
		{
			// list empty
			current->ft_md_base->last = func_struct;
			current->ft_md_base->next = func_struct;
			current->ft_md_base->count += 1;
		}
		else
		{
			current->ft_md_base->last->next = func_struct;
			current->ft_md_base->last = func_struct;
			current->ft_md_base->count += 1;
		}
	}
	else if (action == REMOVE_FTRACE)
	{
		if (current == NULL || current->ft_md_base == NULL)
		{
			return -EINVAL;
		}

		struct ftrace_head *baseftrace = current->ft_md_base;
		struct ftrace_info *current_node = baseftrace->next;
		struct ftrace_info *prev_node = NULL;

		// Traverse the list to find the node with syscall_num matching x
		while (current_node != NULL && current_node->faddr != faddr)
		{
			prev_node = current_node;
			current_node = current_node->next;
		}

		if (current_node == NULL)
		{
			return -EINVAL;
		}
		if (*((u8 *)(current_node->faddr)) == INV_OPCODE)
		{
			u8 *ptr = (u8 *)(current_node->faddr);
			for (int i = 0; i < 4; i++)
			{
				*(ptr + i) = current_node->code_backup[i];
			}
		}
		current->ft_md_base->count -= 1;
		if (prev_node != NULL)
		{
			prev_node->next = current_node->next;
		}

		// Update the first and last pointers of the list if necessary
		if (current_node == current->ft_md_base->next)
		{
			current->ft_md_base->next = current_node->next;
		}
		if (current_node == current->ft_md_base->last)
		{
			current->ft_md_base->last = prev_node;
		}
		os_free(current_node, sizeof(current_node));
	}
	else if (action == ENABLE_BACKTRACE)
	{
		if (current == NULL || current->ft_md_base == NULL)
		{
			return -EINVAL;
		}

		struct ftrace_head *baseftrace = current->ft_md_base;
		struct ftrace_info *current_node = baseftrace->next;
		struct ftrace_info *prev_node = NULL;

		// Traverse the list to find the node with func_addr matching x
		while (current_node != NULL && current_node->faddr != faddr)
		{
			prev_node = current_node;
			current_node = current_node->next;
		}
		if (current_node == NULL)
		{
			return -EINVAL;
		}
		u8 *ptr = (u8 *)faddr;
		if(*ptr != INV_OPCODE)
		{
			for (int i = 0; i < 4; i++)
			{
				current_node->code_backup[i] = *(ptr + i);
				*(ptr + i) = INV_OPCODE;
			}
		}
		current_node->capture_backtrace = 1;
	}
	else if (action == DISABLE_BACKTRACE)
	{
		if (current == NULL || current->ft_md_base == NULL)
		{
			return -EINVAL;
		}

		struct ftrace_head *baseftrace = current->ft_md_base;
		struct ftrace_info *current_node = baseftrace->next;
		struct ftrace_info *prev_node = NULL;

		// Traverse the list to find the node with func_addr matching x
		while (current_node != NULL && current_node->faddr != faddr)
		{
			prev_node = current_node;
			current_node = current_node->next;
		}
		if (current_node == NULL)
		{
			return -EINVAL;
		}
		if (*((u8 *)(current_node->faddr)) == INV_OPCODE)
		{
			u8 *ptr = (u8 *)(current_node->faddr);
			for (int i = 0; i < 4; i++)
			{
				*(ptr + i) = current_node->code_backup[i];
			}
		}
		current_node->capture_backtrace = 0;
	}
	else if (action == ENABLE_FTRACE)
	{
		if (current == NULL || current->ft_md_base == NULL)
		{
			return -EINVAL;
		}

		struct ftrace_head *baseftrace = current->ft_md_base;
		struct ftrace_info *current_node = baseftrace->next;
		struct ftrace_info *prev_node = NULL;

		// Traverse the list to find the node with func_addr matching x
		while (current_node != NULL && current_node->faddr != faddr)
		{
			prev_node = current_node;
			current_node = current_node->next;
		}
		if (current_node == NULL)
		{
			return -EINVAL; // func list me hi nhi h
		}

		u8 *ptr = (u8 *)faddr;
		if(*ptr != INV_OPCODE)
		{
			for (int i = 0; i < 4; i++)
			{
				current_node->code_backup[i] = *(ptr + i);
				*(ptr + i) = INV_OPCODE;
			}
		}
	}
	else if (action == DISABLE_FTRACE)
	{
		if (current == NULL || current->ft_md_base == NULL)
		{
			return -EINVAL;
		}

		struct ftrace_head *baseftrace = current->ft_md_base;
		struct ftrace_info *current_node = baseftrace->next;
		struct ftrace_info *prev_node = NULL;

		// Traverse the list to find the node with func_addr matching x
		while (current_node != NULL && current_node->faddr != faddr)
		{
			prev_node = current_node;
			current_node = current_node->next;
		}
		if (current_node == NULL)
		{
			return -EINVAL; // func list me nhi
		}
		if (*((u8 *)(current_node->faddr)) == INV_OPCODE)
		{
			u8 *ptr = (u8 *)(current_node->faddr);
			for (int i = 0; i < 4; i++)
			{
				*(ptr + i) = current_node->code_backup[i];
			}
		}
	}
	return 0;
}

// Fault handler
long handle_ftrace_fault(struct user_regs *regs)
{
	// printk("fault \n");
	regs->entry_rsp -= 8;
	*(((u64 *)regs->entry_rsp)) = regs->rbp;
	regs->rbp = regs->entry_rsp;
	struct exec_context *current_pcb = get_current_ctx();
	if(current_pcb == NULL)
	{
		return -EINVAL;
	}
	if(current_pcb->ft_md_base == NULL)
	{
		return -EINVAL;
	}
	struct ftrace_info *function_list = current_pcb->ft_md_base->next;
	while (function_list != NULL)
	{
		if (function_list->faddr == regs->entry_rip)
		{
			// printk("mil gya\n");
			break;
		}
		function_list = function_list->next;
	}
	if (function_list == NULL)
	{
		return -EINVAL;
	}
	u64 cnt = function_list->num_args + 1 + (function_list->capture_backtrace == 1);
	if (function_list->capture_backtrace == 1)
	{
		u64 *ptr = (u64 *)(regs->rbp);
		while (*(ptr + 1) != END_ADDR)
		{
			cnt++;
			ptr = (u64 *)(*(ptr));
		}
	}
	u64 tmparray[1] = {cnt};
	int x = helperToWriteToBuffer(current_pcb->files[function_list->fd], (char *)(tmparray), sizeof(u64));
	if (x != 8)
	{
		// printk("write nhi hua %d\n", x);
		return -EINVAL;
	}
	int numargs = function_list->num_args;
	u64 *towrite = (u64 *)os_alloc((numargs + 1) * sizeof(u64));
	towrite[0] = function_list->faddr;
	// printk("func addr written %d\n", towrite[0]);
	// rdi rsi rdx rcx r8 r9 in order
	if (numargs >= 1)
	{
		towrite[1] = regs->rdi;
	}
	if (numargs >= 2)
	{
		towrite[2] = regs->rsi;
	}
	if (numargs >= 3)
	{
		towrite[3] = regs->rdx;
	}
	if (numargs >= 4)
	{
		towrite[4] = regs->rcx;
	}
	if (numargs >= 5)
	{
		towrite[5] = regs->r8;
	}
	if (numargs >= 6)
	{
		towrite[6] = regs->r9;
	}
	x = helperToWriteToBuffer(current_pcb->files[function_list->fd], (char *)(towrite), (numargs + 1) * sizeof(u64));
	if (x != (numargs + 1) * 8)
	{
		// printk("write nhi hua %d\n", x);
		return -EINVAL;
	}
	if (function_list->capture_backtrace == 1)
	{
		towrite[0] = function_list->faddr;
		int x = helperToWriteToBuffer(current_pcb->files[function_list->fd], (char *)(towrite), sizeof(u64));
		if (x != 8)
		{
			// printk("write nhi hua %d\n", x);
			return -EINVAL;
		}
		u64 *ptr = (u64 *)(regs->rbp);
		while (*(ptr + 1) != END_ADDR)
		{
			towrite[0] = *(ptr + 1);
			int x = helperToWriteToBuffer(current_pcb->files[function_list->fd], (char *)(towrite), sizeof(u64));
			if (x != 8)
			{
				// printk("write nhi hua %d\n", x);
				return -EINVAL;
			}
			ptr = (u64 *)(*(ptr));
		}
	}
	os_free(towrite, sizeof(towrite));
	regs->entry_rip = function_list->faddr + 4;
	return 0;
}


int sys_read_ftrace(struct file *filep, char *buff, u64 count)
{
	if(filep == NULL || count < 0)
	{
		return -EINVAL;
	}
	int total_bytes_read = 0;
	for (int i = 0; i < count; i++)
	{
		int x = helperToReadFromBuffer(filep, (char *)(buff + total_bytes_read), 8);
		if (x == 98)
		{
			break;
		}
		if (x != 8)
		{
			// printk("first error\n");
			return -EINVAL;
		}
		u64 numtoread = ((u64 *)buff)[total_bytes_read / 8];
		x = helperToReadFromBuffer(filep, (char *)(buff + total_bytes_read), numtoread*8);
		// printk("x = %d\n", x);
		if (x != numtoread*8)
		{
			// printk("second error\n");
			return -EINVAL;
		}
		total_bytes_read += (numtoread*8);
	}
	return total_bytes_read;
}