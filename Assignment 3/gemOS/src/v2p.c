#include <types.h>
#include <mmap.h>
#include <fork.h>
#include <v2p.h>
#include <page.h>
#define _4KB 4096
#define _2MB 2097152


/* 
 * You may define macros and other helper functions here
 * You must not declare and use any static/global variables 
 * */

void merge_mem(struct vm_area *vm){
	// printk("MERGE MEM\n");
	if(!vm || !vm->vm_next) return;
	if(vm->vm_end == vm->vm_next->vm_start && vm->access_flags == vm->vm_next->access_flags){
		struct vm_area *tmp = vm->vm_next;
		vm->vm_end = vm->vm_next->vm_end;
		vm->vm_next = vm->vm_next->vm_next;
		os_free(tmp, sizeof(struct vm_area));
		stats->num_vm_area--;
		merge_mem(vm);
	}
	else if(vm->vm_next){
		merge_mem(vm->vm_next);
	}
	return;
}

long create_mem(struct vm_area *vm, u64 addr, int size, int prot){
	// printk("CREATE MEM\n");
	struct vm_area *new_vm = (struct vm_area*)os_alloc(sizeof(struct vm_area));
	new_vm->vm_start = addr;
	new_vm->vm_end = addr + size;
	new_vm->access_flags = prot;
	new_vm->vm_next = NULL;
	stats->num_vm_area++;

	if(vm->vm_next) new_vm->vm_next = vm->vm_next;
	vm->vm_next = new_vm;

	merge_mem(vm);
	return addr;
}

void delete_pfn(u64 start_addr, u64 end_addr){
	// printk("DELETE PFN\n");
	struct exec_context *current = get_current_ctx();
	for(u64 addr=start_addr; addr < end_addr; addr += _4KB){
		u64 pgd = addr << 16;   pgd = pgd >> 55;
	        u64 pud = addr << 25;   pud = pud >> 55;
	        u64 pmd = addr << 34;   pmd = pmd >> 55;
        	u64 pte = addr << 43;   pte = pte >> 55;
	        u64 offset = offset << 52;
        	offset = offset >> 52;

	        u64 addr1 = (u64 )osmap(current->pgd) + 8 * pgd;
        	if((*(u64 *)addr1 & 1) == 0) continue;

	        u64 val1 = *(u64 *)addr1;
        	u64 addr2 = (u64 )osmap(val1 >> 12) + 8 * pud;
	        if((*(u64 *)addr2 & 1) == 0) continue;

        	u64 val2 = *(u64 *)addr2;
	        u64 addr3 = (u64 )osmap(val2 >> 12) + 8 * pmd;
        	if((*(u64 *)addr3 & 1) == 0) continue;

	        u64 val3 = *(u64 *)addr3;
        	u64 addr4 = (u64 )osmap(val3 >> 12) + 8 * pte;
	        if((*(u64 *)addr4 & 1) == 0) continue;
		
		u64 val4 = *(u64 *)addr4;
		put_pfn((val4 >> 12));
		if(get_pfn_refcount((val4 >> 12)) == 0){
			os_pfn_free(USER_REG, (val4 >> 12));
			*(u64 *)addr4 = *(u64 *)addr4 & (0);
		}
	}

	// Flush the TLB cache
        u64 cr3_value;
        asm volatile(
                "mov %%cr3, %0"
                : "=r" (cr3_value)
        );
        asm volatile(
                "mov %0, %%rax\n\t"
                "mov %%rax, %%cr3"
                :
                :"r" (cr3_value)
                :"eax"
        );
}

