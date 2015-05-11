/* 
** redesigned by warren zhao to setup named sharemem mechanism.
** the purpose is to reach inter-process sharing with ease.
*/

#include <linux/module.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/falloc.h>
#include <linux/miscdevice.h>
#include <linux/security.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/uaccess.h>
#include <linux/personality.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/shmem_fs.h>
#include "zshmem.h"

#define ZSHMEM_NAME_PREFIX		"dev/zshmem/"
#define ZSHMEM_NAME_PREFIX_LEN	(sizeof(ZSHMEM_NAME_PREFIX) - 1)
#define ZSHMEM_FULL_NAME_LEN	(ZSHMEM_NAME_LEN + ZSHMEM_NAME_PREFIX_LEN)
//Jonny
#define VM_CAN_NONLINEAR 0x08000000

/*
 * zshmem_area - anonymous shared memory area
 * Lifecycle: From our parent file's open() until its release()
 * Locking: Protected by `zshmem_mutex'
 * Big Note: Mappings do NOT pin this structure; it dies on close()
 */
struct zshmem_area {
	char name[ZSHMEM_FULL_NAME_LEN+1]; /* optional name in /proc/pid/maps */
	struct list_head unpinned_list;	 /* list of all zshmem areas */
	struct list_head handle_entry;
	struct file *file;		 /* the shmem-based backing file */
	size_t size;			 /* size of the mapping, in bytes */
	unsigned long prot_mask;	 /* allowed prot bits, as vm_flags */
};

/*
 * zshmem_range - represents an interval of unpinned (evictable) pages
 * Lifecycle: From unpin to pin
 * Locking: Protected by `zshmem_mutex'
 */
struct zshmem_range {
	struct list_head lru;		/* entry in LRU list */
	struct list_head unpinned;	/* entry in its area's unpinned list */
	struct zshmem_area *asma;	/* associated area */
	size_t pgstart;			/* starting page, inclusive */
	size_t pgend;			/* ending page, inclusive */
	unsigned int purged;		/* ZSHMEM_NOT or ZSHMEM_WAS_PURGED */
};

/* LRU list of unpinned pages, protected by zshmem_mutex */
static LIST_HEAD(zshmem_lru_list);

/* Count of pages on our LRU list, protected by zshmem_mutex */
static unsigned long lru_count;

static LIST_HEAD(zshmem_handle_list);

/*
 * zshmem_mutex - protects the list of and each individual zshmem_area
 *
 * Lock Ordering: ashmex_mutex -> i_mutex -> i_alloc_sem
 */
static DEFINE_MUTEX(zshmem_mutex);

static struct kmem_cache *zshmem_area_cachep __read_mostly;
static struct kmem_cache *zshmem_range_cachep __read_mostly;

#define range_size(range) \
	((range)->pgend - (range)->pgstart + 1)

#define range_on_lru(range) \
	((range)->purged == ZSHMEM_NOT_PURGED)

#define page_range_subsumes_range(range, start, end) \
	(((range)->pgstart >= (start)) && ((range)->pgend <= (end)))

#define page_range_subsumed_by_range(range, start, end) \
	(((range)->pgstart <= (start)) && ((range)->pgend >= (end)))

#define page_in_range(range, page) \
	(((range)->pgstart <= (page)) && ((range)->pgend >= (page)))

#define page_range_in_range(range, start, end) \
	(page_in_range(range, start) || page_in_range(range, end) || \
		page_range_subsumes_range(range, start, end))

#define range_before_page(range, page) \
	((range)->pgend < (page))

#define PROT_MASK		(PROT_EXEC | PROT_READ | PROT_WRITE)

static inline void lru_add(struct zshmem_range *range)
{
	list_add_tail(&range->lru, &zshmem_lru_list);
	lru_count += range_size(range);
}

static inline void lru_del(struct zshmem_range *range)
{
	list_del(&range->lru);
	lru_count -= range_size(range);
}

/*
 * range_alloc - allocate and initialize a new zshmem_range structure
 *
 * 'asma' - associated zshmem_area
 * 'prev_range' - the previous zshmem_range in the sorted asma->unpinned list
 * 'purged' - initial purge value (ASMEM_NOT_PURGED or ZSHMEM_WAS_PURGED)
 * 'start' - starting page, inclusive
 * 'end' - ending page, inclusive
 *
 * Caller must hold zshmem_mutex.
 */
