#include<context.h>
#include<memory.h>
#include<lib.h>
#include<entry.h>
#include<file.h>
#include<tracer.h>


///////////////////////////////////////////////////////////////////////////
//// 		Start of Trace buffer functionality 		      /////
///////////////////////////////////////////////////////////////////////////

int is_valid_mem_range(unsigned long buff, u32 count, int access_bit) 
{
	struct exec_context *current = get_current_ctx();
	int flag = 0;
	if(buff >= current->mms[MM_SEG_CODE].start && buff+count < current->mms[MM_SEG_CODE].next_free && access_bit == 0) flag = 1;
	if(buff >= current->mms[MM_SEG_RODATA].start && buff+count < current->mms[MM_SEG_RODATA].next_free && access_bit == 0) flag = 1;
	if(buff >= current->mms[MM_SEG_DATA].start && buff+count < current->mms[MM_SEG_DATA].next_free &&(access_bit == 0 || access_bit == 1)) flag = 1;
	if(buff >= current->mms[MM_SEG_STACK].start && buff+count < current->mms[MM_SEG_STACK].end && (access_bit == 0 || access_bit == 1)) flag = 1;
	
	struct vm_area *curr = current->vm_area;
	while(curr){
		if(buff >= curr->vm_start && buff+count < curr->vm_end && ((curr->access_flags >> access_bit)&1)) flag = 1;
		curr = curr->vm_next;
	}

	return flag;
}


long trace_buffer_close(struct file *filep)
{
	if(filep){
		if(filep->trace_buffer){
			if(filep->trace_buffer->memory) os_page_free(USER_REG, filep->trace_buffer->memory);
			else return -EINVAL;
			os_free(filep->trace_buffer, sizeof(struct trace_buffer_info));
		}
		else return -EINVAL;
		if(filep->fops) os_free(filep->fops, sizeof(struct fileops));
		else return -EINVAL;
		os_free(filep, sizeof(struct file));
	}
	else return -EINVAL;
	return 0;	
}


int trace_buffer_read(struct file *filep, char *buff, u32 count)
{
	if(count < 0) return -EINVAL;
	int can_write = is_valid_mem_range((unsigned long)buff, count, 1);
        if(!can_write) return -EBADMEM;

	int read_count = 0;
        int read_offset = filep->trace_buffer->read_offset;
        int write_offset = filep->trace_buffer->write_offset;
        int is_empty = filep->trace_buffer->size == 0;

        if(is_empty || count == 0) return 0;

        do{
                buff[read_count] = filep->trace_buffer->memory[read_offset];
                read_count++;
                read_offset = (read_offset + 1)%4096;
        }
        while(read_count < count && read_offset != write_offset);
	
	filep->trace_buffer->size -= read_count;
        filep->trace_buffer->read_offset = read_offset;

        return read_count;
}


int trace_buffer_write(struct file *filep, char *buff, u32 count)
{
	if(count < 0) return -EINVAL;
	int can_read = is_valid_mem_range((unsigned long)buff, count, 0);
	if(!can_read) return -EBADMEM;
	
	int write_count = 0;
	int read_offset = filep->trace_buffer->read_offset;
	int write_offset = filep->trace_buffer->write_offset;
	int is_full = filep->trace_buffer->size == TRACE_BUFFER_MAX_SIZE;

	if(is_full || count == 0) return 0;
	
	do{
		filep->trace_buffer->memory[write_offset] = buff[write_count];
                write_count++;
                write_offset = (write_offset + 1)%4096;
	}
	while(write_count < count && write_offset != read_offset);
	
	filep->trace_buffer->size += write_count;
	filep->trace_buffer->write_offset = write_offset;

    	return write_count;
}