void protect_pfn(u64 start_addr, u64 end_addr, int prot){
	// printk("PROTECT PFN\n");
        struct exec_context *current = get_current_ctx();
	u64 gap = _4KB;
        
	for(u64 addr=start_addr; addr < end_addr; addr += gap){
                u64 pgd = addr << 16;   pgd = pgd >> 55;
                u64 pud = addr << 25;   pud = pud >> 55;
                u64 pmd = addr << 34;   pmd = pmd >> 55;
                u64 pte = addr << 43;   pte = pte >> 55;
                u64 offset = offset << 52;
                offset = offset >> 52;

                u64 addr1 = (u64 )osmap(current->pgd) + 8 * pgd;
                if((*(u64 *)addr1 & 1) == 0) continue;
		if(prot==PROT_READ|PROT_WRITE) *(u64 *)addr1 |= 8;

                u64 val1 = *(u64 *)addr1;
                u64 addr2 = (u64 )osmap(val1 >> 12) + 8 * pud;
                if((*(u64 *)addr2 & 1) == 0) continue;
		if(prot==PROT_READ|PROT_WRITE) *(u64 *)addr2 |= 8;

                u64 val2 = *(u64 *)addr2;
                u64 addr3 = (u64 )osmap(val2 >> 12) + 8 * pmd;
                if((*(u64 *)addr3 & 1) == 0) continue;
		if(prot==PROT_READ|PROT_WRITE) *(u64 *)addr3 |= 8;

                u64 val3 = *(u64 *)addr3;
                u64 addr4 = (u64 )osmap(val3 >> 12) + 8 * pte;
                if((*(u64 *)addr4 & 1) == 0) continue;

                if(prot==PROT_READ) *(u64 *)addr4 &= (-9);	// VERIFY
		else *(u64 *)addr4 |= 8;
        }

	// Flush the TLB cache
	u64 cr3_value;
	asm volatile(
        	"mov %%cr3, %0"
        	: "=r" (cr3_value)
	);
    	asm volatile(
        	"mov %0, %%rax\n\t"
        	"mov %%rax, %%cr3"
        	:
       		:"r" (cr3_value)
        	:"eax"
	);
}


// Verify corner cases properly for same addr and start/end values
void delete_mem(struct vm_area *vm, u64 addr, int size){
	// printk("DELETE MEM\n");
	if(!vm || !vm->vm_next) return;
	if(addr + size <= vm->vm_next->vm_start) return;
	else if(addr >= vm->vm_next->vm_end){
		delete_mem(vm->vm_next, addr, size);
	}
	else if(addr <= vm->vm_next->vm_start && vm->vm_next->vm_end <= addr + size){
		delete_pfn(vm->vm_next->vm_start, vm->vm_next->vm_end);
		
		struct vm_area *tmp = vm->vm_next;
		vm->vm_next = tmp->vm_next;
		os_free(tmp, sizeof(struct vm_area));
		stats->num_vm_area--;
		delete_mem(vm, addr, size);
	}
	else if(addr <= vm->vm_next->vm_start && addr + size < vm->vm_next->vm_end){
		delete_pfn(vm->vm_next->vm_start, addr + size);
		
		vm->vm_next->vm_start = addr + size;
	}
	else if(vm->vm_next->vm_start < addr && vm->vm_next->vm_end <= addr + size){
		delete_pfn(addr, vm->vm_next->vm_end);
		
		vm->vm_next->vm_end = addr;
		delete_mem(vm->vm_next, addr, size);
		// delete_mem(vm->vm_next, vm->vm_next->vm_end, size);		// VERIFY
	}
	else if(vm->vm_next->vm_start < addr && addr + size < vm->vm_next->vm_end){
		delete_pfn(addr, addr + size);

		struct vm_area *new_vm = (struct vm_area*)os_alloc(sizeof(struct vm_area));
		new_vm->vm_start = addr + size;
		new_vm->vm_end = vm->vm_next->vm_end;
		new_vm->access_flags = vm->vm_next->access_flags;
		new_vm->vm_next = vm->vm_next->vm_next;
		vm->vm_next->vm_end = addr;
		vm->vm_next->vm_next = new_vm;
		stats->num_vm_area++;
	}
	return;
}

