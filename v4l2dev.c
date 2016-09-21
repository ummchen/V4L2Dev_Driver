/********************************
* Module:       V4L2Dev         *
* Author:       Josh Chen       *
* Date:         2016/09/08      *
********************************/

#include <linux/module.h>
#include <linux/videodev2.h>
#include <media/videobuf2-vmalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <media/v4l2-common.h>

#define IMG_WIDTH	320
#define IMG_HEIGHT	240

#ifdef DEBUG
#define DEBUG_PRINT(format, args...) printfk(KERN_DEBUG "[%s:%d] "format, __FILE__, __LINE__, ##args)
#else
#define DEBUG_PRINT(args...)
#endif

struct v4l2dev
{
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct vb2_queue queue;
	struct mutex lock;
};

static struct v4l2dev v4l2_dev;

static const struct v4l2_pix_format v4l2dev_video_format =
{
	.pixelformat	= V4L2_PIX_FMT_GREY,
	.width			= IMG_WIDTH,
	.height			= IMG_HEIGHT,
	.field			= V4L2_FIELD_NONE,
	.colorspace		= V4L2_COLORSPACE_SRGB,
	.bytesperline	= IMG_WIDTH,
	.sizeimage		= IMG_WIDTH * IMG_HEIGHT,
};

static int v4l2dev_vidioc_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	strlcpy(cap->driver, "v4l2dev", sizeof(cap->driver));
	strlcpy(cap->card, "V4L2Dev Device", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "V4L2Dev Bus");
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int v4l2dev_vidioc_enum_fmt(struct file *file, void *priv, struct v4l2_fmtdesc *f)
{
	if (f->index != 0)
	{
		DEBUG_PRINT("v4l2_fmtdesc index != 0\n");
		return -EINVAL;
	}
	f->pixelformat = V4L2_PIX_FMT_GREY;
	return 0;
}

static int v4l2dev_vidioc_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	f->fmt.pix = v4l2dev_video_format;
	return 0;
}

static int v4l2dev_vidioc_enum_input(struct file *file, void *priv, struct v4l2_input *i)
{
	if (i->index != 0)
	{
		DEBUG_PRINT("v4l2_input index != 0\n");
		return -EINVAL;
	}
	i->type = V4L2_INPUT_TYPE_CAMERA;
	strlcpy(i->name, "V4L2Dev", sizeof(i->name));
	return 0;
}

static int v4l2dev_vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	return (i == 0) ? 0 : -EINVAL;
}

static int v4l2dev_vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int v4l2dev_queue_setup(struct vb2_queue *q, const struct v4l2_format *fmt, unsigned int *nbuffers, unsigned int *nplanes, unsigned int sizes[], void *alloc_ctxs[])	//for < 4.4
//static int v4l2dev_queue_setup(struct vb2_queue *q, const void *parg, unsigned int *nbuffers, unsigned int *nplanes, unsigned int sizes[], void *alloc_ctxs[])	//for = 4.4
//static int v4l2dev_queue_setup(struct vb2_queue *q, unsigned int *nbuffers, unsigned int *nplanes, unsigned int sizes[], void *alloc_ctxs[])	//for > 4.4
{
	size_t size = IMG_WIDTH * IMG_HEIGHT;
	if (*nplanes)
		return sizes[0] < size ? -EINVAL : 0;
	*nplanes = 1;
	sizes[0] = size;
	return 0;
}

