/*
 * redesigned by warren zhao for inter-process purpose with ease.
 *
 */

#ifndef _LINUX_ZSHMEM_H
#define _LINUX_ZSHMEM_H

#include <linux/limits.h>
#include <linux/ioctl.h>

#define ZSHMEM_NAME_LEN		255

#define ZSHMEM_NAME_DEF		"/dev/zshmem"

/* Return values from ZSHMEM_PIN: Was the mapping purged while unpinned? */
#define ZSHMEM_NOT_PURGED	0
#define ZSHMEM_WAS_PURGED	1

/* Return values from ZSHMEM_GET_PIN_STATUS: Is the mapping pinned? */
#define ZSHMEM_IS_UNPINNED	0
#define ZSHMEM_IS_PINNED	1

struct zshmem_pin {
	__u32 offset;	/* offset into region, in bytes, page-aligned */
	__u32 len;	/* length forward from offset, in bytes, page-aligned */
};

struct zshmem_alloc {
	size_t  size;	
	char    name[ZSHMEM_NAME_LEN+1];	
};

#define __ZSHMEMIOC		0x77

//only after the file attach handle, it can operate
#define ZSHMEM_ALLOC_SHM_HANDLE		_IOW(__ZSHMEMIOC, 1, struct zshmem_alloc)
#define ZSHMEM_GET_NAME				_IOR(__ZSHMEMIOC, 2, char[ZSHMEM_NAME_LEN+1])
#define ZSHMEM_ATTACH_SHM_HANDLE	_IOW(__ZSHMEMIOC, 3, char[ZSHMEM_NAME_LEN+1])
#define ZSHMEM_GET_SIZE				_IO(__ZSHMEMIOC, 4)
#define ZSHMEM_SET_PROT_MASK		_IOW(__ZSHMEMIOC, 5, unsigned long)
#define ZSHMEM_GET_PROT_MASK		_IO(__ZSHMEMIOC, 6)
#define ZSHMEM_PIN					_IOW(__ZSHMEMIOC, 7, struct zshmem_pin)
#define ZSHMEM_UNPIN				_IOW(__ZSHMEMIOC, 8, struct zshmem_pin)
#define ZSHMEM_GET_PIN_STATUS		_IO(__ZSHMEMIOC, 9)
#define ZSHMEM_PURGE_ALL_CACHES		_IO(__ZSHMEMIOC, 10)
#define ZSHMEM_DEALLOC_SHM_HANDLE	_IO(__ZSHMEMIOC, 11)

#endif	/* _LINUX_ZSHMEM_H */