// Verify corner cases properly for same addr and start/end values
void protect_mem(struct vm_area *vm, u64 addr, int size, int prot){ 
	// printk("PROTECT MEM\n");
	if(!vm || !vm->vm_next) return;
	int access_flags = vm->vm_next->access_flags;
	
	if(addr + size <= vm->vm_next->vm_start) return;
        else if(addr >= vm->vm_next->vm_end){
                protect_mem(vm->vm_next, addr, size, prot);
        }
	else if(addr <= vm->vm_next->vm_start && vm->vm_next->vm_end <= addr + size){
		if(access_flags != prot) protect_pfn(vm->vm_next->vm_start, vm->vm_next->vm_end, prot);
		
        	vm->vm_next->access_flags = prot;
		protect_mem(vm->vm_next, addr, size, prot);
		// protect_mem(vm->vm_next, vm->vm_next->vm_end, size, prot);
	}
        else if(addr <= vm->vm_next->vm_start && addr + size < vm->vm_next->vm_end){
		if(access_flags != prot){
			protect_pfn(vm->vm_next->vm_start, addr + size, prot);

        		struct vm_area *new_vm = (struct vm_area*)os_alloc(sizeof(struct vm_area));
			new_vm->vm_start = vm->vm_next->vm_start;
			new_vm->vm_end = addr + size;
			new_vm->access_flags = prot;
			new_vm->vm_next = vm->vm_next;
		
			vm->vm_next->vm_start = addr + size;
			vm->vm_next = new_vm;
			stats->num_vm_area++;
		}
	}
        else if(vm->vm_next->vm_start < addr && vm->vm_next->vm_end <= addr + size){
		if(access_flags != prot){
			protect_pfn(addr, vm->vm_next->vm_end, prot);

        		struct vm_area *new_vm = (struct vm_area*)os_alloc(sizeof(struct vm_area));
	                new_vm->vm_start = addr;
                	new_vm->vm_end = vm->vm_next->vm_end;
        	        new_vm->access_flags = prot;
	                new_vm->vm_next = vm->vm_next->vm_next;
                
			vm->vm_next->vm_end = addr;
        	        vm->vm_next->vm_next = new_vm;
			stats->num_vm_area++;
			protect_mem(new_vm, addr, size, prot);
		}
		else{
			protect_mem(vm->vm_next, addr, size, prot);
		}
	}
        else if(vm->vm_next->vm_start < addr && addr + size < vm->vm_next->vm_end){
		if(access_flags != prot){
			protect_pfn(addr, addr + size, prot);

        		struct vm_area *new_vm1 = (struct vm_area*)os_alloc(sizeof(struct vm_area));
			struct vm_area *new_vm2 = (struct vm_area*)os_alloc(sizeof(struct vm_area));
			new_vm2->vm_start = addr + size;
			new_vm2->vm_end = vm->vm_next->vm_end;
			new_vm2->access_flags = vm->vm_next->access_flags;
			new_vm2->vm_next = vm->vm_next->vm_next;

			new_vm1->vm_start = addr;
			new_vm1->vm_end = addr + size;
			new_vm1->access_flags = prot;
			new_vm1->vm_next = new_vm2;
		
			vm->vm_next->vm_end = addr;
			vm->vm_next->vm_next = new_vm1;
			stats->num_vm_area++;
			stats->num_vm_area++;
		}
	}
        return;
}

struct vm_area *copy_vm_area(struct vm_area *src){
	// printk("COPY VM AREA\n");
	if(!src) return NULL;
	struct vm_area *new_vm = (struct vm_area*)os_alloc(sizeof(struct vm_area));
	new_vm->vm_start = src->vm_start;
	new_vm->vm_end = src->vm_end;
	new_vm->access_flags = src->access_flags;
	new_vm->vm_next = copy_vm_area(src->vm_next);
	return new_vm;
}

int is_valid_mem_range(u64 start_addr, u64 end_addr) {
	struct exec_context *current = get_current_ctx();
	int flag[4] = {1, 1, 1, 1};
	if(end_addr <= current->mms[MM_SEG_CODE].start || start_addr >= current->mms[MM_SEG_CODE].next_free) flag[0] = 0;
	if(end_addr <= current->mms[MM_SEG_RODATA].start || start_addr >= current->mms[MM_SEG_RODATA].next_free) flag[1] = 0;
	if(end_addr <= current->mms[MM_SEG_DATA].start || start_addr >= current->mms[MM_SEG_DATA].next_free) flag[2] = 0;
	if(end_addr <= current->mms[MM_SEG_STACK].start || start_addr >= current->mms[MM_SEG_STACK].end) flag[3] = 0;
	if(flag[0] || flag[1] || flag[2] || flag[3]) return 1;

	struct vm_area *curr = current->vm_area;
	while(curr){
		int flag = 1;
		if(end_addr <= curr->vm_start || start_addr >= curr->vm_end) flag = 0;
		curr = curr->vm_next;
		if(flag) return 1;
	}

	return 0;
}

