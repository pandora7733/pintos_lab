/* 이 파일은 MIT 6.828 강의에서 사용된 소스 코드에서
   파생되었다. 원본 저작권 고지는 아래에 그대로
   수록되어 있다. */

/*
 * Copyright (C) 1997 Massachusetts Institute of Technology 
 *
 * This software is being provided by the copyright holders under the
 * following license. By obtaining, using and/or copying this software,
 * you agree that you have read, understood, and will comply with the
 * following terms and conditions:
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose and without fee or royalty is
 * hereby granted, provided that the full text of this NOTICE appears on
 * ALL copies of the software and documentation or portions thereof,
 * including modifications, that you make.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS," AND COPYRIGHT HOLDERS MAKE NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED. BY WAY OF EXAMPLE,
 * BUT NOT LIMITATION, COPYRIGHT HOLDERS MAKE NO REPRESENTATIONS OR
 * WARRANTIES OF MERCHANTABILITY OR FITNESS FOR ANY PARTICULAR PURPOSE OR
 * THAT THE USE OF THE SOFTWARE OR DOCUMENTATION WILL NOT INFRINGE ANY
 * THIRD PARTY PATENTS, COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS. COPYRIGHT
 * HOLDERS WILL BEAR NO LIABILITY FOR ANY USE OF THIS SOFTWARE OR
 * DOCUMENTATION.
 *
 * The name and trademarks of copyright holders may NOT be used in
 * advertising or publicity pertaining to the software without specific,
 * written prior permission. Title to copyright in this software and any
 * associated documentation will at all times remain with copyright
 * holders. See the file AUTHORS which should have accompanied this software
 * for a list of all copyright holders.
 *
 * This file may be derived from previously copyrighted software. This
 * copyright applies only to those changes made by the copyright
 * holders listed in the AUTHORS file. The rest of this file is covered by
 * the copyright notices, if any, listed below.
 */

#include "threads/loader.h"
	
#### 커널 로더.

#### 이 코드는 하드 디스크의 첫 번째 섹터에 저장되어야 한다.
#### BIOS가 실행되면 이 코드를 물리 주소 0x7c00-0x7e00(512바이트)에
#### 로드한다. 그런 다음 리얼 모드에서 이 코드의 시작 부분으로
#### 점프한다. 이 코드는 모든 메모리에 접근할 수 있도록 보호 모드
#### (32비트 모드)로 전환하고, 커널을 메모리에 로드한 뒤, start.S가
#### 링크되어 있는 커널의 첫 바이트로 점프한다.
	
/* 제어 레지스터 0의 플래그. */
#define CR0_PE 0x00000001      /* Protection Enable. */
#define CR0_EM 0x00000004      /* (Floating-point) Emulation. */
#define CR0_PG 0x80000000      /* Paging. */
#define CR0_WP 0x00010000      /* Write-Protect enable in kernel mode. */


.globl start
start:
	
# 코드는 16비트 세그먼트인 리얼 모드에서 실행된다.
	.code16

# 인터럽트를 비활성화한다. 보호 모드에서 인터럽트를 처리할 준비는
# 훨씬 나중에야 되기 때문이다.
# 문자열 명령어가 위쪽(증가 방향)으로 진행하도록 한다 (예: 아래의 "rep stosl").

	cli
	cld

# 데이터 세그먼트를 설정한다.

	subw %ax, %ax
	movw %ax, %es
	movw %ax, %ds

# 스택 세그먼트를 설정한다.
# 스택은 우리 위치에서 시작해 아래쪽으로 자란다.
# 우리가 스택을 직접 쓰지는 않지만, 호출하는 BIOS는
# 사용할 수도 있다.

	movw %ax, %ss
	movw $0x7c00, %sp
	
#### A20을 활성화한다. 머신이 부팅될 때 주소 라인 20은 low로 고정되어
#### 있어서 1MB 이상의 메모리를 주소 지정할 수 없다. 이 코드가
#### 그 문제를 해결한다.
	
# 바쁜 동안 상태 레지스터를 폴링한다.

1:	inb $0x64, %al
	testb $0x2, %al
	jnz 1b

# 출력 포트 쓰기 명령을 보낸다.

	movb $0xd1, %al
	outb %al, $0x64

# 바쁜 동안 상태 레지스터를 폴링한다.

1:	inb $0x64, %al
	testb $0x2, %al
	jnz 1b

# A20 라인을 활성화한다.

	movb $0xdf, %al
	outb %al, $0x60


#### 인터럽트 15h의 e820h 기능으로 메모리 크기를 얻는다. 이제 pintos는
#### x86_64에서 실행되므로, "Get Extended Memory Size" 연산으로는
#### 우리 목적에 충분하지 않다.
	mov $0xe820, %eax  # 명령
	mov $(E820_MAP4), %edi  # 목적지
	xor %ebx, %ebx
	mov $0x534d4150, %edx # 매직
	mov $24, %ecx
	int $0x15
	cmp %eax, %edx
	test %ebx, %ebx
	je panic
	mov $24, %ebp

parse_e820:
	mov %ecx, -4(%edi)
	add $24, %edi
	mov $0xe820, %eax
	mov $24, %ecx
	int $0x15
	jc e820_parse_done
	add $24, %ebp
	test %ebx, %ebx
	jne parse_e820

e820_parse_done:
	mov %ecx, -4(%edi)
	movl $0x40, MULTIBOOT_FLAG
	movl %ebp, MULTIBOOT_MMAP_LEN
	movl $(E820_MAP), MULTIBOOT_MMAP_ADDR # e820

#### 보호 모드로 전환한다.

# 인터럽트는 여전히 꺼져 있음에 유의하라.

