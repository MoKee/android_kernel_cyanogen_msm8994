/*------------------------------------------------------------------------------
 * by warren zhao
 -----------------------------------------------------------------------------*/

#include <linux/cdev.h>

//fixed minor is from MINOR_START to MINOR_MIN-1 
enum {
	SILEAD_MINOR_XXXX = 0,

	/* add the fixed minors here */
	/* ... */

	SILEAD_MINOR_DYN_MIN,
	SILEAD_MINOR_DYN_MAX = 255
};

/* dynamical minor grows from MINOR_MIN */
#define	SILEAD_MINOR_DYNAMICAL	-1

struct silead_device {
	char			*name;
	uint32_t		minor; 	/* SILEAD_MINOR_... */
	uint32_t		count;	/* 1 or more */
	struct cdev		cdev;
};

/* register a silead class device.
 * @param silead:	the following fields should be provided.
 * 		silead->name
 * 		silead->minor
 * 		silead->count
 * @param ops:	file operations
 */
int silead_device_register(struct silead_device *silead,
		struct file_operations *ops);

/* unregister a silead class device */
int silead_device_unregister(struct silead_device *silead);

