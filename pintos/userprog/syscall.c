#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {

	// rax = 시스템 콜 번호
	// rip; 돌아갈 주소
    // cs; 돌아갈 모드
    // eflags;
    // rsp; 유저 스택 위치

	// gp_register
	// 인수는 %rdi, %rsi, %rdx, %r10, %r8, %r9 순서로 전달
	// rcx는 다른 곳에 쓰임

	// 반환 값은 RAX 레지스터에 저장하는 것이 관례입니다. 
	// 값을 반환하는 시스템 호출은 해당 rax 레지스터의 멤버를 수정하여 값을 반환할 수 있습니다
	// struct intr_frame

// 입구 코드 (syscall-entry.S)

// 유저 rsp를 잠깐 보관하고, TSS에서 커널 스택 위치를 꺼내 rsp를 바꿉니다.
// 커널 스택에 차례로 저장합니다. 이 묶음이 intr_frame입니다.
// 돌아갈 정보: rip(rcx에 있던 값), cs, rflags(r11에 있던 값), 유저 rsp, ss
// 일반 레지스터 전부 → R
// 그 묶음의 주소를 인자로 syscall_handler(f)를 부릅니다.

// syscall-entry.S에서 호출함
// movabs $syscall_handler, %r12

	// TODO: Your implementation goes here.
	switch(f->R.rax){
		case SYS_HALT: // 민구
			halt();
			break;
		case SYS_EXIT:
			exit((int) f->R.rdi);
			break;
		case SYS_FORK:
			break;
		case SYS_EXEC:
			break;
		case SYS_WAIT: // 민구
			// wait((pid_t) f->R.rdi);
			break;
		case SYS_CREATE:
			break;
		case SYS_REMOVE:
			break;
		case SYS_OPEN:
			break;
		case SYS_FILESIZE:
			break;
		case SYS_READ:
				return read((int) f->R.rdi, (void *) f->R.rsi, (unsigned) f->R.rdx);
			break;
		case SYS_WRITE: // 민구
			break;
		case SYS_SEEK:
			break;
		case SYS_TELL:
			break;
		case SYS_CLOSE:
			break;
		// Project Part 3 - 4
		// case SYS_MMAP:
		// 	break;
		// case SYS_MUNMAP:
		// 	break;
		// case SYS_CHDIR:
		// 	break;
		// case SYS_MKDIR:
		// 	break;
		// case SYS_READDIR:
		// 	break;
		// case SYS_ISDIR:
		// 	break;
		// case SYS_INUMBER:
		// 	break;
		// case SYS_SYMLINK:
		// 	break;
		// case SYS_DUP2:
		// 	break;
		// case SYS_MOUNT:
		// 	break;
		// case SYS_UMOUNT:
		// 	break;
		default:
			break;

	}
	printf ("system call!\n");
	thread_exit ();
}

void 
halt(void){
	power_off();
}

void
exit(int status){

	thread_exit();

	return status;
}

// int 
// wait (pid_t pid){
// 	return ;
// }

int read(int fd, void *buffer, unsigned size){

	// 테스트 코드 (유저 쪽)
	// int fd = open("sample.txt");   // 커널이 3을 돌려줌
	// read(fd, buf, 10);             // 3을 다시 넘김 → 커널은 rdi에서 3을 꺼냄
	// close(fd);

	// fd는 그냥 f->R.rdi에 들어오는 숫자

	return file_read(fd, buffer, size);
}

int write (int fd, const void *buffer, unsigned size){
	switch (fd){
		case 0:
			return 0;
		case 1:
			return putbuf(buffer, size);
		case 2:
			return file_read(fd, buffer, size);
		default:
			break;
	}
	putbuf(buffer, size);
	return size;
}