# GDTR이 우리의 GDT를 가리키게 한다. 보호 모드에는 GDT가 필요하다.
# GDT 디스크립터의 32비트가 모두 로드되도록 data32 접두어가
# 필요하다 (기본값은 24비트만 로드한다).

	data32 lgdt gdtdesc

# 그런 다음 CR0의 다음 비트들을 켠다:
#    PE (Protect Enable): 보호 모드를 켠다.
#    PG (Paging): 페이징을 켠다.
#    WP (Write Protect): 설정하지 않으면 링 0 코드는 페이지 테이블의
#       쓰기 보호 비트를 무시한다 (!).
#    EM (Emulation): 부동소수점 명령어가 트랩을 일으키도록 강제한다.
#       우리는 부동소수점을 지원하지 않는다.
	
	movl %cr0, %eax
	orl $CR0_PE, %eax
	movl %eax, %cr0
	
# 이제 16비트 세그먼트의 보호 모드에 있다. CPU는 여전히 리얼 모드
# 코드 세그먼트를 %cs의 세그먼트 디스크립터에 캐시하고 있다.
# %cs를 다시 로드해야 하는데, 가장 쉬운 방법은 far jump를 쓰는 것이다.
# 32비트 세그먼트가 아니므로 32비트 오프셋으로 점프하려면
# data32 접두어가 필요하다.

	data32 ljmp $SEL_KCSEG, $protcseg

# 이제 32비트 세그먼트의 보호 모드에 있다.

	.code32

# 나머지 모든 세그먼트 레지스터와 스택 포인터가 새 GDT를
# 가리키도록 다시 로드한다.

protcseg:
	movw $SEL_KDSEG, %ax
	movw %ax, %ds		
	movw %ax, %es		
	movw %ax, %fs		
	movw %ax, %gs		
	movw %ax, %ss
	movl $LOADER_PHYS_BASE + 0x30000, %esp

#### IDE 컨트롤러를 직접 조작하여 물리 주소 LOADER_PHYS_BASE부터
#### 커널을 로드한다.

	movl $1, %ebx
	movl $LOADER_PHYS_BASE, %edi

# 데이터를 폴링할 것이므로 IDE 컨트롤러의 인터럽트 전달을
# 비활성화한다.
# (이렇게 하지 않으면, 부팅 중에 인터럽트 컨트롤러를 리셋한 뒤에는
# IDE 컨트롤러도 리셋하더라도 Bochs 2.2.6이 이후 IDE 인터럽트를
# 전혀 전달하지 않는다.)

	movw $0x3f6, %dx
	movb $0x02, %al
	outb %al, %dx
	
read_sector:

# 컨트롤러가 바쁜 동안 상태 레지스터를 폴링한다.

	movl $0x1f7, %edx
1:	inb %dx, %al
	testb $0x80, %al
	jnz 1b

# 섹터 하나를 읽는다.

	movl $0x1f2, %edx
	movb $1, %al
	outb %al, %dx

# 하위 28비트에 쓸 섹터 번호.
# 상위 4비트에 LBA 모드, 장치 0.

	movl %ebx, %eax
	andl $0x0fffffff, %eax
	orl $0xe0000000, %eax

# %eax를 포트 0x1f3...0x1f6에 쓴다.

	movl $4, %ecx
1:	incw %dx
	outb %al, %dx
	shrl $8, %eax
	loop 1b

# 명령 레지스터에 READ 명령을 쓴다.

	incw %dx
	movb $0x20, %al
	outb %al, %dx

# 컨트롤러가 바쁜 동안 상태 레지스터를 폴링한다.

1:	inb %dx, %al
	testb $0x80, %al
	jnz 1b

# 데이터가 준비될 때까지 상태 레지스터를 폴링한다.

1:	inb %dx, %al
	testb $0x08, %al
	jz 1b

# 섹터를 전송한다.

	movl $256, %ecx
	movl $0x1f0, %edx
	rep insw

# 다음 섹터.

	incl %ebx
	cmpl $KERNEL_LOAD_PAGES*8 + 1, %ebx
	jnz read_sector

#### 커널 진입점으로 점프한다.
	movl $LOADER_PHYS_BASE, %eax
	call *%eax
	jmp panic

#### GDT

gdt:
	.quad 0x0000000000000000	# 널 세그먼트
	.quad 0x00cf9a000000ffff	# 코드 세그먼트
	.quad 0x00cf92000000ffff  # 데이터 세그먼트
	
gdtdesc:
	.word	0x17			# sizeof (gdt) - 1
	.long	gdt

#### 치명적 오류.
#### (BIOS의 도움을 받아) panic_message를 출력하고 무한 루프를 돈다.

panic:  .code16			# 패닉은 리얼 모드에서만 일어난다.
	movw $panic_message, %si
	movb $0xe, %ah
	subb %bh, %bh
1:	lodsb
	test %al, %al
2:	jz 2b			# 무한 루프.
	int $0x10
	jmp 1b

panic_message:
	.ascii "Panic!"
	.byte 0

#### 커맨드 라인 인자와 그 개수.
#### `pintos' 유틸리티가 기록하고 커널이 읽는다.
#### 로더 자체는 커맨드 라인으로 아무것도 하지 않는다.
	.org LOADER_ARG_CNT - LOADER_BASE
arg_cnt:
	.long 0
	.org LOADER_ARGS - LOADER_BASE
args:
	.fill 0x80, 1, 0

#### 부트 섹터 시그니처.
#### BIOS는 이 값이 올바르게 설정되어 있는지 확인한다.
	.org LOADER_SIG - LOADER_BASE
	.word 0xaa55
