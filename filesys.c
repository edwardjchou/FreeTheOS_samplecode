#include "filesys.h"
static int8_t* filesys_ptr;
/*
 * void init_filesys(uint32_t filesys_addr)
 * File open, does nothing
 *
 * Inputs: none
 * Outputs: none
 */
void init_filesys(uint32_t filesys_addr){
	filesys_ptr=(int8_t*) filesys_addr;
}
/*
 * int32_t file_read(int32_t fd, int8_t* buf, int32_t length, int32_t* pos_ptr)
 * File open, does nothing
 *
 * Inputs: none
 * Outputs: none
 */
int32_t file_read(int32_t fd, int8_t* buf, int32_t length, int32_t* pos_ptr){
	pcb_t* pcb_addr = PCB_LOCATION;
	int32_t inode = pcb_addr->file_desc_table[fd].inode;
	int32_t offset = *pos_ptr;
	uint32_t size=read_data((uint32_t)inode,(uint32_t)offset,(uint8_t*)buf,(uint32_t)length);
	*pos_ptr+=size;
	return size;
}
/*
 * int32_t read_dentry_by_name(const uint8_t* fname, dentry_t* dentry)
 *
 * Inputs: fname - the name of the file, dentry - a dentry struct
 * Outputs: 0 on success, -1 on failure
 * If the file can't be found, return -1.  Else, fill in the dentry block passed in as the second argument
 * with the file name, file type, and inode number for the file, and then return 0 (indicating end of file has been reached).
 */
int32_t read_dentry_by_name(const uint8_t* fname, dentry_t* dentry){
	int8_t* file_ptr=filesys_ptr;
	uint32_t num_dir=*((uint32_t*)file_ptr);
	int i;

	file_ptr=file_ptr+ENTRY_SIZE; // first block is the boot block, go down 4+4+4+52 (64) to get to directory entries
	for(i=0;i<num_dir;i++){ // go through the entries
		if(!(strncmp(file_ptr,(int8_t*)fname,FILE_NAME-1))){ // if name is found in directory
			memcpy(dentry,file_ptr,(FILE_NAME + FOUR_BYTE*2)); //copy to dentry passed in arg
			return 0; //success
		}
		file_ptr=file_ptr+ENTRY_SIZE; // increment by entry
	}
	
	return -1; //else fail
}
/*
 * int32_t read_dentry_by_index(uint8_t* index, dentry_t* dentry)
 *
 * Inputs: index - the index of the file, dentry - a dentry struct
 * Outputs: 0 on success, -1 on failure
 * If the file can't be found, return -1.  Else, fill in the dentry block passed in as the second argument
 * with the file name, file type, and inode number for the file, and then return 0 (indicating end of file has been reached).
 */
int32_t read_dentry_by_index(uint32_t index, dentry_t* dentry){
	int8_t* file_ptr=filesys_ptr;
	uint32_t num_dir=*((uint32_t*)file_ptr);
	if(index>=num_dir) return -1; // if exceeds the boundaries, fail
	file_ptr=file_ptr+ENTRY_SIZE*(index+1); //else go down the directory by the index, go down 64 again to get to directory entries
	memcpy(dentry,file_ptr,(FILE_NAME + FOUR_BYTE*2)); //copy to dentry block
	return 0; //success
}
/*
 * int32_t read_data(uint32_t inode, uint32_t offset, uint8_t* buf, uint32_t length){
 *
 * Inputs: 
 *	inode - the index node of the file
 *	offset - the offset within the file
 *	buf - the buffer to copy too
 *	length - the length of memory to copy
 * Outputs: length - the length read, either the length actually read or the length of the file
 *	Goes to the index, find the length of the file in bytes, cap off the length to read by the length of the file.  Go to the first block within the index, 
 * find the offset % by 4096 within the block, and find the correct block.  Copy either the length left in the file or the remainder of the block.  If there 
 * is memory left to read, find the next block to read in the index and repeat.
 */