int sys_create_trace_buffer(struct exec_context *current, int mode)
{
	if(mode != O_READ && mode != O_WRITE && mode != O_RDWR) return -EINVAL;	
	int fd = -1;
	for(int i=0;i<MAX_OPEN_FILES;i++){
		if(!current->files[i]){
			fd = i;
			break;
		}
	}

	if(fd == -1) return -EINVAL;
	
	struct file *new_file = (struct file*)os_alloc(sizeof(struct file));
	if(!new_file) return -ENOMEM;

	current->files[fd] = new_file;
	new_file->type = TRACE_BUFFER;
	new_file->mode = mode;
	new_file->offp = 0;
	new_file->ref_count = 1;
	new_file->inode = NULL;
	
	struct trace_buffer_info *trace_buffer = (struct trace_buffer_info*)os_alloc(sizeof(struct trace_buffer_info));
	struct fileops *fops = (struct fileops*)os_alloc(sizeof(struct fileops));
	
	if(!trace_buffer || !fops) return -ENOMEM;	

	new_file->trace_buffer = trace_buffer;
	new_file->fops = fops;

	trace_buffer->size = 0;
	trace_buffer->read_offset = 0;
	trace_buffer->write_offset = 0;
	trace_buffer->memory = (char *)os_page_alloc(USER_REG);

	fops->read = trace_buffer_read;
	fops->write = trace_buffer_write;
	fops->close = trace_buffer_close;

	return fd;
}

///////////////////////////////////////////////////////////////////////////
//// 		Start of strace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////


int perform_tracing(u64 syscall_num, u64 param1, u64 param2, u64 param3, u64 param4)
{
	struct exec_context *current = get_current_ctx();
	if(current->st_md_base->is_traced == 0 || SYSCALL_START_STRACE == syscall_num || SYSCALL_END_STRACE == syscall_num) return 0;	// Cross-Check
	struct file* filep = current->files[current->st_md_base->strace_fd];
	
	if(current->st_md_base->tracing_mode == FILTERED_TRACING){
		int flag = 0;
		struct strace_info * temp = current->st_md_base->next;
		while(temp){
			if(syscall_num == temp->syscall_num) flag = 1;
			temp = temp->next;
		}
		if(!flag) return 0;
	}

	if(SYSCALL_GETPID==syscall_num || SYSCALL_GETPPID==syscall_num || SYSCALL_FORK==syscall_num || SYSCALL_CFORK==syscall_num || SYSCALL_VFORK==syscall_num || SYSCALL_PHYS_INFO==syscall_num || SYSCALL_STATS==syscall_num || SYSCALL_GET_USER_P==syscall_num || SYSCALL_GET_COW_F==syscall_num){
		trace_buffer_write(filep, (char*)&syscall_num, 8);
	}
	else if(SYSCALL_EXIT==syscall_num || SYSCALL_CONFIGURE==syscall_num || SYSCALL_DUMP_PTT==syscall_num || SYSCALL_SLEEP==syscall_num || SYSCALL_PMAP==syscall_num || SYSCALL_DUP==syscall_num || SYSCALL_CLOSE==syscall_num || SYSCALL_TRACE_BUFFER==syscall_num){
		trace_buffer_write(filep, (char*)&syscall_num, 8);
		trace_buffer_write(filep, (char*)&param1, 8);
	}
	else if(SYSCALL_SIGNAL==syscall_num || SYSCALL_EXPAND==syscall_num || SYSCALL_CLONE==syscall_num || SYSCALL_MUNMAP==syscall_num || SYSCALL_OPEN==syscall_num || SYSCALL_DUP2==syscall_num || SYSCALL_STRACE==syscall_num){
		trace_buffer_write(filep, (char*)&syscall_num, 8);
		trace_buffer_write(filep, (char*)&param1, 8);
		trace_buffer_write(filep, (char*)&param2, 8);
	}
	else if(SYSCALL_MPROTECT==syscall_num || SYSCALL_READ==syscall_num || SYSCALL_WRITE==syscall_num || SYSCALL_LSEEK==syscall_num || SYSCALL_READ_STRACE==syscall_num || SYSCALL_READ_FTRACE==syscall_num){
		trace_buffer_write(filep, (char*)&syscall_num, 8);
		trace_buffer_write(filep, (char*)&param1, 8);
		trace_buffer_write(filep, (char*)&param2, 8);
		trace_buffer_write(filep, (char*)&param3, 8);
	}
	else{
		trace_buffer_write(filep, (char*)&syscall_num, 8);
		trace_buffer_write(filep, (char*)&param1, 8);
		trace_buffer_write(filep, (char*)&param2, 8);
		trace_buffer_write(filep, (char*)&param3, 8);
		trace_buffer_write(filep, (char*)&param4, 8);
	}
    	return 0;
}