static int range_alloc(struct zshmem_area *asma,
		       struct zshmem_range *prev_range, unsigned int purged,
		       size_t start, size_t end)
{
	struct zshmem_range *range;

	range = kmem_cache_zalloc(zshmem_range_cachep, GFP_KERNEL);
	if (unlikely(!range))
		return -ENOMEM;

	range->asma = asma;
	range->pgstart = start;
	range->pgend = end;
	range->purged = purged;

	list_add_tail(&range->unpinned, &prev_range->unpinned);

	if (range_on_lru(range))
		lru_add(range);

	return 0;
}

static void range_del(struct zshmem_range *range)
{
	list_del(&range->unpinned);
	if (range_on_lru(range))
		lru_del(range);
	kmem_cache_free(zshmem_range_cachep, range);
}

/*
 * range_shrink - shrinks a range
 *
 * Caller must hold zshmem_mutex.
 */
static inline void range_shrink(struct zshmem_range *range,
				size_t start, size_t end)
{
	size_t pre = range_size(range);

	range->pgstart = start;
	range->pgend = end;

	if (range_on_lru(range))
		lru_count -= pre - range_size(range);
}

static int zshmem_open(struct inode *inode, struct file *file)
{
	//struct zshmem_area *asma;
	int ret;

	ret = generic_file_open(inode, file);
	if (unlikely(ret))
		return ret;

	return 0;
}

static int zshmem_release(struct inode *ignored, struct file *file)
{

	return 0;
}

static ssize_t zshmem_read(struct file *file, char __user *buf,
			   size_t len, loff_t *pos)
{
	struct zshmem_area *asma = file->private_data;
	int ret = 0;

	mutex_lock(&zshmem_mutex);

	/* If size is not set, or set to 0, always return EOF. */
	if (asma->size == 0)
		goto out;

	if (!asma->file) {
		ret = -EBADF;
		goto out;
	}

	ret = asma->file->f_op->read(asma->file, buf, len, pos);
	if (ret < 0)
		goto out;

	/** Update backing file pos, since f_ops->read() doesn't */
	asma->file->f_pos = *pos;

out:
	mutex_unlock(&zshmem_mutex);
	return ret;
}

static loff_t zshmem_llseek(struct file *file, loff_t offset, int origin)
{
	struct zshmem_area *asma = file->private_data;
	int ret;

	mutex_lock(&zshmem_mutex);

	if (asma->size == 0) {
		ret = -EINVAL;
		goto out;
	}

	if (!asma->file) {
		ret = -EBADF;
		goto out;
	}

	ret = asma->file->f_op->llseek(asma->file, offset, origin);
	if (ret < 0)
		goto out;

	/** Copy f_pos from backing file, since f_ops->llseek() sets it */
	file->f_pos = asma->file->f_pos;

out:
	mutex_unlock(&zshmem_mutex);
	return ret;
}

static inline vm_flags_t calc_vm_may_flags(unsigned long prot)
{
	return _calc_vm_trans(prot, PROT_READ,  VM_MAYREAD) |
	       _calc_vm_trans(prot, PROT_WRITE, VM_MAYWRITE) |
	       _calc_vm_trans(prot, PROT_EXEC,  VM_MAYEXEC);
}

static int zshmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct zshmem_area *asma = file->private_data;
	int ret = 0;

	mutex_lock(&zshmem_mutex);

	/* user needs to ALLOC or ATTACH the handle before mapping */
	if (unlikely(!asma)) 
	{
		ret = -EINVAL;
		goto out;
	}
	if (unlikely(!asma->size)) 
	{
		ret = -EINVAL;
		goto out;
	}

	/* requested protection bits must match our allowed protection mask */
	if (unlikely((vma->vm_flags & ~calc_vm_prot_bits(asma->prot_mask)) &
		     calc_vm_prot_bits(PROT_MASK))) 
	{
		ret = -EPERM;
		goto out;
	}
	vma->vm_flags &= ~calc_vm_may_flags(~asma->prot_mask);

	if (!asma->file) 
	{
		struct file *vmfile;

		/* ... and allocate the backing shmem file */
		vmfile = shmem_file_setup(asma->name, asma->size, vma->vm_flags);
		if (unlikely(IS_ERR(vmfile))) 
		{
			ret = PTR_ERR(vmfile);
			goto out;
		}
		asma->file = vmfile;
	}
	get_file(asma->file);

	if (vma->vm_flags & VM_SHARED)
	{
		shmem_set_file(vma, asma->file);
	}
	else
	{
		if (vma->vm_file)
		{
			fput(vma->vm_file);
		}
		vma->vm_file = asma->file;
	}
	vma->vm_flags |= VM_CAN_NONLINEAR;

