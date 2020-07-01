#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/bio.h>
#include <linux/blk-mq.h>
#include <linux/blk_types.h>
#include <linux/completion.h>
#include <linux/nvme_ioctl.h>
#include <linux/nvme.h>


//TODO: do we want FMODE_EXCL?
#define MY_BDEV_MODE (FMODE_READ | FMODE_WRITE)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("wnukowski@google.com");
MODULE_DESCRIPTION("A simple example Linux module.");
MODULE_VERSION("0.01");

union nvme_result {
		__le16	u16;
		__le32	u32;
		__le64	u64;
	};

struct nvme_request {
	struct nvme_command	*cmd;
	union nvme_result	result;
	u8			retries;
	u8			flags;
	u16			status;
	void	*ctrl;
};

static inline struct nvme_request *nvme_req(struct request *req)
{
	return blk_mq_rq_to_pdu(req);
}

static int __init lkm_example_init(void) {
  struct request *req = NULL;
  void * ret_buf = NULL;
  printk(KERN_INFO "Init!\n");
  // TODO: have a proper holder?
  struct block_device *bdev = blkdev_get_by_path("/dev/nvme0n1", MY_BDEV_MODE, NULL);
  if (IS_ERR(bdev))     {
    printk(KERN_ERR "No such block device. %ld\n", PTR_ERR(bdev));
    return -1;
  }

  struct gendisk *bd_disk;
  bd_disk = bdev->bd_disk;
  if (IS_ERR_OR_NULL(bd_disk)) {
    printk ("bd_disk is null?.\n");
    goto err;
  }
  const struct block_device_operations *fops;
  fops = bd_disk->fops;
  if (IS_ERR_OR_NULL(fops)) {
    printk ("fops is null?.\n");
    goto err;
  }
  printk ("Before call");
  int buff_size = sizeof(struct nvme_id_ctrl);
  ret_buf = kzalloc(buff_size, GFP_ATOMIC | GFP_KERNEL);
	if (!ret_buf) {
    printk ("Failed to malloc?.\n");
		goto err;
  }
  // this is a hack!!! But probably we do not need to use it.
  //set_fs(KERNEL_DS);

  struct nvme_command c;
  memset(&c, 0, sizeof(c));
  c.common.opcode =  0x06; // Identify
  c.common.cdw10[0] = cpu_to_le32(1); // IdentifyType = Controller

  struct nvme_id_ctrl* result = (struct nvme_id_ctrl*)ret_buf;

  // arg.opcode = 0x06; // Identify
	// arg.nsid = 0;
	// arg.addr = (unsigned long)result;
  // arg.data_len = buff_size;
	// arg.cdw10 = 1; // IdentifyType = Controller
	// struct nvme_passthru_cmd cmd;

  req = blk_mq_alloc_request(bdev->bd_queue, false, 0);
	if (IS_ERR(req)) {
    printk ("blk_mq_alloc_request failed?  %ld\n",  PTR_ERR(req));
		goto err;
  }
  req->cmd_flags |= REQ_FAILFAST_DRIVER;
  nvme_req(req)->cmd = &c;
  nvme_req(req)->retries = 0;
  nvme_req(req)->flags = 0;
  int ret = blk_rq_map_kern(bdev->bd_queue, req, ret_buf, buff_size, GFP_KERNEL);
  if (ret)
  {
    printk("blk_rq_map_kern failed?.\n");
    goto err;
  }
  struct bio *bio = req->bio;
	bio->bi_bdev = bdev;
  req->timeout = 60 * HZ; //hmm

  blk_execute_rq(req->q, bd_disk, req, 0);

  // int ioctrl_ret = (*fops->ioctl)(bdev, /*mode=*/0, NVME_IOCTL_ADMIN_CMD,
  //                                 /*arg=*/(unsigned long)&arg);
  //printk("ioctrl result. %d\n", ioctrl_ret);
  printk("status. %d\n", nvme_req(req)->status);
  printk("result. %lld\n", nvme_req(req)->result.u64);
  // should print 0x1AE0
  printk("vid. %d\n", result->vid);

err:
  if (ret_buf) {
    kfree(ret_buf);
  }
  if (req) {
    blk_mq_free_request(req);
  }
  return -1;
}
static void __exit lkm_example_exit(void) {

 printk(KERN_INFO "Done?\n");
}
module_init(lkm_example_init);
module_exit(lkm_example_exit);
