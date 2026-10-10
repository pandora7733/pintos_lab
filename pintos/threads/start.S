#include "threads/loader.h"
#define LONG_MODE (1 << 29)
#define CR0_PE 0x00000001
#define CR0_PG (1 << 31)
#define CR4_PAE 0x20
#define PTE_P 0x1
#define PTE_W 0x2
#define EFER_MSR 0xC0000080
#define EFER_LME (1 << 8)
#define EFER_SCE (1 << 0)
#define RELOC(x) (x - LOADER_KERN_BASE)
.section .entry

.globl _start
_start = RELOC(bootstrap)

.globl bootstrap
.func bootstrap
.code32
#### 64비트 코드로 부트스트랩한다.
bootstrap:
	pushf
	pop %eax
	mov %ecx, %eax
	xor $0x200000, %eax
	push %eax
	popf
	cmp %eax, %ebx
	jz no_long_mode # cpuid 명령어가 존재하는지 확인한다.
	xor %eax, %eax
	cpuid           # cpuid 1을 질의한다.
	cmp $1, %eax
	jb no_long_mode
	test $LONG_MODE, %edx
#### 물리 주소 확장(PAE)을 활성화한다
	movl %cr4, %eax
	orl $CR4_PAE, %eax
	movl %eax, %cr4


#### 페이지 디렉터리와 페이지 테이블을 만들고
#### 페이지 디렉터리 베이스 레지스터(cr3)를 설정한다.
setup_page_table:
# 1. boot_pml4e를 0으로 채운다
  lea (RELOC(boot_pml4e)), %edi
	xor %eax, %eax
	mov $0x400, %ecx
	rep stosl (%edi)

# 2. pdpt들을 설정한다
  lea (RELOC(boot_pml4e)), %edi
	lea (RELOC(boot_pdpt1)), %ebx
	orl $(PTE_P | PTE_W), %ebx
	mov %ebx, (%edi) # pdpt1
	lea (RELOC(boot_pdpt2)), %ebx
	orl $(PTE_P | PTE_W), %ebx
	mov %ebx, 8(%edi) # pdpt2

# 3. pdpe들을 설정한다
  lea (RELOC(boot_pdpt1)), %edi
	lea (RELOC(boot_pde1)), %ebx
	orl $(PTE_P | PTE_W), %ebx
	mov %ebx, (%edi)

  lea (RELOC(boot_pdpt2)), %edi
	lea (RELOC(boot_pde2)), %ebx
	orl $(PTE_P | PTE_W), %ebx
	mov %ebx, (%edi)

# 4. pde들을 설정한다
  mov $128, %ecx
	lea (RELOC(boot_pde1)), %ebx
	lea (RELOC(boot_pde2)), %edx
	add $256, %edx
	mov $(PTE_P | PTE_W | 0x180), %eax

fill_pdes:
	mov %eax, (%ebx)
	mov %eax, (%edx)
	add $8, %ebx
	add $8, %edx
	add $0x200000, %eax
	dec %ecx
	cmp $0, %ecx
	jne fill_pdes

# 5. 페이지 디렉터리 베이스 레지스터(cr3)를 로드한다.
	lea (RELOC(boot_pml4e)), %eax
	mov %eax, %cr3

#### MSR(Model Specific Register)을 사용해 롱 모드를 활성화한다
#### syscall을 활성화한다 (EFER_SCE)
	mov $EFER_MSR, %ecx
	rdmsr
	orl $(EFER_LME | EFER_SCE), %eax
	wrmsr

#### 페이징을 활성화한다
	mov %cr0, %eax
	or $(CR0_PE|CR0_PG), %eax
	mov %eax, %cr0

#### 롱 모드로 점프한다
	lea (RELOC(gdt_desc64)), %eax
	lgdt (%eax)
	mov $(entry_64 - LOADER_KERN_BASE), %eax
	push $SEL_KCSEG
	push %eax
	lret
.endfunc

no_long_mode:
	jmp no_long_mode

.p2align 2
gdt64:
  .quad 0                   # 널 세그먼트
  .quad 0x00af9a000000ffff  # 64비트 코드 세그먼트
  .quad 0x00af92000000ffff  # 64비트 데이터 세그먼트
gdt_desc64:
  .word 0x17
  .quad RELOC(gdt64)

.p2align 12
.globl boot_pml4e
.globl boot_pdpt1
.globl boot_pdpt2
.globl boot_pde1
.globl boot_pde2

boot_pml4e:
  .space  0x1000
boot_pdpt1:
  .space  0x1000
boot_pdpt2:
  .space  0x1000
boot_pde1:
  .space  0x1000
boot_pde2:
  .space  0x1000

.section .text
.code64
.globl entry_64
.func entry_64
entry_64:
	#### 0 ~ 0x1000을 부트 스택으로 사용한다.
	xor %rbp, %rbp
	movabs $(LOADER_KERN_BASE + 0x1000), %rsp
	movabs $main, %rax
	call *%rax
.endfunc
