#include "dss_ispresizer.h"

struct isp_node pipe;
wait_queue_head_t wait;
u32 out_buf_phy_addr[MAX_VIDEO_BUFFERS];
static int index;
static int isprsz;
extern int isp_reset;

void dss_isp_rsz_dma_tx_callback(void *arg)
{
	wake_up_interruptible(&wait);
}

int ispresizer_init(u16 width, u16 height, u16 crop_width, u16 crop_height,
		u16 out_width, u16 out_height)
{
	struct v4l2_pix_format pix;
	int num_video_buffers = 0;
	int ret = 0;
	if (isp_reset == 1) {
		isprsz = 0;
		isp_reset = 0;
	}

	if (isprsz)
		return 0;

	memset(out_buf_phy_addr, 0, (sizeof(u32)*MAX_VIDEO_BUFFERS));
	index = 0;
	/* get the ISP resizer resource and configure it*/
	ispdss_put_resource();
	ret = ispdss_get_resource();
	if (ret) {
		pr_info("<%s>: <%s> failed to get ISP "
				"resizer resource = %d\n",
				__FILE__, __func__, ret);
		return ret;
	}

	pix.width = width;
	pix.height = height;
	pix.pixelformat = V4L2_PIX_FMT_UYVY;
	pix.field = V4L2_FIELD_NONE;
	pix.bytesperline = pix.width * 2;
	pix.sizeimage = pix.bytesperline * pix.height;
	pix.bytesperline = (pix.bytesperline + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	pix.priv = 0;
	pix.colorspace = V4L2_COLORSPACE_JPEG;

	/* clear data */
	memset(&pipe, 0, sizeof(pipe));
	pipe.in.path = RSZ_MEM_YUV;
	/* setup source parameters */
	pipe.in.image = pix;
	pipe.in.crop.left = 0;
	pipe.in.crop.top = 0;
	pipe.in.crop.width = crop_width;
	pipe.in.crop.height = crop_height;
	/* setup destination parameters */
	pipe.out.image.width = out_width;
	pipe.out.image.height = out_height;

	num_video_buffers = MAX_VIDEO_BUFFERS;

	ret = ispdss_configure(&pipe, dss_isp_rsz_dma_tx_callback,
			num_video_buffers, (void *)out_width);
	if (ret) {
		pr_info("<%s> failed to configure "
				"ISP_resizer = %d\n",
				__func__, ret);
		ispdss_put_resource();
		return ret;
	}

	isprsz = 1;

	return ret;
}

int ispresizer_begin(u16 width, u16 height, u32 *paddr)
{
	int ret = 0;
	for (ret = 0; ret < MAX_VIDEO_BUFFERS ; ret++) {
		if (out_buf_phy_addr[ret] == 0)	{
			out_buf_phy_addr[ret] = *paddr;
			index = ret;
			break;
		} else if (out_buf_phy_addr[ret] == *paddr) {
			index = ret;
			break;
		}
	}

	/*Start resizing*/
	ret = ispdss_begin(&pipe, index, index,
			pipe.out.image.width * 2,
			*paddr,
			*paddr,
			width * height * 2);

	return ret;
}

