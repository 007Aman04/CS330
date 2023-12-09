#include <stdio.h>
#include <sys/mman.h>

unsigned long *HEAD = NULL;

void delete_chunk(unsigned long *node){
	if(!node) return;
	if(HEAD == node){
		HEAD = (unsigned long *)node[1];
		if(HEAD) HEAD[2] = (unsigned long)NULL;
	}
	else{
		if(node[1]) ((unsigned long *)node[1])[2] = node[2];
		if(node[2]) ((unsigned long *)node[2])[1] = node[1];
	}
	// node[1] = node[2] = (unsigned long)NULL;
}

void insert_chunk(unsigned long *node, unsigned long space){
	unsigned long *newNode = node + space / 8;
	newNode[0] = node[0] - space;
	newNode[1] = (unsigned long)HEAD;
	newNode[2] = (unsigned long)NULL;
	if(newNode[1]) ((unsigned long *)newNode[1])[2] = (unsigned long)newNode;
	HEAD = newNode;
}

void create_chunk(unsigned long *node, unsigned long space){
	unsigned long *newNode = node;
	newNode[0] = space;
	newNode[1] = (unsigned long)HEAD;
	newNode[2] = (unsigned long)NULL;
	if(newNode[1]) ((unsigned long *)newNode[1])[2] = (unsigned long)newNode;
	HEAD = newNode;
}

void *memalloc(unsigned long size) 
{
	if(size == 0) return NULL;
	unsigned long *temp = HEAD;
	unsigned long padding = 8 - size % 8;
	if(size%8==0) padding = 0;

	if(size + 8 + padding < 16) padding += 8;
	if(size + 8 + padding < 24) padding += 8;
	
	while(temp){
		unsigned long space = *temp;

		if(space < size + 8 + padding){
			temp = (unsigned long *)temp[1];
			continue;
		}

		if(space == size + 8 + padding){
			delete_chunk(temp);
			temp[0] = (unsigned long)space;
			return temp + 1;
		}

		if(space - size - 8 - padding < 24){
			delete_chunk(temp);
			temp[0] = (unsigned long)(space);
			return temp + 1;
		}

		if(space - size - 8 - padding >= 24){
			delete_chunk(temp);
			insert_chunk(temp, size + 8 + padding);
			temp[0] = (unsigned long)(size + 8 + padding);
			return temp + 1;
		}
	}
	
	unsigned long MB = 1024 * 1024;
	unsigned long count = (size + 8 + padding) / (4 * MB);
	if(4 * MB * count < size + 8 + padding) count++;
	
	unsigned long *ptr = mmap(NULL, 4 * MB * count, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (ptr == MAP_FAILED) {
        return NULL;
    }
	if((4 * MB * count == size + 8 + padding) || (4 * MB * count - size - 8 - padding < 24)){
		ptr[0] = 4 * MB * count;
		return ptr + 1;
	}

	ptr[0] = 4 * MB * count;
	// ptr[1] = (unsigned long)NULL;
	// ptr[2] = (unsigned long)NULL;
	insert_chunk(ptr, size + 8 + padding);
	ptr[0] = size + 8 + padding;
	return ptr + 1;

	// printf("memalloc() called\n");
	// return NULL;
}

int memfree(void *ptr)
{
	if(!ptr) return -1;
	unsigned long *ptrCopy =  (unsigned long *)ptr;
	ptrCopy -= 1;
	unsigned long *temp = HEAD;
	unsigned long *ptr1 = NULL;
	unsigned long *ptr2 = NULL;
	while(temp){
		unsigned long size = *temp;
		unsigned long space = *ptrCopy;
		if(temp + size/8 == ptrCopy){
			ptr1 = temp;
		}

		if(ptrCopy + space/8 == temp){
			ptr2 = temp;
		}

		temp = (unsigned long *)temp[1];
	}

	if(ptr1) delete_chunk(ptr1);
	if(ptr2) delete_chunk(ptr2);
	
	if(!ptr1 && !ptr2){
		create_chunk(ptrCopy, *ptrCopy);
		return 0;
	}

	else if(!ptr1 && ptr2){
		create_chunk(ptrCopy, *ptrCopy + *ptr2);
		return 0;
	}

	else if(ptr1 && !ptr2){
		create_chunk(ptr1, *ptr1 + *ptrCopy);
		return 0;
	}

	else{
		create_chunk(ptr1, *ptr1 + *ptrCopy + *ptr2);
		return 0;
	}

	// printf("memfree() called\n");
	// return 0;
}	