u32 copy_page_table(struct exec_context *ctx){
	// printk("COPY PAGE TABLE\n");
	u32 pgd = ctx->pgd;
	u32 new_pgd = os_pfn_alloc(OS_PT_REG);

	for(u64 i=0;i<(1<<9);i++){
		u64 addr1 = (u64 )osmap(pgd) + 8 * i;
		u64 new_addr1 = (u64 )osmap(new_pgd) + 8 * i;
		u64 val1 = *(u64 *)addr1;
		u64 base = 0;

		if((val1 & 1) && is_valid_mem_range((base + (i<<39)), (base + ((i+1)<<39)))){
			u32 temp1 = os_pfn_alloc(OS_PT_REG);
			// printk("%x\n", val1 & ((1<<12) - 1));
            *(u64 *)new_addr1 = ((temp1<<12) | (val1 & ((1<<12) - 1)));
			u64 new_val1 = *(u64 *)new_addr1;

			for(u64 j=0;j<(1<<9);j++){
				u64 addr2 = (u64 )osmap((val1 >> 12)) + 8 * j;
				u64 new_addr2 = (u64 )osmap((new_val1 >> 12)) + 8 * j;
				u64 val2 = *(u64 *)addr2;
				base = (i<<39);

				if((val2 & 1) && is_valid_mem_range((base + (j<<30)), (base + ((j+1)<<30)))){
					u32 temp2 = os_pfn_alloc(OS_PT_REG);
					*(u64 *)new_addr2 = ((temp2<<12) | (val2 & ((1<<12) - 1)));
					u64 new_val2 = *(u64 *)new_addr2;

					for(u64 k=0;k<(1<<9);k++){
						u64 addr3 = (u64 )osmap((val2 >> 12)) + 8 * k;
						u64 new_addr3 = (u64 )osmap((new_val2 >> 12)) + 8 * k;
						u64 val3 = *(u64 *)addr3;
						base = ((i<<39) + (j<<30));

						if((val3 & 1) && is_valid_mem_range((base + (k<<21)), (base + ((k+1)<<21)))){
							u32 temp3 = os_pfn_alloc(OS_PT_REG);
							*(u64 *)new_addr3 = ((temp3<<12) | (val3 & ((1<<12) - 1)));
							u64 new_val3 = *(u64 *)new_addr3;

							for(u64 l=0;l<(1<<9);l++){
								u64 addr4 = (u64 )osmap((val3 >> 12)) + 8 * l;
								u64 new_addr4 = (u64 )osmap((new_val3 >> 12)) + 8 * l;
								u64 val4 = *(u64 *)addr4;
								base = ((i<<39) + (j<<30) + (k<<21));
								
								if((val4 & 1) && is_valid_mem_range((base + (l<<12)), (base + ((l+1)<<12)))){
									*(u64 *)new_addr4 = val4;
									*(u64 *)addr4 &= (-9);
									*(u64 *)new_addr4 &= (-9);
									val4 = *(u64 *)addr4;
									u64 new_val4 = *(u64 *)new_addr4;
									// printk("%x	%x	%x	%x\n", addr4, new_addr4, val4, new_val4);
									get_pfn((val4 >> 12));
									// printk("%d\n", get_pfn_refcount((val4>>12)));
								}
							}
						}
					}
				}
			}
		}
	}

	return new_pgd;
}

/**
 * mprotect System call Implementation.
 */
long vm_area_mprotect(struct exec_context *current, u64 addr, int length, int prot)
{
	// printk("VM AREA PROTECT\n");
	int size = length + (_4KB - length % _4KB) % _4KB;
        if(length <= 0 || length > _2MB) return -EINVAL;
        if(prot != PROT_READ && prot != PROT_WRITE && prot != (PROT_READ | PROT_WRITE)) return -EINVAL;

	struct vm_area *curr = current->vm_area;
        while(curr->vm_next){
                // if(addr >= curr->vm_next->vm_end || addr + size <= curr->vm_next->vm_start) curr = curr->vm_next;
				if(addr >= curr->vm_next->vm_end) curr = curr->vm_next;		// VERIFY
                else{
                        protect_mem(curr, addr, size, prot);
			break;			// VERIFY
                }
        }

	merge_mem(current->vm_area);

	return 0;
	return -EINVAL;
}

/**
 * mmap system call implementation.
 */
