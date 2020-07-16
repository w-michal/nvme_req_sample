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
#include <nvme_internal.h>

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
                                   struct nvme_command *cmd)
{
  struct request *req;

  req = blk_mq_alloc_request(q, nvme_is_write(cmd), 0);
  if (IS_ERR(req))
    return req;

  req->cmd_type = REQ_TYPE_DRV_PRIV;
  req->cmd = (unsigned char *)cmd;
  req->cmd_len = sizeof(struct nvme_command);
  req->errors = 0;

  return req;
}

static inline struct nvme_request *nvme_req(struct request *req)
{
  return blk_mq_rq_to_pdu(req);
}

int nvme_submit_user_cmd(struct gendisk *disk, struct request_queue *q, struct nvme_command *cmd,
                         void *buffer, unsigned bufflen, u32 *result, unsigned timeout)
{
  struct nvme_completion cqe;
  struct request *req;
  struct bio *bio = NULL;
  int ret;
  req = nvme_alloc_request(q, cmd);
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

    bio->bi_bdev = bdget_disk(disk, 0);
    if (!bio->bi_bdev)
    {
      printk("bdget_disk failed?.\n");
      ret = -ENODEV;
      goto out_unmap;
    }
  }

  printk("Before call");
  int req_res = blk_execute_rq(req->q, disk, req, 0);
  printk(KERN_INFO "req_res %d\n", req_res);
  printk(KERN_INFO "status %d\n",nvme_req(req)->status);
  printk(KERN_INFO "req flags %d\n", nvme_req(req)->flags);
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

static int __init lkm_example_init(void)
{
  void *ret_buf = NULL;
  struct nvme_command *ncmd = NULL;
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
	struct nvme_ns *ns = bdev->bd_disk->private_data;
  if (IS_ERR_OR_NULL(ns))
  {
    printk("nvme_ns is null?.\n");
    goto err;
  }

  int buff_size = sizeof(struct nvme_id_ctrl);
  ret_buf = kzalloc(buff_size, GFP_ATOMIC | GFP_KERNEL);
  if (!ret_buf)
  {
    printk("Failed to malloc?.\n");
    goto err;
  }

  ncmd = kzalloc (sizeof (struct nvme_command), GFP_KERNEL);

  memset(ncmd, 0, sizeof(&ncmd));
  ncmd->common.opcode = nvme_admin_identify;
  ncmd->identify.cns = cpu_to_le32(NVME_ID_CNS_CTRL);

  struct nvme_id_ctrl *result = (struct nvme_id_ctrl *)ret_buf;
  u32 code_result = 0;
  int submit_result = nvme_submit_user_cmd(bd_disk, ns->ctrl->admin_q, ncmd, ret_buf, buff_size, &code_result, 0);

  printk("submit_result. %d\n", submit_result);
  printk("result. %u\n", code_result);  // probably not usefull
  // should print 0x1AE0 (6880)
  printk("vid. %d\n", result->vid);

err:
  if (ncmd) {
     kfree(ncmd);
  }
  if (ret_buf)
  {
    kfree(ret_buf);
  }
  return -1;
}
static void __exit lkm_example_exit(void)
{

  printk(KERN_INFO "Done?\n");
}
module_init(lkm_example_init);
module_exit(lkm_example_exit);