int sys_strace(struct exec_context *current, int syscall_num, int action)
{
	if(!current->st_md_base){
		current->st_md_base = (struct strace_head*)os_alloc(sizeof(struct strace_head));
		current->st_md_base->next = NULL;
		current->st_md_base->last = NULL;	
	}
	int flag = 0, count = 0;
	struct strace_info *tmp = current->st_md_base->next;
	while(tmp){
		count++;
		if(tmp->syscall_num == syscall_num) flag = 1;
		tmp = tmp->next;
	}

	if(action == ADD_STRACE){
		if(flag || count==STRACE_MAX) return -EINVAL;
		struct strace_info *new_strace = (struct strace_info*)os_alloc(sizeof(struct strace_info));
		if(!new_strace) return -EINVAL;

                new_strace->syscall_num = syscall_num;
                new_strace->next = NULL;
		if(!current->st_md_base->next){
			current->st_md_base->next = new_strace;
			current->st_md_base->last = new_strace;
		}
		else{
			current->st_md_base->last->next = new_strace;
			current->st_md_base->last = new_strace;		// Cross-Check
		}
		current->st_md_base->count++;
	}
	else{
		if(!flag) return -EINVAL;
		struct strace_info *temp = current->st_md_base->next;
		if(temp->syscall_num == syscall_num){
			current->st_md_base->next = temp->next;
			os_free(temp, sizeof(struct strace_info));
		}

		while(temp->next){
			if(temp->next->syscall_num == syscall_num){
				struct strace_info *tmp = temp->next;
				temp->next = temp->next->next;
				if(temp->next == NULL) current->st_md_base->last = temp;
				os_free(tmp, sizeof(struct strace_info));
				break;
			}
		}
		current->st_md_base->count--;
	}
	return 0;
}

int sys_read_strace(struct file *filep, char *buff, u64 count)
{
	if(count < 0) return -EINVAL;
	int read_count = 0;
	while(count--){
		int bytes_count = trace_buffer_read(filep, buff+read_count, 8);
		read_count += bytes_count;
		if(bytes_count == 0) break;
		u64 syscall_num = *(u64*)(buff+read_count-8);

		if(SYSCALL_GETPID==syscall_num || SYSCALL_GETPPID==syscall_num || SYSCALL_FORK==syscall_num || SYSCALL_CFORK==syscall_num || SYSCALL_VFORK==syscall_num || SYSCALL_PHYS_INFO==syscall_num || SYSCALL_STATS==syscall_num || SYSCALL_GET_USER_P==syscall_num || SYSCALL_GET_COW_F==syscall_num){
	        }
        	else if(SYSCALL_EXIT==syscall_num || SYSCALL_CONFIGURE==syscall_num || SYSCALL_DUMP_PTT==syscall_num || SYSCALL_SLEEP==syscall_num || SYSCALL_PMAP==syscall_num || SYSCALL_DUP==syscall_num || SYSCALL_CLOSE==syscall_num || SYSCALL_TRACE_BUFFER==syscall_num){
                	read_count += trace_buffer_read(filep, buff+read_count, 8);
        	}
	        else if(SYSCALL_SIGNAL==syscall_num || SYSCALL_EXPAND==syscall_num || SYSCALL_CLONE==syscall_num || SYSCALL_MUNMAP==syscall_num || SYSCALL_OPEN==syscall_num || SYSCALL_DUP2==syscall_num || SYSCALL_START_STRACE==syscall_num || SYSCALL_STRACE==syscall_num){
	                read_count += trace_buffer_read(filep, buff+read_count, 8);
                	read_count += trace_buffer_read(filep, buff+read_count, 8);
        	}
	        else if(SYSCALL_MPROTECT==syscall_num || SYSCALL_READ==syscall_num || SYSCALL_WRITE==syscall_num || SYSCALL_LSEEK==syscall_num || SYSCALL_READ_STRACE==syscall_num || SYSCALL_READ_FTRACE==syscall_num){
			read_count += trace_buffer_read(filep, buff+read_count, 8);
	                read_count += trace_buffer_read(filep, buff+read_count, 8);
                	read_count += trace_buffer_read(filep, buff+read_count, 8);
        	}
	        else{
        	        read_count += trace_buffer_read(filep, buff+read_count, 8);
	                read_count += trace_buffer_read(filep, buff+read_count, 8);
                	read_count += trace_buffer_read(filep, buff+read_count, 8);
        	        read_count += trace_buffer_read(filep, buff+read_count, 8);
	        }
	}
	return read_count;
}

