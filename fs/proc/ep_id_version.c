#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/utsname.h>
#include <linux/io.h>

#include <linux/share_region.h>

#define IOCTL_OPEN 0x9f
#define IOCTL_CLOSE 0xAf

static int read_flag = 0;

static int version_proc_ep_id_show(struct seq_file *m, void *v)
{
	char version[32];
	if(read_flag==0){
		version[0]='0';
		version[1]='f';
		version[2]='\0';
		seq_printf(m, "%s", version);
	}else{
		ep_get_device_id(version, sizeof(version));
		version[31] = '\0';
		seq_printf(m, "%s", version);
	}
	return 0;
}

static int version_proc_ep_id_open(struct inode *inode, struct file *file)
{
	return single_open(file, version_proc_ep_id_show, NULL);
}

int get_dev_id_ioctl(struct file *filp,unsigned int cmd, unsigned long arg)
{

	switch (cmd) {        
		case IOCTL_OPEN:
			read_flag = 1;
			break;   
		case IOCTL_CLOSE:
			read_flag = 0;
			break;	
		default:            
			printk("[Driver ] default  \n");
			break;   
	}     

	return 0;
}

static const struct file_operations version_proc_ep_id_fops = {
	.open		= version_proc_ep_id_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.unlocked_ioctl = get_dev_id_ioctl,
};

static int __init proc_version_ep_id_init(void)
{
	proc_create("ep_id_version", 0, NULL, &version_proc_ep_id_fops);
	return 0;
}
module_init(proc_version_ep_id_init);