long vm_area_map(struct exec_context *current, u64 addr, int length, int prot, int flags)
{
	// printk("VM AREA MAP\n");
	int size = length + (_4KB - length % _4KB) % _4KB;
	if(length <= 0 || length > _2MB) return -EINVAL;
	if(prot != PROT_READ && prot != PROT_WRITE && prot != (PROT_READ | PROT_WRITE)) return -EINVAL;
	if(flags != 0 && flags != MAP_FIXED) return -EINVAL;
	
	// Create dummy node if not in the list
	if(!current->vm_area){
		struct vm_area *dummy = (struct vm_area*)os_alloc(sizeof(struct vm_area));
		dummy->vm_start = MMAP_AREA_START;
		dummy->vm_end = MMAP_AREA_START + _4KB;
		dummy->vm_next = NULL;
		dummy->access_flags = 0;
		current->vm_area = dummy;
		stats->num_vm_area++;
	}

	int find_first = 0;
	if(addr){
		struct vm_area *curr = current->vm_area;
		struct vm_area *prev = NULL;
		
		while(curr){
			if(curr->vm_end <= addr) prev = curr;
			curr = curr->vm_next;
		}
		
		if(!prev) return -EINVAL;
		else if(addr + size > MMAP_AREA_END){
			if(flags == MAP_FIXED) return -EINVAL;
			else find_first = 1;
		}
		else if(!prev->vm_next){
			return create_mem(prev, addr, size, prot);
		}
		else if(addr + size <= prev->vm_next->vm_start){		// VERIFY
			return create_mem(prev, addr, size, prot);
		}
		else if(flags == MAP_FIXED){
			return -1;
		}
		else find_first = 1;
	}
	else{
		if(flags == MAP_FIXED) return -1;
	}
	
	struct vm_area *curr = current->vm_area;
	struct vm_area *prev = NULL;
	while(curr){
		if(curr->vm_next){
			u64 new_addr = curr->vm_end;
			if(new_addr + size <= curr->vm_next->vm_start){
				return create_mem(curr, new_addr, size, prot);
			}
			else curr = curr->vm_next;
		}
		else{
			u64 new_addr = curr->vm_end;
			if(new_addr + size <= MMAP_AREA_END){
				return create_mem(curr, new_addr, size, prot);
			}
			else return -EINVAL;
		}
	}

    	return -EINVAL;
}

/**
 * munmap system call implemenations
 */

long vm_area_unmap(struct exec_context *current, u64 addr, int length)
{
	// printk("VM AREA UNMAP\n");
	int size = length + (_4KB - length % _4KB) % _4KB;
	if(length <= 0 || length > _2MB) return -EINVAL;

	struct vm_area *curr = current->vm_area;
	while(curr->vm_next){
		// if(addr >= curr->vm_next->vm_end || addr + size <= curr->vm_next->vm_start) curr = curr->vm_next;
		if(addr >= curr->vm_next->vm_end) curr = curr->vm_next;		// VERIFY
		else{
			delete_mem(curr, addr, size);
			break;			// VERIFY
		}
	}

	return 0;
	return -EINVAL;
}



/**
 * Function will invoked whenever there is page fault for an address in the vm area region
 * created using mmap
 */

long vm_area_pagefault(struct exec_context *current, u64 addr, int error_code)
{
	// printk("VM AREA PAGEFAULT\n");
	struct vm_area *curr = current->vm_area;
	struct vm_area *vm = NULL;
	while(curr){
		if(addr >= curr->vm_start && addr < curr->vm_end) vm = curr;
		curr = curr->vm_next;
	}
	
	if(!vm) return -1;
	if(error_code == 7){
		if(vm->access_flags == PROT_READ) return -1;
		else handle_cow_fault(current, addr, vm->access_flags);
	}
	if(error_code == 6 && vm->access_flags == PROT_READ) return -1;

	u64 pgd = addr << 16;	pgd = pgd >> 55;
	u64 pud = addr << 25;	pud = pud >> 55;
	u64 pmd = addr << 34;	pmd = pmd >> 55;
	u64 pte = addr << 43;	pte = pte >> 55;
	u64 offset = addr << 52;
	offset = offset >> 52;

	u64 addr1 = (u64 )osmap(current->pgd) + 8 * pgd;
	if((*(u64 *)addr1 & 1) == 0){
                u32 temp = os_pfn_alloc(OS_PT_REG);
                *(u64 *)addr1 = (temp<<12) | (error_code<<2) | 1;
		// *(u64 *)addr1 |= 8;
        }	

	u64 val1 = *(u64 *)addr1;
	u64 addr2 = (u64 )osmap(val1 >> 12) + 8 * pud;
	if((*(u64 *)addr2 & 1) == 0){
                u32 temp = os_pfn_alloc(OS_PT_REG);
                *(u64 *)addr2 = (temp<<12) | (error_code<<2) | 1;
		// *(u64 *)addr2 |= 8;
	}

	u64 val2 = *(u64 *)addr2;
	u64 addr3 = (u64 )osmap(val2 >> 12) + 8 * pmd;
	if((*(u64 *)addr3 & 1) == 0){
                u32 temp = os_pfn_alloc(OS_PT_REG);
                *(u64 *)addr3 = (temp<<12) | (error_code<<2) | 1;
		// *(u64 *)addr3 |= 8;
        }
	
	u64 val3 = *(u64 *)addr3;
        u64 addr4 = (u64 )osmap(val3 >> 12) + 8 * pte;
	if((*(u64 *)addr4 & 1) == 0){
                u32 temp = os_pfn_alloc(USER_REG);
                *(u64 *)addr4 = (temp<<12) | (error_code<<2) | 1;
		// *(u64 *)addr4 |= 8;
        }

	return 1;
}