int sys_start_strace(struct exec_context *current, int fd, int tracing_mode)
{
	if(!current->st_md_base){
		current->st_md_base = (struct strace_head*)os_alloc(sizeof(struct strace_head));
		current->st_md_base->next = NULL;
		current->st_md_base->last = NULL;
	}
	current->st_md_base->count = 0;
	current->st_md_base->is_traced = 1;
	current->st_md_base->strace_fd = fd;
	current->st_md_base->tracing_mode = tracing_mode;
	return 0;
}

int sys_end_strace(struct exec_context *current)
{
	current->st_md_base->count = 0;
	current->st_md_base->is_traced = 0;
	struct strace_info *temp = current->st_md_base->next;
	while(temp){
		current->st_md_base->next = temp->next;
		os_free(temp, sizeof(struct strace_info));
		temp = current->st_md_base->next;
	}
	current->st_md_base->next = NULL;
	current->st_md_base->last = NULL;
	struct strace_head *tmp = current->st_md_base;
	os_free(tmp, sizeof(struct strace_head));
	current->st_md_base = NULL;
	return 0;
}



///////////////////////////////////////////////////////////////////////////
//// 		Start of ftrace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////


long do_ftrace(struct exec_context *ctx, unsigned long faddr, long action, long nargs, int fd_trace_buffer)
{
	int count = 0;
	struct ftrace_info *tmp = ctx->ft_md_base->next;
	struct ftrace_info *curr_ftrace = NULL;

	while(tmp){
		count++;
		if(tmp->faddr == faddr) curr_ftrace = tmp;
		tmp = tmp->next;
	}

	switch(action) {
		case ADD_FTRACE: {
			if(curr_ftrace || count==FTRACE_MAX) return -EINVAL;
			struct ftrace_info *new_ftrace = (struct ftrace_info*)os_alloc(sizeof(struct ftrace_info));
			new_ftrace->faddr = faddr;
			*(u32 *)new_ftrace->code_backup = -1;		// Verify
			new_ftrace->num_args = nargs;
			new_ftrace->fd = fd_trace_buffer;
			new_ftrace->capture_backtrace = 0;
			new_ftrace->next = NULL;
			
			if(!ctx->ft_md_base->next){
				ctx->ft_md_base->last = new_ftrace;
				ctx->ft_md_base->next = new_ftrace;
			}
			else{
				ctx->ft_md_base->last->next = new_ftrace;
				ctx->ft_md_base->last = new_ftrace;
			}
			ctx->ft_md_base->count++;
			break;
		}

		case REMOVE_FTRACE: {
			if(!curr_ftrace) return -EINVAL;
			*(u32 *)faddr = *(u32 *)curr_ftrace->code_backup;	// Verify
			curr_ftrace->capture_backtrace = 0;

			struct ftrace_info *temp = ctx->ft_md_base->next;
                	if(temp->faddr == faddr){
                        	ctx->ft_md_base->next = temp->next;
                        	os_free(temp, sizeof(struct ftrace_info));
	                }
			else{
        		        while(temp->next){
	                	        if(temp->next->faddr == faddr){
                        		        struct ftrace_info *tmp = temp->next;
                        	        	temp->next = temp->next->next;
	        	                        os_free(tmp, sizeof(struct ftrace_info));
        		                }
	                	}
			}
                	ctx->ft_md_base->count--;
			break;
		}

		case ENABLE_FTRACE: {
			if(!curr_ftrace) return -EINVAL;
			if(*(u32 *)faddr != -1){
				*(u32 *)curr_ftrace->code_backup = *(u32 *)faddr;
				*(u32 *)faddr = -1;
			}
			break;
		}
		case DISABLE_FTRACE: {
			if(!curr_ftrace) return -EINVAL;
			if(*(u32 *)faddr == -1){
				*(u32 *)faddr = *(u32 *)curr_ftrace->code_backup;
				*(u32 *)curr_ftrace->code_backup = -1;
			}
			break;
		}
		case ENABLE_BACKTRACE: {
			if(!curr_ftrace) return -EINVAL;
			if(*(u32 *)faddr != -1){
				*(u32 *)curr_ftrace->code_backup = *(u32 *)faddr;
                        	*(u32 *)faddr = -1;
			}
			curr_ftrace->capture_backtrace = 1;
			break;
		}
		case DISABLE_BACKTRACE: {
			if(!curr_ftrace) return -EINVAL;
			if(*(u32 *)faddr == -1){
                        	*(u32 *)faddr = *(u32 *)curr_ftrace->code_backup;
				*(u32 *)curr_ftrace->code_backup = -1;
			}
			curr_ftrace->capture_backtrace = 0;
                        break;
		}
		default: {
			return -EINVAL;
		}
	}
    	return 0;
}