out:
	mutex_unlock(&zshmem_mutex);
	return ret;
}

/*
 * zshmem_shrink - our cache shrinker, called from mm/vmscan.c :: shrink_slab
 *
 * 'nr_to_scan' is the number of objects (pages) to prune, or 0 to query how
 * many objects (pages) we have in total.
 *
 * 'gfp_mask' is the mask of the allocation that got us into this mess.
 *
 * Return value is the number of objects (pages) remaining, or -1 if we cannot
 * proceed without risk of deadlock (due to gfp_mask).
 *
 * We approximate LRU via least-recently-unpinned, jettisoning unpinned partial
 * chunks of zshmem regions LRU-wise one-at-a-time until we hit 'nr_to_scan'
 * pages freed.
 */
static int zshmem_shrink(struct shrinker *s, struct shrink_control *sc)
{
	struct zshmem_range *range, *next;

	/* We might recurse into filesystem code, so bail out if necessary */
	if (sc->nr_to_scan && !(sc->gfp_mask & __GFP_FS))
		return -1;
	if (!sc->nr_to_scan)
		return lru_count;

	mutex_lock(&zshmem_mutex);
	list_for_each_entry_safe(range, next, &zshmem_lru_list, lru) {
		loff_t start = range->pgstart * PAGE_SIZE;
		loff_t end = (range->pgend + 1) * PAGE_SIZE;

		do_fallocate(range->asma->file,
				FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
				start, end - start);
		range->purged = ZSHMEM_WAS_PURGED;
		lru_del(range);

		sc->nr_to_scan -= range_size(range);
		if (sc->nr_to_scan <= 0)
			break;
	}
	mutex_unlock(&zshmem_mutex);

	return lru_count;
}

static struct shrinker zshmem_shrinker = {
	.shrink = zshmem_shrink,
	.seeks = DEFAULT_SEEKS * 4,
};

static int set_prot_mask(struct zshmem_area *asma, unsigned long prot)
{
	int ret = 0;

	mutex_lock(&zshmem_mutex);

	/* the user can only remove, not add, protection bits */
	if (unlikely((asma->prot_mask & prot) != prot)) {
		ret = -EINVAL;
		goto out;
	}

	/* does the application expect PROT_READ to imply PROT_EXEC? */
	if ((prot & PROT_READ) && (current->personality & READ_IMPLIES_EXEC))
		prot |= PROT_EXEC;

	asma->prot_mask = prot;

out:
	mutex_unlock(&zshmem_mutex);
	return ret;
}

static int get_name(struct zshmem_area *asma, void __user *name)
{
	int ret = 0;

	mutex_lock(&zshmem_mutex);

	if(!asma)
	{
		//first alloc handle, then you can get name;
		ret = -EFAULT;
		goto out;
	}

	if (asma->name[ZSHMEM_NAME_PREFIX_LEN] != '\0') 
	{
		size_t len;

		/*
		 * Copying only `len', instead of ZSHMEM_NAME_LEN, bytes
		 * prevents us from revealing one user's stack to another.
		 */
		len = strlen(asma->name + ZSHMEM_NAME_PREFIX_LEN) + 1;
		if ( unlikely(copy_to_user(name, (asma->name + ZSHMEM_NAME_PREFIX_LEN), len)) )
		{
			ret = -EFAULT;
		}
	} 
	else 
	{
		if ( unlikely(copy_to_user(name, "\0",1)) )
		{
			ret = -EFAULT;
		}
	}

out:
	mutex_unlock(&zshmem_mutex);

	return ret;
}

/*
 * zshmem_pin - pin the given zshmem region, returning whether it was
 * previously purged (ZSHMEM_WAS_PURGED) or not (ZSHMEM_NOT_PURGED).
 *
 * Caller must hold zshmem_mutex.
 */
