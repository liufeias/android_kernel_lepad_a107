#ifndef __OMAP2_DSS_ISPRESIZER_H
#define __OMAP2_DSS_ISPRESIZER_H

#include <linux/kernel.h>
#include <linux/sched.h>
#include <mach/isp_user.h>
#include <linux/ispdss.h>
#include "../../../media/video/isp/ispresizer.h"
#include <linux/omap_resizer.h>

#define MAX_VIDEO_BUFFERS	6
#define VID_MAX_WIDTH		1280	/* Largest width */
#define VID_MAX_HEIGHT		720	/* Largest height */

int ispresizer_init(u16 width, u16 height, u16 crop_width, u16 crop_height,
		u16 out_width, u16 out_height);
int ispresizer_begin(u16 width, u16 height, u32 *paddr);

#endif