/**
 * cfork system call implemenations
 * The parent returns the pid of child process. The return path of
 * the child process is handled separately through the calls at the 
 * end of this function (e.g., setup_child_context etc.)
 */

long do_cfork(){
    	u32 pid;
    	struct exec_context *new_ctx = get_new_ctx();
    	struct exec_context *ctx = get_current_ctx();
	/* Do not modify above lines
	* 
	* */   
	/*--------------------- Your code [start]---------------*/
	// printk("CFORKKKKKKKKKK\n");
	new_ctx->ppid = ctx->pid;
	new_ctx->type = ctx->type;
	new_ctx->state = ctx->state;
	new_ctx->used_mem = ctx->used_mem;
	new_ctx->pgd = copy_page_table(ctx);
	for(int i=0;i<MAX_MM_SEGS;i++){
	 	new_ctx->mms[i] = ctx->mms[i];
	}
	new_ctx->vm_area = copy_vm_area(ctx->vm_area);
	for(int i=0;i<CNAME_MAX;i++){
	 	new_ctx->name[i] = ctx->name[i];
	}
	new_ctx->regs = ctx->regs;
	new_ctx->pending_signal_bitmap = ctx->pending_signal_bitmap;
	for(int i=0;i<MAX_SIGNALS;i++){
		new_ctx->sighandlers[i] = ctx->sighandlers[i];
	}
	new_ctx->ticks_to_sleep = ctx->ticks_to_sleep;
	new_ctx->alarm_config_time = ctx->alarm_config_time;
	new_ctx->ticks_to_alarm = ctx->ticks_to_alarm;
	for(int i=0;i<MAX_OPEN_FILES;i++){
		new_ctx->files[i] = ctx->files[i];
	}
	pid = new_ctx->pid;

	/*--------------------- Your code [end] ----------------*/
	
	/*
	* The remaining part must not be changed
	*/
    	copy_os_pts(ctx->pgd, new_ctx->pgd);
    	do_file_fork(new_ctx);
    	setup_child_context(new_ctx);
	// printk("CFORKKKKKKKKKK\n");
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
	// printk("COWWWWWWWWWWWw\n");
	// if(access_flags == PROT_READ) return 1;
	// if(access_flags != PROT_WRITE && access_flags != (PROT_READ|PROT_WRITE)) return -1;
	
	u64 pgd = vaddr << 16;   pgd = pgd >> 55;
        u64 pud = vaddr << 25;   pud = pud >> 55;
        u64 pmd = vaddr << 34;   pmd = pmd >> 55;
        u64 pte = vaddr << 43;   pte = pte >> 55;
        u64 offset = vaddr << 52;
        offset = offset >> 52;

        u64 addr1 = (u64 )osmap(current->pgd) + 8 * pgd;
        u64 val1 = *(u64 *)addr1;
        u64 addr2 = (u64 )osmap(val1 >> 12) + 8 * pud;
        u64 val2 = *(u64 *)addr2;
        u64 addr3 = (u64 )osmap(val2 >> 12) + 8 * pmd;
        u64 val3 = *(u64 *)addr3;
        u64 addr4 = (u64 )osmap(val3 >> 12) + 8 * pte;
	u64 val4 = *(u64 *)addr4;

	u32 temp = os_pfn_alloc(USER_REG);
        memcpy(osmap(temp), osmap(val4 >> 12), _4KB);
        get_pfn(temp);
        put_pfn(val4 >> 12);
        *(u64 *)addr4 |= 8;
        *(u64 *)addr4 &= 0xfff;
        *(u64 *)addr4 |= (temp << 12);
	
	return 1;
	return -1;
}