static int zshmem_pin(struct zshmem_area *asma, size_t pgstart, size_t pgend)
{
	struct zshmem_range *range, *next;
	int ret = ZSHMEM_NOT_PURGED;

	list_for_each_entry_safe(range, next, &asma->unpinned_list, unpinned) {
		/* moved past last applicable page; we can short circuit */
		if (range_before_page(range, pgstart))
			break;

		/*
		 * The user can ask us to pin pages that span multiple ranges,
		 * or to pin pages that aren't even unpinned, so this is messy.
		 *
		 * Four cases:
		 * 1. The requested range subsumes an existing range, so we
		 *    just remove the entire matching range.
		 * 2. The requested range overlaps the start of an existing
		 *    range, so we just update that range.
		 * 3. The requested range overlaps the end of an existing
		 *    range, so we just update that range.
		 * 4. The requested range punches a hole in an existing range,
		 *    so we have to update one side of the range and then
		 *    create a new range for the other side.
		 */
		if (page_range_in_range(range, pgstart, pgend)) {
			ret |= range->purged;

			/* Case #1: Easy. Just nuke the whole thing. */
			if (page_range_subsumes_range(range, pgstart, pgend)) {
				range_del(range);
				continue;
			}

			/* Case #2: We overlap from the start, so adjust it */
			if (range->pgstart >= pgstart) {
				range_shrink(range, pgend + 1, range->pgend);
				continue;
			}

			/* Case #3: We overlap from the rear, so adjust it */
			if (range->pgend <= pgend) {
				range_shrink(range, range->pgstart, pgstart-1);
				continue;
			}

			/*
			 * Case #4: We eat a chunk out of the middle. A bit
			 * more complicated, we allocate a new range for the
			 * second half and adjust the first chunk's endpoint.
			 */
			range_alloc(asma, range, range->purged,
				    pgend + 1, range->pgend);
			range_shrink(range, range->pgstart, pgstart - 1);
			break;
		}
	}

	return ret;
}

/*
 * zshmem_unpin - unpin the given range of pages. Returns zero on success.
 *
 * Caller must hold zshmem_mutex.
 */
static int zshmem_unpin(struct zshmem_area *asma, size_t pgstart, size_t pgend)
{
	struct zshmem_range *range, *next;
	unsigned int purged = ZSHMEM_NOT_PURGED;

restart:
	list_for_each_entry_safe(range, next, &asma->unpinned_list, unpinned) {
		/* short circuit: this is our insertion point */
		if (range_before_page(range, pgstart))
			break;

		/*
		 * The user can ask us to unpin pages that are already entirely
		 * or partially pinned. We handle those two cases here.
		 */
		if (page_range_subsumed_by_range(range, pgstart, pgend))
			return 0;
		if (page_range_in_range(range, pgstart, pgend)) {
			pgstart = min_t(size_t, range->pgstart, pgstart),
			pgend = max_t(size_t, range->pgend, pgend);
			purged |= range->purged;
			range_del(range);
			goto restart;
		}
	}

	return range_alloc(asma, range, purged, pgstart, pgend);
}

/*
 * zshmem_get_pin_status - Returns ZSHMEM_IS_UNPINNED if _any_ pages in the
 * given interval are unpinned and ZSHMEM_IS_PINNED otherwise.
 *
 * Caller must hold zshmem_mutex.
 */
static int zshmem_get_pin_status(struct zshmem_area *asma, size_t pgstart,
				 size_t pgend)
{
	struct zshmem_range *range;
	int ret = ZSHMEM_IS_PINNED;

	list_for_each_entry(range, &asma->unpinned_list, unpinned) {
		if (range_before_page(range, pgstart))
			break;
		if (page_range_in_range(range, pgstart, pgend)) {
			ret = ZSHMEM_IS_UNPINNED;
			break;
		}
	}

	return ret;
}

static int zshmem_pin_unpin(struct zshmem_area *asma, unsigned long cmd,
			    void __user *p)
{
	struct zshmem_pin pin;
	size_t pgstart, pgend;
	int ret = -EINVAL;

	if (unlikely(!asma->file))
		return -EINVAL;

	if (unlikely(copy_from_user(&pin, p, sizeof(pin))))
		return -EFAULT;

	/* per custom, you can pass zero for len to mean "everything onward" */
	if (!pin.len)
		pin.len = PAGE_ALIGN(asma->size) - pin.offset;

	if (unlikely((pin.offset | pin.len) & ~PAGE_MASK))
		return -EINVAL;

	if (unlikely(((__u32) -1) - pin.offset < pin.len))
		return -EINVAL;

	if (unlikely(PAGE_ALIGN(asma->size) < pin.offset + pin.len))
		return -EINVAL;

	pgstart = pin.offset / PAGE_SIZE;
	pgend = pgstart + (pin.len / PAGE_SIZE) - 1;

	mutex_lock(&zshmem_mutex);

	switch (cmd) {
	case ZSHMEM_PIN:
		ret = zshmem_pin(asma, pgstart, pgend);
		break;
	case ZSHMEM_UNPIN:
		ret = zshmem_unpin(asma, pgstart, pgend);
		break;
	case ZSHMEM_GET_PIN_STATUS:
		ret = zshmem_get_pin_status(asma, pgstart, pgend);
		break;
	}

	mutex_unlock(&zshmem_mutex);

	return ret;
}