int32_t read_data(uint32_t inode, uint32_t offset, uint8_t* buf, uint32_t length){
	int8_t* file_ptr=filesys_ptr;

	int i,counter;
	counter=length;
	uint32_t num_inodes=*((uint32_t*)(file_ptr+FOUR_BYTE)); //32 bytes
	uint32_t num_blocks=*((uint32_t*)(file_ptr+FOUR_BYTE*2));
	if(inode>=num_inodes) return 0; // if the index node is greater than the number of indexes, fail
	uint8_t* buf_ptr=buf; 
	int8_t* inode_ptr=file_ptr + SYSTEM_BLOCK * (inode+1); // get to the right inode * 4096
	int8_t* block_ptr;
	uint8_t start_off=offset%SYSTEM_BLOCK;
	// if length (you want to read) + offset(in file) > length of file in bytes,  counter = length = length in bytes (cap off), else just read the length
	if(length+offset>*((uint32_t*)inode_ptr)) counter=length=*((uint32_t*)inode_ptr)-offset; 
	/*if((offset+length)/4096>=num_blocks)
		counter=length=num_blocks*4096-offset;
	}*/
	for(i=offset/SYSTEM_BLOCK;i<=(offset+length)/SYSTEM_BLOCK;i++){
		block_ptr=file_ptr+(1+num_inodes+*((uint32_t*)(inode_ptr+FOUR_BYTE+i*FOUR_BYTE)))*SYSTEM_BLOCK; // get to the right block * 4096
		if(((block_ptr-file_ptr)/SYSTEM_BLOCK)-num_inodes-1>num_blocks||(block_ptr-file_ptr)/SYSTEM_BLOCK-num_inodes-1<0) // if block number exceeds number of blocks or is less than zero
			return 0; // fail 
		int temp=SYSTEM_BLOCK-start_off < counter?SYSTEM_BLOCK-start_off:counter; //if the remaining data in block is less than amount to read, temp = remaining data, else temp = amount to read (counter)
		memcpy(buf_ptr, block_ptr+start_off, temp); // copy the data block + the start off offset, with the length of the amount read
		buf_ptr+=temp; //increment the buffer pointer by the amount read
		counter-=temp; // decrement the amount needed to read by the amount read
		start_off=0; // reset the within block offset to zero
		
	}
	return length;
}
/*
 * int32_t read_directory(uint8_t* buf, uint32_t length)
 *
 * Basically the ls function, lists the file names within the directory.
 * Inputs: buf - the buffer to copy to, length - the length of the buffer
 * Outputs: none
 */
int32_t read_directory(int32_t fd, int8_t* buf, int32_t length, int32_t* pos_ptr){
	int8_t* file_ptr=filesys_ptr;
	int num_dir = *((uint32_t*)file_ptr);
	if(*pos_ptr>=num_dir) return 0;
	int j;
		//memcpy(file_ptr+entry_size*(i+1),buf+file_name*i,32);
	for(j=0;j<32;j++){ //max length of filename is 32
		if(*(file_ptr+ENTRY_SIZE*(*pos_ptr+1)+j)=='\0'||j==length) break; //if the name is less than 32 (null terminated), break
		else{
			//printf("%c",*(file_ptr+entry_size*(i+1)+j));
			buf[j]=*(file_ptr+ENTRY_SIZE*(*pos_ptr+1)+j); //copy the string name one by one
		}
	} // end each name with new line
	int i=j;
	
	while(j<length-1){
		j++;
		buf[j]='\0';
	}
	(*pos_ptr)++;
	return i;
}

/*
 * int32_t file_open()
 * File open, does nothing
 *
 * Inputs: none
 * Outputs: none
 */
int32_t file_open(){
	return 0;
}

/*
 * int32_t file_write()
 * File write, return -1
 *
 * Inputs: none
 * Outputs: none
 */
int32_t file_write(int32_t fd, const int8_t* buf, int32_t length){
	return -1;
}


/*
 * int32_t file_close()
 * File close, does nothing
 *
 * Inputs: none
 * Outputs: none
 */
int32_t file_close(){
	return 0;
}

/*
 * int32_t dir_open()
 * Direcotry open, does nothing
 *
 * Inputs: none
 * Outputs: none
 */
int32_t dir_open(){
	return 0;
}

/*
 * int32_t dir_write()
 * Directory write, return -1
 *
 * Inputs: none
 * Outputs: none
 */
int32_t dir_write(int32_t fd, const int8_t* buf, int32_t length){
	return -1;
}


/*
 * int32_t dir_close()
 * Directory close, return 0
 *
 * Inputs: none
 * Outputs: none
 */
int32_t dir_close(){
	return 0;
}