static void v4l2dev_buffer_queue(struct vb2_buffer *vb)
{
	static unsigned char data = 0;
	struct v4l2dev *v4l2dev_data = vb2_get_drv_priv(vb->vb2_queue);
	u8 *ptr;

	if (v4l2dev_data == NULL)
	{
		DEBUG_PRINT("vb2_get_drv_pri fail\n");
		goto fault;
	}

	ptr = vb2_plane_vaddr(vb, 0);
	if (!ptr)
	{
		DEBUG_PRINT("vb2_plane_vaddr fail\n");
		goto fault;
	}

	memset(ptr, data++, IMG_WIDTH * IMG_HEIGHT);

	vb2_set_plane_payload(vb, 0, IMG_WIDTH * IMG_HEIGHT);
	vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
	return;
fault:
	vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops v4l2dev_queue_ops = {
	.queue_setup	= v4l2dev_queue_setup,
	.buf_queue		= v4l2dev_buffer_queue,
	.wait_prepare	= vb2_ops_wait_prepare,
	.wait_finish	= vb2_ops_wait_finish,
};

static const struct vb2_queue v4l2dev_queue = {
	.type				= V4L2_BUF_TYPE_VIDEO_CAPTURE,
	.io_modes			= VB2_MMAP | VB2_READ,
	.buf_struct_size	= sizeof(struct vb2_buffer),
	.ops				= &v4l2dev_queue_ops,
	.mem_ops			= &vb2_vmalloc_memops,
	//.timestamp_flags	= V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC,	//for > 3.14 only
	.min_buffers_needed	= 1,
};

static const struct v4l2_file_operations v4l2dev_video_fops = {
	.owner			= THIS_MODULE,
	.open			= v4l2_fh_open,
	.release		= vb2_fop_release,
	.unlocked_ioctl	= video_ioctl2,
	.read			= vb2_fop_read,
	.mmap			= vb2_fop_mmap,
	.poll			= vb2_fop_poll,
};

static const struct v4l2_ioctl_ops v4l2dev_video_ioctl_ops = {
	.vidioc_querycap			= v4l2dev_vidioc_querycap,
	.vidioc_enum_fmt_vid_cap	= v4l2dev_vidioc_enum_fmt,
	.vidioc_try_fmt_vid_cap		= v4l2dev_vidioc_fmt,
	.vidioc_s_fmt_vid_cap		= v4l2dev_vidioc_fmt,
	.vidioc_g_fmt_vid_cap		= v4l2dev_vidioc_fmt,
 	.vidioc_enum_input			= v4l2dev_vidioc_enum_input,
	.vidioc_g_input				= v4l2dev_vidioc_g_input,
	.vidioc_s_input				= v4l2dev_vidioc_s_input,
	.vidioc_reqbufs				= vb2_ioctl_reqbufs,
	.vidioc_create_bufs			= vb2_ioctl_create_bufs,
	.vidioc_querybuf			= vb2_ioctl_querybuf,
	.vidioc_qbuf				= vb2_ioctl_qbuf,
	.vidioc_dqbuf				= vb2_ioctl_dqbuf,
	.vidioc_expbuf				= vb2_ioctl_expbuf,
	.vidioc_streamon			= vb2_ioctl_streamon,
	.vidioc_streamoff			= vb2_ioctl_streamoff,
};

static const struct video_device v4l2dev_video_device = {
	.name		= "V4L2Dev Video Device",
	.fops		= &v4l2dev_video_fops,
	.ioctl_ops	= &v4l2dev_video_ioctl_ops,
	.release	= video_device_release_empty,
};

static int __init v4l2dev_init(void)
{    
	struct v4l2dev *v4l2dev_data;
	int ret;

	//v4l2dev_data = kzalloc(sizeof(struct v4l2dev), GFP_KERNEL);
	v4l2dev_data = &v4l2_dev;
	if (v4l2dev_data == NULL)
	{
		DEBUG_PRINT("kzalloc fail\n");
		return -1;
	}

	strlcpy(v4l2dev_data->v4l2_dev.name, "V4L2Dev V4L2 Device", sizeof(v4l2dev_data->v4l2_dev.name));
	ret = v4l2_device_register(NULL, &v4l2dev_data->v4l2_dev);
	if (ret < 0)
	{
		DEBUG_PRINT("v4l2_device_register fail\n");
		goto error_unreg_v4l2;
	}

	mutex_init(&v4l2dev_data->lock);
	v4l2dev_data->queue				= v4l2dev_queue;
	v4l2dev_data->queue.drv_priv	= v4l2dev_data;
	v4l2dev_data->queue.lock		= &v4l2dev_data->lock;
	ret = vb2_queue_init(&v4l2dev_data->queue);
	if (ret < 0)
	{
		DEBUG_PRINT("vb2_queue_init fail\n");
		goto error_unreg_v4l2;
	}

	v4l2dev_data->vdev = v4l2dev_video_device;
	strlcpy(v4l2dev_data->vdev.name, "V4l2Dev Video Device", sizeof(v4l2dev_data->vdev.name));
	v4l2dev_data->vdev.v4l2_dev	= &v4l2dev_data->v4l2_dev;
	v4l2dev_data->vdev.lock		= &v4l2dev_data->lock;
	v4l2dev_data->vdev.queue	= &v4l2dev_data->queue;
	video_set_drvdata(&v4l2dev_data->vdev, v4l2dev_data);

	ret = video_register_device(&v4l2dev_data->vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0)
	{
		DEBUG_PRINT("video_register_device fail\n");
		ret = -ENODEV;
		goto error_unreg_video;
	}
	DEBUG_PRINT("module inserted\n");
	return 0;

error_unreg_video:
	video_unregister_device(&v4l2dev_data->vdev);
error_unreg_v4l2:
	v4l2_device_unregister(&v4l2dev_data->v4l2_dev);
	return ret;
}

static void __exit v4l2dev_exit(void)
{
	struct v4l2dev *v4l2dev_data;
    	v4l2dev_data = &v4l2_dev;

	if (v4l2dev_data == NULL)
	{
		DEBUG_PRINT("v4l2dev_data == NULL\n");
		return;
	}

	DEBUG_PRINT("module remove\n");
	video_unregister_device(&v4l2dev_data->vdev);
	v4l2_device_unregister(&v4l2dev_data->v4l2_dev);
}

module_init(v4l2dev_init);
module_exit(v4l2dev_exit);

MODULE_DESCRIPTION("V4L2Dev Device");
MODULE_LICENSE("GPL");

