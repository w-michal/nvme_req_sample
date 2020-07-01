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
  __le16 u16;
  __le32 u32;
  __le64 u64;
};

struct nvme_request
{
  struct nvme_command *cmd;
  union nvme_result result;
  u8 retries;
  u8 flags;
  u16 status;
  void *ctrl;
};

struct request *nvme_alloc_request(struct request_queue *q,
                                   struct nvme_command *cmd, unsigned int flags)
{
  struct request *req;

  req = blk_mq_alloc_request(q, nvme_is_write(cmd), flags);
  if (IS_ERR(req))
    return req;

  req->cmd_type = REQ_TYPE_DRV_PRIV;
  req->cmd_flags |= REQ_FAILFAST_DRIVER;
  req->cmd = (unsigned char *)cmd;
  req->cmd_len = sizeof(struct nvme_command);

  return req;
}

int nvme_submit_user_cmd(struct gendisk *disk, struct request_queue *q, struct nvme_command *cmd,
                         void *buffer, unsigned bufflen, u32 *result, unsigned timeout)
{
  struct nvme_completion cqe;
  struct request *req;
  struct bio *bio = NULL;
  int ret;

  req = nvme_alloc_request(q, cmd, 0);
  if (IS_ERR(req))
  {
    printk("nvme_alloc_request failed?.\n");
    return PTR_ERR(req);
  }

  req->timeout = timeout ? timeout : 60 * HZ;
  req->special = &cqe;

  if (buffer && bufflen)
  {
    ret = blk_rq_map_kern(q, req, buffer, bufflen,
                          GFP_KERNEL);
    if (ret)
    {
      printk("blk_rq_map_kern failed?.\n");
      goto out;
    }
    bio = req->bio;

    if (!disk)
    {
      printk("!disk?.\n");
      goto submit;
    }
    bio->bi_bdev = bdget_disk(disk, 0);
    if (!bio->bi_bdev)
    {
      printk("bdget_disk failed?.\n");
      ret = -ENODEV;
      goto out_unmap;
    }
  }
submit:
  printk("Before call");
  blk_execute_rq(req->q, disk, req, 0);
  printk(KERN_INFO "req->errors %d\n", req->errors);
  ret = req->errors;
  if (result)
    *result = le32_to_cpu(cqe.result);
out_unmap:
  if (bio)
  {
    if (disk && bio->bi_bdev)
      bdput(bio->bi_bdev);
  }
out:
  blk_mq_free_request(req);
  return ret;
}

static inline struct nvme_request *nvme_req(struct request *req)
{
  return blk_mq_rq_to_pdu(req);
}

static int __init lkm_example_init(void)
{
  struct request *req = NULL;
  void *ret_buf = NULL;
  printk(KERN_INFO "Init!\n");
  // TODO: have a proper holder?
  struct block_device *bdev = blkdev_get_by_path("/dev/nvme0n1", MY_BDEV_MODE, NULL);
  if (IS_ERR(bdev))
  {
    printk(KERN_ERR "No such block device. %ld\n", PTR_ERR(bdev));
    return -1;
  }

  struct gendisk *bd_disk;
  bd_disk = bdev->bd_disk;
  if (IS_ERR_OR_NULL(bd_disk))
  {
    printk("bd_disk is null?.\n");
    goto err;
  }
  const struct block_device_operations *fops;
  fops = bd_disk->fops;
  if (IS_ERR_OR_NULL(fops))
  {
    printk("fops is null?.\n");
    goto err;
  }

  int buff_size = sizeof(struct nvme_id_ctrl);
  ret_buf = kzalloc(buff_size, GFP_ATOMIC | GFP_KERNEL);
  if (!ret_buf)
  {
    printk("Failed to malloc?.\n");
    goto err;
  }
  // this is a hack!!! But probably we do not need to use it.
  //set_fs(KERNEL_DS);

  struct nvme_command c;
  memset(&c, 0, sizeof(c));
  c.common.opcode = 0x06;             // Identify
  c.common.cdw10[0] = cpu_to_le32(1); // IdentifyType = Controller

  struct nvme_id_ctrl *result = (struct nvme_id_ctrl *)ret_buf;
  u32 code_result = 0;
  int submit_result = nvme_submit_user_cmd(bd_disk, bdev->bd_queue, &c, ret_buf, buff_size, &code_result, 0);
  // arg.opcode = 0x06; // Identify
  // arg.nsid = 0;
  // arg.addr = (unsigned long)result;
  // arg.data_len = buff_size;
  // arg.cdw10 = 1; // IdentifyType = Controller
  // struct nvme_passthru_cmd cmd;

  // req = blk_mq_alloc_request(bdev->bd_queue, false, 0);
  // if (IS_ERR(req)) {
  //   printk ("blk_mq_alloc_request failed?  %ld\n",  PTR_ERR(req));
  // 	goto err;
  // }
  // req->cmd_flags |= REQ_FAILFAST_DRIVER;
  // nvme_req(req)->cmd = &c;
  // nvme_req(req)->retries = 0;
  // nvme_req(req)->flags = 0;
  // int ret = blk_rq_map_kern(bdev->bd_queue, req, ret_buf, buff_size, GFP_KERNEL);
  // if (ret)
  // {
  //   printk("blk_rq_map_kern failed?.\n");
  //   goto err;
  // }
  // struct bio *bio = req->bio;
  // bio->bi_bdev = bdget_disk(bd_disk, 0);
  // req->timeout = 60 * HZ; //hmm
  // req->special = &cqe;

  // blk_execute_rq(req->q, bd_disk, req, 0);

  // int ioctrl_ret = (*fops->ioctl)(bdev, /*mode=*/0, NVME_IOCTL_ADMIN_CMD,
  //                                 /*arg=*/(unsigned long)&arg);
  //printk("ioctrl result. %d\n", ioctrl_ret);
  printk("submit_result. %d\n", submit_result);
  printk("result. %u\n", code_result);
  // should print 0x1AE0
  printk("vid. %d\n", result->vid);

err:
  if (ret_buf)
  {
    kfree(ret_buf);
  }
  if (req)
  {
    blk_mq_free_request(req);
  }
  return -1;
}
static void __exit lkm_example_exit(void)
{

  printk(KERN_INFO "Done?\n");
}
module_init(lkm_example_init);
module_exit(lkm_example_exit);