//Fault handler
long handle_ftrace_fault(struct user_regs *regs)
{
	struct exec_context *ctx = get_current_ctx();
	struct ftrace_info *temp = ctx->ft_md_base->next;
	struct ftrace_info *curr_ftrace = NULL;
	while(temp){
		if(temp->faddr == regs->entry_rip){
			curr_ftrace = temp;
			break;
		}
		temp = temp->next;
	}
	
	int nargs = curr_ftrace->num_args;
	struct file *filep = ctx->files[curr_ftrace->fd];
	int minus_one = -1;
	trace_buffer_write(filep, (char *)&curr_ftrace->faddr, 8);
	if(nargs >= 1) trace_buffer_write(filep, (char *)&regs->rdi, 8);
	if(nargs >= 2) trace_buffer_write(filep, (char *)&regs->rsi, 8);
	if(nargs >= 3) trace_buffer_write(filep, (char *)&regs->rdx, 8);
	if(nargs >= 4) trace_buffer_write(filep, (char *)&regs->rcx, 8);
	if(nargs >= 5) trace_buffer_write(filep, (char *)&regs->r8, 8);

	if(curr_ftrace->capture_backtrace){
		trace_buffer_write(filep, (char *)&curr_ftrace->faddr, 8);
		u64 return_addr = *(u64 *)regs->entry_rsp;
		u64 rbp = regs->rbp;
		while(return_addr != END_ADDR){
			trace_buffer_write(filep, (char *)&return_addr, 8);
			return_addr = *(u64 *)(rbp + 8);
			rbp = *(u64 *)rbp;
		}
	}

	trace_buffer_write(filep, (char *)&minus_one, 8);

	regs->entry_rsp -= 8;
        *((u64 *)regs->entry_rsp) = regs->rbp;
        regs->rbp = regs->entry_rsp;
        regs->entry_rip += 4;

	return 0;
}


int sys_read_ftrace(struct file *filep, char *buff, u64 count)
{
	if(count < 0) return -EINVAL;
	int read_count = 0;
        while(count--){
		int bytes_count = 0;
		while(1){
                	bytes_count = trace_buffer_read(filep, buff+read_count, 8);
			if(bytes_count == 0) break;
                	int buffer_value = *(int *)(buff+read_count);
        	        if(buffer_value == -1) break;
                	read_count += bytes_count;
        	}
		if(bytes_count == 0) break;
	}
        return read_count;
}



