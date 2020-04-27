// Kernel module that blocks/allows tcp connections

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/device.h>

#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <asm/ioctl.h>

#define IOCTL_FILTER_ADDRESS	_IOW('k', 1, unsigned int) // Used for creating the ioctl command number

static char *toggle_string = "no_block";
module_param(toggle_string, charp, 0000);
MODULE_PARM_DESC(toggle_string, "Toggle Blocker on/off");

static int dom_netfilter_open(struct inode *inode, struct file *file);
static int dom_netfilter_release(struct inode *inode, struct file *file);
static long dom_netfilter_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = dom_netfilter_open,
	.release = dom_netfilter_release,
	.unlocked_ioctl = dom_netfilter_ioctl
};

static unsigned int dom_netfilter_hookfn(void *priv, struct sk_buff *skb, const struct nf_hook_state *state);

static struct nf_hook_ops net_nfho = {
	.hook        = dom_netfilter_hookfn,
	.hooknum     = NF_INET_LOCAL_OUT,
	.pf          = PF_INET,
	.priority    = NF_IP_PRI_FIRST
};

static struct cdev dom_cdev;
static int dev_major = 0;
static atomic_t ioctl_set;
static unsigned int ioctl_set_addr;

// Test ioctl_set_addr if it has been set.
static int test_daddr(unsigned int dst_addr) {
	int ret = 0;

	if (atomic_read(&ioctl_set) == 1)
		ret = (ioctl_set_addr == dst_addr);
	else
		ret = 1;
		
	return ret;
}

static unsigned int dom_netfilter_hookfn(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {
	// get IP header
	struct iphdr *iph = ip_hdr(skb);

	if (iph->protocol == IPPROTO_TCP && test_daddr(iph->daddr)) {
		// then get the TCP header
		struct tcphdr *tcph = tcp_hdr(skb);
		
		// see if the packet was for initiation
		if (tcph->syn && !tcph->ack) {
			printk("domnetfilter: TCP connection initiated from " "%pI4:%u\n", &iph->saddr, ntohs(tcph->source));
			if (!strcmp(toggle_string, "block"))
				printk("domnetfilter: Packets being blocked!");
			else
				printk("domnetfilter: Packets allowed to pass!");
		}
	}

	// what to do with the packets...
 	if (!strcmp(toggle_string, "block"))	
 		return NF_DROP;
 	else		
		return NF_ACCEPT;
}

static int dom_netfilter_open(struct inode *inode, struct file *file) {
    return 0;
}

static int dom_netfilter_release(struct inode *inode, struct file *file) {
    return 0;
}

static long dom_netfilter_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
	switch (cmd) {
	case IOCTL_FILTER_ADDRESS:
		if (copy_from_user(&ioctl_set_addr, (void *) arg, sizeof(ioctl_set_addr)))
			return -EFAULT;
		atomic_set(&ioctl_set, 1);
		break;

	default:
		return -ENOTTY; //indicates ioctl is not setup properly
	}

	return 0;
}

int __init domnet_init(void) {
	int err;
	dev_t dev;
	err = alloc_chrdev_region(&dev, 0, 1, "domnetfilter");
	
	//Find the major number of the device using the MAJOR() macro
	dev_major = MAJOR(dev);
	printk("domnetfilter: Doms Network Filter Started!");
	printk("domnetfilter: Major number %d", dev_major);
	
	atomic_set(&ioctl_set, 0);
	ioctl_set_addr = 0;

	cdev_init(&dom_cdev, &fops);
	cdev_add(&dom_cdev, MKDEV(dev_major, 0), 1);
	
	err = nf_register_net_hook(&init_net, &net_nfho);
	
	if (err)
		goto out;

	return 0;
	
out:
	// cleanup the device if init not successful
	printk("domnetfilter: Doms Network Filter Destroyed!");
	cdev_del(&dom_cdev);
	unregister_chrdev_region(MKDEV(dev_major, 0), 1);
	return err;
}

void __exit domnet_exit(void) {
	printk("domnetfilter: Doms Network Filter Destroyed!");
	nf_unregister_net_hook(&init_net, &net_nfho);
	cdev_del(&dom_cdev);
	unregister_chrdev_region(MKDEV(dev_major, 0), 1);
}

module_init(domnet_init);
module_exit(domnet_exit);

MODULE_DESCRIPTION("TCP netfilter module");
MODULE_AUTHOR("Dom");
MODULE_LICENSE("GPL");
