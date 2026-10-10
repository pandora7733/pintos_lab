#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* inode를 식별한다. */
#define INODE_MAGIC 0x494e4f44

/* 디스크상의 inode.
 * 정확히 DISK_SECTOR_SIZE 바이트 길이여야 한다. */
struct inode_disk {
	disk_sector_t start;                /* 첫 번째 데이터 섹터. */
	off_t length;                       /* 바이트 단위 파일 크기. */
	unsigned magic;                     /* 매직 넘버. */
	uint32_t unused[125];               /* 사용하지 않음. */
};

/* SIZE 바이트 길이의 inode에 할당해야 할 섹터 수를
 * 반환한다. */
static inline size_t
bytes_to_sectors (off_t size) {
	return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* 메모리상의 inode. */
struct inode {
	struct list_elem elem;              /* inode 리스트의 원소. */
	disk_sector_t sector;               /* 디스크 위치의 섹터 번호. */
	int open_cnt;                       /* 이 inode를 연 횟수. */
	bool removed;                       /* 삭제되었으면 true, 아니면 false. */
	int deny_write_cnt;                 /* 0: 쓰기 가능, >0: 쓰기 거부. */
	struct inode_disk data;             /* inode 내용. */
};

/* INODE 안에서 바이트 오프셋 POS를 담고 있는 디스크 섹터를
 * 반환한다.
 * INODE에 오프셋 POS 위치의 바이트 데이터가 없으면 -1을
 * 반환한다. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) {
	ASSERT (inode != NULL);
	if (pos < inode->data.length)
		return inode->data.start + pos / DISK_SECTOR_SIZE;
	else
		return -1;
}

/* 열린 inode 리스트. 같은 inode를 두 번 열면 같은
 * `struct inode'를 반환하도록 하기 위함이다. */
static struct list open_inodes;

/* inode 모듈을 초기화한다. */
void
inode_init (void) {
	list_init (&open_inodes);
}

/* LENGTH 바이트의 데이터로 inode를 초기화하고,
 * 새 inode를 파일 시스템 디스크의 SECTOR 섹터에
 * 기록한다.
 * 성공하면 true를 반환한다.
 * 메모리나 디스크 할당에 실패하면 false를 반환한다. */
bool
inode_create (disk_sector_t sector, off_t length) {
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT (length >= 0);

	/* 이 단언(assertion)이 실패한다면 inode 구조체의 크기가 정확히
	 * 한 섹터가 아닌 것이므로, 이를 고쳐야 한다. */
	ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

	disk_inode = calloc (1, sizeof *disk_inode);
	if (disk_inode != NULL) {
		size_t sectors = bytes_to_sectors (length);
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;
		if (free_map_allocate (sectors, &disk_inode->start)) {
			disk_write (filesys_disk, sector, disk_inode);
			if (sectors > 0) {
				static char zeros[DISK_SECTOR_SIZE];
				size_t i;

				for (i = 0; i < sectors; i++) 
					disk_write (filesys_disk, disk_inode->start + i, zeros); 
			}
			success = true; 
		} 
		free (disk_inode);
	}
	return success;
}

/* SECTOR에서 inode를 읽어
 * 그것을 담은 `struct inode'를 반환한다.
 * 메모리 할당에 실패하면 널 포인터를 반환한다. */
struct inode *
inode_open (disk_sector_t sector) {
	struct list_elem *e;
	struct inode *inode;

	/* 이 inode가 이미 열려 있는지 확인한다. */
	for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
			e = list_next (e)) {
		inode = list_entry (e, struct inode, elem);
		if (inode->sector == sector) {
			inode_reopen (inode);
			return inode; 
		}
	}

	/* 메모리 할당. */
	inode = malloc (sizeof *inode);
	if (inode == NULL)
		return NULL;

	/* 초기화. */
	list_push_front (&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	disk_read (filesys_disk, inode->sector, &inode->data);
	return inode;
}

/* INODE를 다시 열어 반환한다. */
struct inode *
inode_reopen (struct inode *inode) {
	if (inode != NULL)
		inode->open_cnt++;
	return inode;
}

/* INODE의 inode 번호를 반환한다. */
disk_sector_t
inode_get_inumber (const struct inode *inode) {
	return inode->sector;
}

/* INODE를 닫고 디스크에 기록한다.
 * INODE에 대한 마지막 참조였다면 메모리를 해제한다.
 * INODE가 삭제된 inode이기도 하다면 블록들도 해제한다. */
void
inode_close (struct inode *inode) {
	/* 널 포인터는 무시한다. */
	if (inode == NULL)
		return;

	/* 마지막으로 연 쪽이었다면 자원을 해제한다. */
	if (--inode->open_cnt == 0) {
		/* inode 리스트에서 제거하고 락을 해제한다. */
		list_remove (&inode->elem);

		/* 삭제되었다면 블록들을 해제한다. */
		if (inode->removed) {
			free_map_release (inode->sector, 1);
			free_map_release (inode->data.start,
					bytes_to_sectors (inode->data.length)); 
		}

		free (inode); 
	}
}

/* INODE를 열고 있는 마지막 호출자가 닫을 때 삭제되도록
 * 표시한다. */
void
inode_remove (struct inode *inode) {
	ASSERT (inode != NULL);
	inode->removed = true;
}

/* INODE의 OFFSET 위치부터 SIZE 바이트를 읽어 BUFFER에 저장한다.
 * 실제로 읽은 바이트 수를 반환하며, 오류가 발생하거나 파일 끝에
 * 도달하면 SIZE보다 작을 수 있다. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) {
	uint8_t *buffer = buffer_;
	off_t bytes_read = 0;
	uint8_t *bounce = NULL;

	while (size > 0) {
		/* 읽을 디스크 섹터, 섹터 안에서의 시작 바이트 오프셋. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* inode에 남은 바이트, 섹터에 남은 바이트, 그리고 둘 중 작은 값. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* 이 섹터에서 실제로 복사할 바이트 수. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* 섹터 전체를 호출자의 버퍼로 직접 읽는다. */
			disk_read (filesys_disk, sector_idx, buffer + bytes_read); 
		} else {
			/* 섹터를 바운스 버퍼로 읽은 뒤, 일부를
			 * 호출자의 버퍼로 복사한다. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}
			disk_read (filesys_disk, sector_idx, bounce);
			memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
		}

		/* 전진. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}
	free (bounce);

	return bytes_read;
}

/* BUFFER의 SIZE 바이트를 INODE의 OFFSET 위치부터 쓴다.
 * 실제로 쓴 바이트 수를 반환하며, 파일 끝에 도달하거나 오류가
 * 발생하면 SIZE보다 작을 수 있다.
 * (보통은 파일 끝에서 쓰면 inode가 확장되겠지만,
 * 확장은 아직 구현되지 않았다.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
		off_t offset) {
	const uint8_t *buffer = buffer_;
	off_t bytes_written = 0;
	uint8_t *bounce = NULL;

	if (inode->deny_write_cnt)
		return 0;

	while (size > 0) {
		/* 쓸 섹터, 섹터 안에서의 시작 바이트 오프셋. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* inode에 남은 바이트, 섹터에 남은 바이트, 그리고 둘 중 작은 값. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* 이 섹터에 실제로 쓸 바이트 수. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* 섹터 전체를 디스크에 직접 쓴다. */
			disk_write (filesys_disk, sector_idx, buffer + bytes_written); 
		} else {
			/* 바운스 버퍼가 필요하다. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}

			/* 쓰려는 덩어리의 앞이나 뒤에 섹터 데이터가 있다면
			   먼저 섹터를 읽어 와야 한다. 그렇지 않으면 모두
			   0인 섹터에서 시작한다. */
			if (sector_ofs > 0 || chunk_size < sector_left) 
				disk_read (filesys_disk, sector_idx, bounce);
			else
				memset (bounce, 0, DISK_SECTOR_SIZE);
			memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
			disk_write (filesys_disk, sector_idx, bounce); 
		}

		/* 전진. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}
	free (bounce);

	return bytes_written;
}

/* INODE에 대한 쓰기를 비활성화한다.
   inode를 연 쪽마다 최대 한 번만 호출할 수 있다. */
	void
inode_deny_write (struct inode *inode) 
{
	inode->deny_write_cnt++;
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* INODE에 대한 쓰기를 다시 허용한다.
 * inode에 대해 inode_deny_write()를 호출했던 각 opener는
 * inode를 닫기 전에 이 함수를 한 번 호출해야 한다. */
void
inode_allow_write (struct inode *inode) {
	ASSERT (inode->deny_write_cnt > 0);
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
	inode->deny_write_cnt--;
}

/* INODE 데이터의 길이를 바이트 단위로 반환한다. */
off_t
inode_length (const struct inode *inode) {
	return inode->data.length;
}
