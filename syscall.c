#include "syscall.h"

/*
* int32_t sys_halt(uint8_t* status)
*   Inputs: status - for return 0 to avoid abnormal termination, modified in syscallz.S
*   Outputs: none
*	For jumping back to the execute source, restore the pcbs and the ebps and esps, use
* 	jump to jump to the halt label in the execute program, just return.
*/

uint32_t pids[PIDS_SIZE]={0};
int32_t sys_halt(uint8_t* status){
	//uint32_t flags; // this fixed the halt prolly
	cli();
	pcb_t cur_pcb = *(pcb_t*)PCB_LOCATION;
	
	free_pid(pid);
	
	pid=cur_pcb.parent_pid;
	//stacks start at the bottom of the page 

	tss.esp0 = PCB_STACKS_START-(pid)*PAGE_SIZE*2-4;
	pcb_t* old_pcb = PCB_LOCATION;
	old_pcb->next=cur_pcb.next; // this fixes the exit shell problem
	old_pcb->prev=cur_pcb.prev;
	((pcb_t*)cur_pcb.prev)->next=old_pcb; // like removing linked lists
	((pcb_t*)cur_pcb.next)->prev=old_pcb;
	disable_paging();
	//free the used pcb
	free(cur_pcb.physical_addr);
	free((void*)(cur_pcb.process_page_directory[VIRTUAL_ADDR>>PAGE_DIRECTORY_SHIFT]&FIRST_20_BITS));
	free((void*)(cur_pcb.process_page_directory));
	//if process ran from shell, use same page as the original process
	if(pid>=NUM_TERMINALS){
		set_page_directory(old_pcb->process_page_directory);
		
	}
	//else set a new page
	else{
		set_page_directory(page_directory);
	}
	enable_paging();
	//restore_flags(flags);
	//checks for status, restores esp and ebp
	flush_tlb();
	asm volatile(
				"	movl	%2, %%eax				\n\
					andl	$0xFF, %%eax			\n\
					 movl 	%0, %%esp 				\n\
					movl 	%1, %%ebp				\n\
    			jmp halt_ret_label					\n\
    			"
    			: 
    			:	"g"(old_pcb->esp), "g"(old_pcb->ebp), "a" (status)
    			: "memory", "ecx"
    			);
	return 0;
	// the cr3 reloading; flush the tlb!!
	
}
/*
* int32_t execute(const uint8_t* command)
*   Inputs: const uint8_t* command
*   Outputs: none
*	Executes a command, basically black magic.  Checks if it's executable with ELF and finds entry point.  Loads a new program and executes by 
*	doing context switching with the TSS and iret (getting into ring 3 on OSDEV).  Creates a new PCB for each new process, Save the ESP and EBP
* 	in the PCB struct, and load the new esp and ebp to be restored at halt.  Copies the new PCB into the PCB location, updates the pid.  Also
*	opens stdin and stdout.
*/
int32_t execute(const uint8_t* command){
	if(command== NULL) return -1;
	int8_t command_buf[ARGLEN];
	int8_t args_buf[ARGLEN];
	parse_args((int8_t*)command, (int8_t*)command_buf, (int8_t*)args_buf);
	dentry_t temp_dentry;
	uint32_t entry_point;
	//first parse for the file
	if(read_dentry_by_name((uint8_t*)command_buf, &temp_dentry)!=0){
		printf("file does not exist\n");
		return -1;
	}

	uint8_t header[HEADER_SIZE];
	//next read the header
	read_data(temp_dentry.inode,0,header, HEADER_SIZE);

	//check if first field is ELF, if not then incorrect
	if(*((uint32_t*)header)!=ELF_MAGIC){
		printf("incorrect file type!\n");
		return -1;
	}
	cli();
	disable_paging();
	pcb_t new_pcb;
	new_pcb.physical_addr=malloc(1); // size greater than 1
	new_pcb.process_page_directory=(uint32_t*) malloc(0);
	//printf("physical address: %x \n page dir address: %x \n", new_pcb.physical_addr, new_pcb.process_page_directory);
	
	int i;
	for(i=0;i<PAGE_ENTRIES;i++){
		new_pcb.process_page_directory[i]=FULL_NOTUSED;
	}
	new_pcb.process_page_directory[0]=((uint32_t)page_table0)|PAGE_TABLE_SETTINGS_11_0;
	new_pcb.process_page_directory[1]=KERNEL_PAGE;
	new_pcb.process_page_directory[2]=((uint32_t)page_table_vid)|PAGE_TABLE_SETTINGS_11_0;
	set_page_directory(new_pcb.process_page_directory);
	for(i=0;i<PAGE_ENTRIES;i++){
			map_4kbpage(VIRTUAL_ADDR+i*PAGE_SIZE, (uint32_t)new_pcb.physical_addr+i*PAGE_SIZE,1, PAGE_TABLE_ENTRY_SETTINGS);
	}
	//get the E_ENTRY, offset 0x18
	//set_page_directory(new_pcb.process_page_directory);
	entry_point = *((uint32_t*)(header + E_ENTRY_OFFSET));
	read_data(temp_dentry.inode,0,new_pcb.physical_addr+PROGRAM_IMG_OFFSET, -1);
	enable_paging();
	pcb_t* old_pcb = PCB_LOCATION;
		//and i flushed tlb again in case; maybe don't do it
	asm volatile("								\n\
	    		movl %%esp, %0				\n\
	    		movl %%ebp, %1							\n\
	    		"							
	    		: "=g"(old_pcb->esp), "=g" (old_pcb->ebp)
	    		:
	    		: "memory", "eax", "ebx"
	    		);
	
	new_pcb.next=old_pcb->next; 
	new_pcb.prev=old_pcb->prev;

	new_pcb.parent_pid=pid;
	pid=find_pid(); // find new pid
	((pcb_t*)new_pcb.prev)->next=PCB_LOCATION;
	((pcb_t*)new_pcb.next)->prev=PCB_LOCATION;
	new_pcb.terminal_num=old_pcb->terminal_num; // executing in shell, just use the same terminal
	tss.ss0 = KERNEL_DS;
	tss.esp0 = PCB_STACKS_START-(pid)*PAGE_SIZE*2-4;
	

	memcpy(&new_pcb.args, args_buf, sizeof(args_buf));
	new_pcb.pid=pid; //set the new pid
	//new_pcb.vidmapped = 0;

	//printf("%d",sizeof(pcb_t));
	
	//memcpy(dest, src), move into the relevant PCB_LOCATION based on pid
	memcpy((void*)PCB_LOCATION,&new_pcb, sizeof(pcb_t));
	
	sti();
	
	open((uint8_t*)"stdin");
	open((uint8_t*)"stdout");
	prepare_iret((uint32_t)USER_DS, (uint32_t)USER_STACK, (uint32_t)USER_CS,(uint32_t) entry_point);
	asm volatile("								\n\
    			halt_ret_label:					\n\
    			leave							\n\
    			ret"							
    			: 
    			:
    			: "memory"
    			);
	//printf("ebx in exe %d\n", ebx);

	return 0;
}
/*
* int32_t read (int32_t fd, void* buf, int32_t nbytes)
*   Inputs: int32_t fd, void* buf, int32_t nbytes
*   Outputs: none
*	Read data from keyboard, file, RTC, or directory.  Return number of bytes read.  Different cases
*	are handled differently; refer to Appendix B.  Use jump table.
*/
int32_t read (int32_t fd, void* buf, int32_t nbytes){
	//if(pid%3!=0) return -1;
	uint32_t flags; // this fixed the halt prolly
	cli_and_save(flags);
	if(fd<0||fd>(FDTABLE_SIZE-1)) return -1;
	//printf("read called- pid: %d fd: %d\n",pid,fd);
	pcb_t* pcb_addr = PCB_LOCATION;

	if(pcb_addr->file_desc_table[fd].can_read &&pcb_addr->file_desc_table[fd].in_use ){
		restore_flags(flags);
		return pcb_addr->file_desc_table[fd].file_ops->read(fd,buf,nbytes, (int32_t*)&(pcb_addr->file_desc_table[fd].pos));
	}
	else{
		restore_flags(flags);
		return -1;
	}
}
/*
* int32_t write (int32_t fd, const void* buf, int32_t nbytes)
*   Inputs: int32_t fd, const void* buf, int32_t nbytes
*   Outputs: none
*	Writes data to the terminal or to a RTC
*/
int32_t write (int32_t fd, const void* buf, int32_t nbytes){
	//printf("write called- pid: %d buf: %s\n",pid,buf);
	if(fd<0||fd>(FDTABLE_SIZE-1)) return -1;
	pcb_t* pcb_addr = PCB_LOCATION;
	if(pcb_addr->file_desc_table[fd].can_write &&pcb_addr->file_desc_table[fd].in_use )
		return pcb_addr->file_desc_table[fd].file_ops->write(fd,buf,nbytes);
	else return -1;
}
/*
* int32_t open (const uint8_t* filename)
*   Inputs: const uint8_t* filename
*   Outputs: none
*	Provides access to file system.  Should find directory entry corresponding to named file, allocate unused fd,
*	and set up data needed to handle types of data.  Return -1 if file not found.
*/
int32_t open (const uint8_t* filename){
	//printf("open called- pid: %d filename: %s\n",pid, filename);
	uint32_t flags; // this fixed the halt prolly
	cli_and_save(flags);
	int32_t ret;
	if(!strncmp((int8_t*) filename,"stdin",5)){
		pcb_t* pcb_addr = PCB_LOCATION;
		file_ops_t ops={
			.read = &terminal_read,
			.open = &terminal_open,
			.write = &terminal_write,
			.close = &terminal_close,
		};
		pcb_addr->file_ops_table[0]=ops;
		file_desc_t desc={
			.file_ops = &(pcb_addr->file_ops_table[0]),
			.inode = 0,
			.pos = 0,
			.flags = 0,
			.in_use=1,
			.can_read=1,
			.can_write=0,
		};
		pcb_addr->file_desc_table[0] = desc;
		ret = 0;
	}else if(!strncmp((int8_t*) filename,"stdout",6)){
		pcb_t* pcb_addr =PCB_LOCATION;
		file_ops_t ops={
			.read = &terminal_read,
			.open = &terminal_open,
			.write = &terminal_write,
			.close = &terminal_close,
		};
		pcb_addr->file_ops_table[1]=ops;
		file_desc_t desc={
			.file_ops = &(pcb_addr->file_ops_table[1]),
			.inode = 0,
			.pos = 0,
			.flags = 0,
			.in_use=1,
			.can_read=0,
			.can_write=1,
		};
		pcb_addr->file_desc_table[1] = desc;
		ret = 1;
	}else{
		pcb_t* pcb_addr = PCB_LOCATION;
		dentry_t dentry;
		if(read_dentry_by_name(filename,&dentry)==-1){
			return -1;
		}
		file_ops_t ops;
		file_desc_t desc={
			.file_ops = 0,
			.inode = dentry.inode,
			.pos = 0,
			.flags = 0,
			.in_use=1,
			.can_read=1,
			.can_write=0,
		};
		if(dentry.file_type==0){
			ops.read = &rtc_read;
			ops.open = &rtc_open;
			ops.write = &rtc_write;
			ops.close = &rtc_close;
			desc.can_write=1;

		}
		else if(dentry.file_type==1){
			ops.read = &read_directory;
			ops.open = &file_open;
			ops.write = &file_write;
			ops.close = &file_close;
		}else if(dentry.file_type==2){
			ops.read = &file_read;
			ops.open = &file_open;
			ops.write = &file_write;
			ops.close = &file_close;
		}

		int i;
		for(i=2;i<FDTABLE_SIZE;i++){
			if(pcb_addr->file_desc_table[i].in_use==0)
				break;
		}
		if(i>=FDTABLE_SIZE){
			restore_flags(flags);
			return -1;
		}
		pcb_addr->file_ops_table[i]=ops;
		desc.file_ops=&(pcb_addr->file_ops_table[i]);
		pcb_addr->file_desc_table[i]=desc;
		ret = i;
	}
	restore_flags(flags);
	return ret;
}
/*
* int32_t close (int32_t fd)
*   Inputs: int32_t fd
*   Outputs: none
*	Closes specified file descriptor, makes it available for return from later calls to open.
*/
int32_t close (int32_t fd){
	uint32_t flags; // this fixed the halt prolly
	cli_and_save(flags);
	if(fd<2||fd>7) return -1;
	pcb_t* pcb_addr = PCB_LOCATION;
	if(pcb_addr->file_desc_table[fd].in_use==1){
		pcb_addr->file_desc_table[fd].in_use=0;
		restore_flags(flags);
		return 0;
	}
	else{
		restore_flags(flags);
		return -1;
	}
		
}
/*
* int32_t getargs (uint8_t* buf, int32_t nbytes)
*   Inputs: nbytes - length of arg
*   Outputs: none
*	Reads program's command line arguments into a user-level buffer, copied into user space.
*/
int32_t getargs (uint8_t* buf, int32_t nbytes){
	//printf("getargs called\n");
	uint32_t flags; // this fixed the halt prolly
	cli_and_save(flags);
	if((uint32_t) buf< VIRTUAL_ADDR || (uint32_t) buf > PAGE_4MB +VIRTUAL_ADDR) return -1;
	pcb_t* pcb_addr = PCB_LOCATION;
	if(strlen(pcb_addr->args)>=nbytes) return -1;
	memcpy(buf, pcb_addr->args, nbytes>ARGLEN?ARGLEN:nbytes);
	restore_flags(flags);
	return 0;
}
/*
* int32_t vidmap (uint8_t** screen_start)
*   Inputs: uint8_t** screen_start
*   Outputs: none
*	Maps a 4mb page, sets cr3 to pdbr and then maps the virtual address
*/
int32_t vidmap (uint8_t** screen_start){
	if((uint32_t)screen_start<VIRTUAL_ADDR||(uint32_t)screen_start>=VIRTUAL_ADDR+PAGE_4MB) return -1; // error checking
	*screen_start=(uint8_t*)(PHYSICAL_ADDR+FOUR_KB*PCB_LOCATION->terminal_num);
	return 0;
}
/*
* int32_t set_handler (int32_t signum, void* handler_address)
*   Inputs: int32_t signum, void* handler_address
*   Outputs: none
*	Set handler just returns
*/
int32_t set_handler (int32_t signum, void* handler_address){
	int32_t ret;
	return ret;
}
/*
* int32_t sigreturn (void)
*   Inputs: none
*   Outputs: none
*	Sigreturn just returns
*/
int32_t sigreturn (void){
	int32_t ret;
	return ret;
}
/*
* int32_t parse_args(int8_t* input_buf, int8_t* command_buf, int8_t* args_buf)
*   Inputs: int8_t* input_buf, int8_t* command_buf, int8_t* args_buf
*   Outputs: none
*	Parse args; place input buffer into comand buffer until NULL char, then get the rest of the input buffer into the arg buffer
*	Added error checking for NULL
*/
int32_t parse_args(int8_t* input_buf, int8_t* command_buf, int8_t* args_buf){
	uint32_t flags; // this fixed the halt prolly
	cli_and_save(flags);
	int i=0;
	int j=0;
	while(input_buf[i]!='\0'&&input_buf[i]!=' '){
		command_buf[i]=input_buf[i];
		i++;
	}
	command_buf[i]='\0';
	if(input_buf[i]=='\0'){
		args_buf[0]='\0';
		restore_flags(flags);
		return 0;
	}
	i++;
	while(input_buf[i]!='\0'){
		args_buf[j]=input_buf[i];
		i++;
		j++;
	}
	args_buf[j]='\0';
	restore_flags(flags);
	return 0;
}

/*
* int32_t find_pid();
*   Inputs: void
*   Outputs: none
*	find pid looks for an empty pid, loops through amount of pids, returns 
*	has to be + NUM SHELLS b/c the first few pids are associated with the shells. set to 1.
*/
int32_t find_pid(){
	int i;
	for(i=0;i<PIDS_SIZE;i++){
		if(!pids[i]){
			pids[i]=1;
			return i+NUM_SHELLS;
		}
	}
	return -1;
}
/*
* int32_t free_pid(uint32_t index);
*   Inputs: void
*   Outputs: none
*	Finds the right pid, remember to subtract the NUM_SHELLS to get to the right pid, sets to 0
*/
void free_pid(uint32_t index){
	pids[index-NUM_SHELLS]=0;
}