static int zshmem_alloc_shm_handle(struct file *file, void __user *p)
{
	struct zshmem_alloc  alloc_handle;
	struct zshmem_area   *asma, *next;

	if ( unlikely(copy_from_user(&alloc_handle, p, sizeof(alloc_handle))) )
	{
		return -EFAULT;
	}

	if(!alloc_handle.size)
	{
		printk(KERN_ERR "zshmem: alloc size can not be zero\n");
		return -EFAULT;
	}
	if( (strlen(alloc_handle.name) <= 0) || (strlen(alloc_handle.name) > ZSHMEM_NAME_LEN) )
	{
		printk(KERN_ERR "zshmem: region name must be set\n");
		return -EFAULT;
	}

	mutex_lock(&zshmem_mutex);

	list_for_each_entry_safe(asma, next, &zshmem_handle_list, handle_entry) 
	{
		if( strcmp(&(asma->name[strlen(ZSHMEM_NAME_PREFIX)]),alloc_handle.name) == 0 )
		{
			return -1;
		}
	}
	mutex_unlock(&zshmem_mutex);

	asma = kmem_cache_zalloc(zshmem_area_cachep, GFP_KERNEL);
	if (unlikely(!asma))
	{
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&asma->unpinned_list);
	snprintf(asma->name,(ZSHMEM_FULL_NAME_LEN),"%s%s",ZSHMEM_NAME_PREFIX,alloc_handle.name);
	asma->size = alloc_handle.size;
	asma->prot_mask = PROT_MASK;
	file->private_data = asma;
	list_add_tail(&asma->handle_entry, &zshmem_handle_list);

	return 0;
}

static int zshmem_dealloc_shm_handle(struct file *file)
{
	struct zshmem_area *asma = file->private_data;
	struct zshmem_range *range, *next;

	mutex_lock(&zshmem_mutex);

	list_del(&asma->handle_entry);
	list_for_each_entry_safe(range, next, &asma->unpinned_list, unpinned)
	{
		range_del(range);
	}

	mutex_unlock(&zshmem_mutex);

	if (asma->file)
	{
		fput(asma->file);
	}
	kmem_cache_free(zshmem_area_cachep, asma);

	return 0;
}

static int zshmem_get_shm_handle(struct file *file, void __user *name)
{
	int ret = -1;
	struct zshmem_area *asma, *next;
	char   tname[ZSHMEM_NAME_LEN+1];

	if ( unlikely(copy_from_user(tname, name, ZSHMEM_NAME_LEN)) )
	{
		return -EFAULT;
	}
	tname[ZSHMEM_NAME_LEN] = 0;
printk(KERN_ERR "zshmem: tname=%s\n",tname);

	mutex_lock(&zshmem_mutex);

	list_for_each_entry_safe(asma, next, &zshmem_handle_list, handle_entry) 
	{
printk(KERN_ERR "zshmem: &(asma->name[strlen(ZSHMEM_NAME_PREFIX)])=%s\n",&(asma->name[strlen(ZSHMEM_NAME_PREFIX)]));
		if( strcmp(&(asma->name[strlen(ZSHMEM_NAME_PREFIX)]),tname) == 0 )
		{
			file->private_data = asma;
			ret = 0;
printk(KERN_ERR "zshmem: find same name shm handle\n");
			break;
		}
	}
	mutex_unlock(&zshmem_mutex);

	return ret;
}

