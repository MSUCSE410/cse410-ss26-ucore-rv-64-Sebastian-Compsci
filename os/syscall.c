#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "stat.h"

uint64 console_write(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	tracef("write size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return len;
}

uint64 console_read(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	tracef("read size = %d", len);
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

/*
* LAB1: you may need to define sys_task_info here
*/
///reads current proc data
///copies task state, count, runtime, and read from current proc
//sys call counts, runtime, status
uint64 sys_task_info(TaskInfo *ti){
	struct proc *p = curr_proc();
	//user pointer to physical address
	uint64 pa = useraddr(p->pagetable, (uint64)ti);
	if(pa==0){
		return -1;
	}

	//update runtime in miliseconds
	uint64 now = get_cycle();
	p->taskinfo.time = (now-p->start_cycle)/(CPU_FREQ/1000);

	//copy kernel structure to memeory
	TaskInfo *pti = (TaskInfo *) pa;
	*pti = p->taskinfo;
	return 0;
}

uint64 sys_mmap(uint64 start, uint64 len, int port, int flags, int fd){
	struct proc *p = curr_proc();

	if(start%PGSIZE != 0){
		return -1;
	}
	// if(len%PGSIZE != 0){
	// 	return -1;
	// }

	//1gb max and not 0
	if(len == 0 || len > (1UL << 30)){
		return -1;
	}
	//checks only RWX bits allowed
	if((port & ~0x7) != 0){
		return -1;
	}

	//permissions check, must have at least one
	if ((port & 0x7) == 0){
		return -1;
	}
	//rounding for pg bounds
	uint64 va = PGROUNDDOWN(start);
	uint64 end = PGROUNDUP(start + len);

	///check all pages unmapped
	for(uint64 i = va; i < end; i+=PGSIZE){
		///walkaddr checks if the page is mapped in the page table
		if(walkaddr(p->pagetable, i) != 0){
			return -1;
		}
	}
	for( ; va<end; va+=PGSIZE){

		//check if mapped
		if(walkaddr(p->pagetable, va) != 0){
			return -1;
		}

		//physical page allocation
		void *pa = kalloc();
		if (pa == 0){
			return -1;
		}

		///zero memeory out
		memset(pa, 0, PGSIZE);

		//convert port to PDE flags
		int perm = PTE_U; ///user processes
		//or used to set bits
		if (port & 1){
			perm |= PTE_R; //read
		}
		if(port & 2){
			perm |= PTE_W; //write
		}
		if(port & 4){
			perm |= PTE_X; //execute
		}

		//maps virtual addresses to physical addresses
		if(mappages(p->pagetable, va, PGSIZE, (uint64)pa, perm) != 0){
			kfree(pa); //cleans if failed
			return -1;
		}
	}

	return 0;
}

//removing mapping from pg table and freeing physical memory
uint64 sys_munmap(uint64 start, uint64 len){
	struct proc *p = curr_proc();
	if(len==0){
		return 0;
	}

	//page aligned only
	if(start%PGSIZE != 0){
		return -1;
	}
	if(len%PGSIZE != 0){
		return -1;
	}

	uint64 va = start;
	uint64 end = start+len;


	//checks that all pages exist
	for(uint64 i = va; i < end; i+=PGSIZE){
		if(walkaddr(p->pagetable, i) == 0){
			return -1;
		}

	}

	uint64 npages = (end-va + PGSIZE-1)/PGSIZE; //# of pages
	//remove mappings and frees physical memory
	uvmunmap(p->pagetable, va, npages, 1);

	return 0;
}

uint64 sys_write(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_write(va, len);
	case FD_INODE:
		return inodewrite(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_read(va, len);
	case FD_INODE:
		return inoderead(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(uint64 val, int _tz)
{
	struct proc *p = curr_proc();
	uint64 cycle = get_cycle();
	TimeVal t;
	t.sec = cycle / CPU_FREQ;
	t.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	copyout(p->pagetable, val, (char *)&t, sizeof(TimeVal));
	return 0;
}

uint64 sys_getpid()
{
	return curr_proc()->pid;
}

uint64 sys_getppid()
{
	struct proc *p = curr_proc();
	return p->parent == NULL ? IDLE_PID : p->parent->pid;
}

uint64 sys_clone()
{
	debugf("fork!");
	return fork();
}

static inline uint64 fetchaddr(pagetable_t pagetable, uint64 va)
{
	uint64 *addr = (uint64 *)useraddr(pagetable, va);
	return *addr;
}

uint64 sys_exec(uint64 path, uint64 uargv)
{
	struct proc *p = curr_proc();
	char name[MAX_STR_LEN];
	copyinstr(p->pagetable, name, path, MAX_STR_LEN);
	uint64 arg;
	static char strpool[MAX_ARG_NUM][MAX_STR_LEN];
	char *argv[MAX_ARG_NUM];
	int i;
	for (i = 0; uargv && (arg = fetchaddr(p->pagetable, uargv));
	     uargv += sizeof(char *), i++) {
		copyinstr(p->pagetable, (char *)strpool[i], arg, MAX_STR_LEN);
		argv[i] = (char *)strpool[i];
	}
	argv[i] = NULL;
	return exec(name, (char **)argv);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

uint64 sys_spawn(uint64 va)
{
	// TODO: your job is to complete the sys call
	struct proc *p = curr_proc();
	char name[200];

	//copying filename from user
	if(copyinstr(p->pagetable, name, va, 200) < 0){
		return -1;
	}
	//getting program id
	//int id = get_id_by_name(name);
	// if(id < 0){
	// 	return -1;
	// }

	//alloc new process
	struct proc *np = allocproc();
	if(np==0){
		return -1;
	}
	np->parent = p;

	//loads program into new process
	//loader(id, np);

	//mark runnable
	np->state = RUNNABLE;
	//add_task(np);


	return np->pid;
}

uint64 sys_set_priority(long long prio)
{
	// TODO: your job is to complete the sys call
	if(prio < 2){ ///prioity must be >2
		return -1;
	}

	struct proc *p = curr_proc();
	///updates process priority
	p->priority = prio;
	//redoes pass value (depends on priority)
	//high priority gives smaller pass which gives slower stride growth
	p->pass = 65536/p->priority;
	return prio;
}

uint64 sys_openat(uint64 va, uint64 omode, uint64 _flags)
{
	struct proc *p = curr_proc();
	char path[200];
	copyinstr(p->pagetable, path, va, 200);
	return fileopen(path, omode);
}

uint64 sys_close(int fd)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d", fd);
		return -1;
	}
	fileclose(f);
	p->files[fd] = 0;
	return 0;
}

///returns file infomration like inode number, type, linkc count
int sys_fstat(int fd,uint64 stat){
	//TODO: your job is to complete the syscall
	struct proc *p = curr_proc();

	//validate file exists
	if(fd < 0 || fd >= FD_BUFFER_SIZE || p->files[fd] == 0){
		return -1;
	}

	//getting file structure from file table
	struct file *f = p->files[fd];
	//can only be regular inode files
	if(f->type != FD_INODE){
		return -1;
	}

	struct inode *ip = f->ip;

	//setting stat default struct
	struct Stat st;
	st.dev = 0;
	st.ino = ip->inum;
	st.nlink = ip->nlink;
	// st.mode = ip->type;

	///file type flags from description
	#define FILE (1 << 20)
	#define DIR (1 << 21)


	///file type from inode type
	if(ip->type == T_FILE){
		st.mode = FILE;
	}
	else if(ip->type == T_DIR){
		st.mode = DIR;
	}
	else{
		st.mode = 0;
	}

	//copy result back to user space
	if(copyout(p->pagetable, stat, (char *)&st, sizeof(st)) < 0){
		return -1;
	}


	return 0;
}

/// creates a new directory entry pointing to an existing node
///increments nlink count and adds a new entry to the directory
int sys_linkat(int olddirfd, uint64 oldpath, int newdirfd, uint64 newpath, uint64 flags){
	//TODO: your job is to complete the syscall
	struct proc *p = curr_proc();
	char oldpathchar[MAXPATH], newpathchar[MAXPATH];

	///copy file path from user space to kernel space
	if(copyinstr(p->pagetable, oldpathchar, oldpath, MAXPATH) < 0){
		return -1;
	}
	if(copyinstr(p->pagetable, newpathchar, newpath, MAXPATH) < 0){
		return -1;
	}

	///find inode of existing file
	struct inode *ip = namei(oldpathchar);
	if(ip == 0){
		return -1;
	}
	///new directory entry for inode so increase nlink count
	ip->nlink++;
	iupdate(ip); //persists change to disk

	
	struct inode *dp = root_dir();
	//adds new directory entry for inode
	if (dirlink(dp, newpathchar, ip->inum) < 0){
		//rollback if failed
		ip->nlink--;
		iupdate(ip);
		iput(ip);
		iput(dp);
		return -1;
	}
	///releasign references
	iput(ip);
	iput(dp);
	return 0;
}

///removes a directory entry and decrements the inode's nlink
//will free data blocks of inode goes to 0
int sys_unlinkat(int dirfd, uint64 name, uint64 flags){
	//TODO: your job is to complete the syscall
	struct proc *p = curr_proc();
	char path[MAXPATH];

	///copy path
	if(copyinstr(p->pagetable, path, name, MAXPATH) < 0){
		return -1;
	}

	struct inode *dp = root_dir();

	///make sure inode loaded from disk
	ivalid(dp);
	struct dirent de;
	uint off;

	//search dict for matching filename
	for(off = 0; off < dp->size; off+=sizeof(de)){
		if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)){
			return -1;
		}
		if(de.inum == 0){
			continue;
		}
		///match condition
		if(strncmp(path, de.name, DIRSIZ) == 0){
			struct inode *ip = namei(path);
			if(ip==0){
				iput(dp);
				return -1;
			}
			///lock inode before changes
			ivalid(ip);

			//decrement and clear file entry
			ip->nlink--;
			de.inum = 0;
			if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)){
				return -1;
			}
			///write updated inode to disk
			iupdate(ip);

			//release inode ref and directory inode
			iput(ip);
			iput(dp);
	
			return 0;
		}
	}
	iput(dp);
	return -1;
}

extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_openat:
		ret = sys_openat(args[0], args[1], args[2]);
		break;
	case SYS_close:
		ret = sys_close(args[0]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday(args[0], args[1]);
		break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_getppid:
		ret = sys_getppid();
		break;
	case SYS_clone: // SYS_fork
		ret = sys_clone();
		break;
	case SYS_execve:
		ret = sys_exec(args[0], args[1]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_fstat:
	    ret = sys_fstat(args[0],args[1]);
		break;
	case SYS_linkat:
	    ret = sys_linkat(args[0],args[1],args[2],args[3],args[4]);
		break;
	case SYS_unlinkat:
	    ret = sys_unlinkat(args[0],args[1],args[2]);
		break;
	case SYS_spawn:
		ret = sys_spawn(args[0]);
		break;
	case SYS_setpriority:
		ret = sys_set_priority(args[0]);
		break;

	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	///calls func for sys call info
	case SYS_task_info:
		ret = sys_task_info((TaskInfo*) args[0]);
		break;
	case SYS_mmap:
		ret = sys_mmap(args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap(args[0], args[1]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