static long zshmem_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct zshmem_area *asma = file->private_data;
	long ret = -ENOTTY;

	switch (cmd) {
	case ZSHMEM_ALLOC_SHM_HANDLE:
		ret = zshmem_alloc_shm_handle(file,(void __user *) arg);
		break;
	case ZSHMEM_DEALLOC_SHM_HANDLE:
		ret = zshmem_dealloc_shm_handle(file);
		break;
	case ZSHMEM_ATTACH_SHM_HANDLE:
		ret = zshmem_get_shm_handle(file, (void __user *) arg);
		break;
	case ZSHMEM_GET_NAME:
		if(!asma)
		{
			printk(KERN_ERR "zshmem: first alloc or attach shm handle, then you can operate\n");
			ret = -EFAULT;
		}
		else
		{
			ret = get_name(asma, (void __user *) arg);
		}
		break;
	case ZSHMEM_GET_SIZE:
		if(!asma)
		{
			printk(KERN_ERR "zshmem: first alloc or attach shm handle, then you can operate\n");
			ret = -EFAULT;
		}
		else
		{
			ret = asma->size;
		}
		break;
	case ZSHMEM_SET_PROT_MASK:
		if(!asma)
		{
			printk(KERN_ERR "zshmem: first alloc or attach shm handle, then you can operate\n");
			ret = -EFAULT;
		}
		else
		{
			ret = set_prot_mask(asma, arg);
		}
		break;
	case ZSHMEM_GET_PROT_MASK:
		if(!asma)
		{
			printk(KERN_ERR "zshmem: first alloc or attach shm handle, then you can operate\n");
			ret = -EFAULT;
		}
		else
		{
			ret = asma->prot_mask;
		}
		break;
	case ZSHMEM_PIN:
	case ZSHMEM_UNPIN:
	case ZSHMEM_GET_PIN_STATUS:
		if(!asma)
		{
			printk(KERN_ERR "zshmem: first alloc or attach shm handle, then you can operate\n");
			ret = -EFAULT;
		}
		else
		{
			ret = zshmem_pin_unpin(asma, cmd, (void __user *) arg);
		}
		break;
	case ZSHMEM_PURGE_ALL_CACHES:
		ret = -EPERM;
		if (capable(CAP_SYS_ADMIN)) 
		{
			struct shrink_control sc = 
			{
				.gfp_mask = GFP_KERNEL,
				.nr_to_scan = 0,
			};
			ret = zshmem_shrink(&zshmem_shrinker, &sc);
			sc.nr_to_scan = ret;
			zshmem_shrink(&zshmem_shrinker, &sc);
		}
		break;
	}

	return ret;
}

static const struct file_operations zshmem_fops = {
	.owner = THIS_MODULE,
	.open = zshmem_open,
	.release = zshmem_release,
	.read = zshmem_read,
	.llseek = zshmem_llseek,
	.mmap = zshmem_mmap,
	.unlocked_ioctl = zshmem_ioctl,
	.compat_ioctl = zshmem_ioctl,
};

static struct miscdevice zshmem_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "zshmem",
	.fops = &zshmem_fops,
};

static int __init zshmem_init(void)
{
	int ret;

	zshmem_area_cachep = kmem_cache_create("warren_shm_area_cache",
					  sizeof(struct zshmem_area),
					  0, 0, NULL);
	if (unlikely(!zshmem_area_cachep)) {
		printk(KERN_ERR "zshmem: failed to create slab cache\n");
		return -ENOMEM;
	}

	zshmem_range_cachep = kmem_cache_create("warren_shm_range_cache",
					  sizeof(struct zshmem_range),
					  0, 0, NULL);
	if (unlikely(!zshmem_range_cachep)) {
		printk(KERN_ERR "zshmem: failed to create slab cache\n");
		return -ENOMEM;
	}

	ret = misc_register(&zshmem_misc);
	if (unlikely(ret)) {
		printk(KERN_ERR "zshmem: failed to register misc device!\n");
		return ret;
	}

	register_shrinker(&zshmem_shrinker);

	printk(KERN_INFO "zshmem: initialized\n");

	return 0;
}

static void __exit zshmem_exit(void)
{
	int ret;

	unregister_shrinker(&zshmem_shrinker);

	ret = misc_deregister(&zshmem_misc);
	if (unlikely(ret))
		printk(KERN_ERR "zshmem: failed to unregister misc device!\n");

	kmem_cache_destroy(zshmem_range_cachep);
	kmem_cache_destroy(zshmem_area_cachep);

	printk(KERN_INFO "zshmem: unloaded\n");
}

module_init(zshmem_init);
module_exit(zshmem_exit);

MODULE_LICENSE("GPL");
