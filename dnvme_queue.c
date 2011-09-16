#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

#include "definitions.h"
#include "sysdnvme.h"
#include "dnvme_reg.h"
#include "dnvme_queue.h"
#include "dnvme_ds.h"

/* structure for nvme queue */
struct nvme_queue *nvme_q;

/* device metrics linked list */
struct metrics_device_list *pmetrics_device_list;

u64 unique_cmd_id = 0;

/* Conditional compilation for QEMU related modifications. */
#ifdef QEMU
/*
* if QEMU is defined then we do 64 bit write in two 32 bit writes using
* writel's otherwise directly call writeq.
*/
static inline void WRITEQ(__u64 val, volatile void __iomem *addr)
{
    writel(val, addr);
    writel(val >> 32, addr + 4);
}
#else
static inline void WRITEQ(__u64 val, volatile void __iomem *addr)
{
    writeq(val, addr);
}
#endif

/*
*  jit_timer_fn - Timer handler routine which gets invoked when the
*  timer expires for the set Time out value.
*/
void jit_timer_fn(unsigned long arg)
{
    unsigned long *data = (unsigned long *)arg;
    *data = 0;
    LOG_NRM("Inside Timer Handler...");
}

/*
* nvme_ctrlrdy_capto - This function is used for checking if the controller
* is ready to process commands after CC.EN is set to 1. This will wait a
* min of CAP.TO seconds before failing.
*/
int nvme_ctrlrdy_capto(struct nvme_dev_entry *nvme_dev)
{
    u32 timer_delay;    /* Timer delay read from CAP.TO register          */
    unsigned long time_out_flag = 1;
    /* Time Out flag for timer handler                */
    struct timer_list asq_timer; /* asq timer declaration                  */

    /* As the TO is in lower 32 of 64 bit cap readl is good enough */
    timer_delay = readl(&nvme_dev->nvme_ctrl_space->cap) & NVME_TO_MASK;

    /* Modify TO as it is specified in 500ms units, timer needs in jiffies */
    timer_delay >>= 24;
    timer_delay *= NVME_MSEC_2_JIFFIES;

    init_timer(&asq_timer);

    /* register the Timer function */
    asq_timer.data     = (unsigned long)&time_out_flag;
    asq_timer.function = jit_timer_fn;
    asq_timer.expires  = timer_delay;

    LOG_NRM("Checking NVME Device Status (CSTS.RDY = 1)...");
    LOG_NRM("Timer Expires in %ld ms", asq_timer.expires);

    /* Add timer just before the status check */
    add_timer(&asq_timer);

    /* Check if the device status set to ready */
    while (!(readl(&nvme_dev->nvme_ctrl_space->csts) & NVME_CSTS_RDY)) {
        LOG_DBG("Waiting...");
        msleep(100);

        /* Check if the time out occured */
        if (time_out_flag == 0) {
            LOG_ERR("ASQ Setup Failed before Timeout");
            LOG_NRM("Check if Admin Completion Queue is Created First");

            /* Delete the timer once failed */
            del_timer(&asq_timer);

            /* set return invalid/failed */
            return -EINVAL;
        }
    }
    /* Timer function is done so delete before leaving*/
    del_timer(&asq_timer);

    LOG_NRM("NVME Controller is Ready to process commands");

    return SUCCESS;
}

/*
* nvme_queue_init - NVME Q initialization routine which initialized the
* queue parameters as per the user size.
*/
int nvme_queue_init(struct nvme_dev_entry *nvme_dev)
{
    LOG_NRM("Performing Queue Initializations...");

    /* Check if Q is allocated else do allocation */
    if (!nvme_q) {
        nvme_q = kzalloc(sizeof(struct nvme_queue), GFP_KERNEL);
        if (nvme_q == NULL) {
            LOG_ERR("Unable to alloc kern mem in queue initialization!!");
            return -ENOMEM;
        }
    }
    /* Assign DMA device in Queue structure */
    nvme_q->dmadev = &nvme_dev->pdev->dev;

    /* Initialize spin lock */
    spin_lock_init(&nvme_q->q_lock);

    /* Set init flag to done */
    nvme_q->q_init = 1;

    return SUCCESS;
}

/*
* nvme_ctrl_enable - NVME controller enable function.This will set the CAP.EN
* flag and this function which call the timer handler and check for the timer
* expiration. It returns success if the ctrl in rdy before timeout.
*/
int nvme_ctrl_enable(struct nvme_dev_entry *nvme_dev)
{
    u32 ctrl_config;

    /* Read Controller Configuration as we can only write 32 bits */
    ctrl_config = readl(&nvme_dev->nvme_ctrl_space->cc);

    /* BIT 0 is set to 1 i.e., CC.EN = 1 */
    ctrl_config |= 0x1;

    /* Write the enable bit into CC register */
    writel(ctrl_config, &nvme_dev->nvme_ctrl_space->cc);

   /* Check the Timeout flag */
    if (nvme_ctrlrdy_capto(nvme_dev) != SUCCESS) {
        LOG_ERR("Controller is not ready before time out");
        return -EINVAL;
    }

    return SUCCESS;
}

/*
* nvme_ctrl_disable - NVME controller disable function.This will reset the
* CAP.EN flag and this function which call the timer handler and check for
* the timer expiration. It returns success if the ctrl in rdy before timeout.
*/
int nvme_ctrl_disable(struct nvme_dev_entry *nvme_dev)
{
    u32 ctrl_config;

    /* Read Controller Configuration as we can only write 32 bits */
    ctrl_config = readl(&nvme_dev->nvme_ctrl_space->cc);

    /* BIT 0 is set to 0 i.e., CC.EN = 0 */
    ctrl_config &= ~0x1;

    /* Write the enable bit into CC register */
    writel(ctrl_config, &nvme_dev->nvme_ctrl_space->cc);

    /* Do clean up */
    /* Write the enable bit into CC register */
    writel(0, &nvme_dev->nvme_ctrl_space->cc);

    if (nvme_q != NULL) {
        dma_free_coherent(nvme_q->dmadev, nvme_q->asq_depth,
            (void *)nvme_q->virt_asq_addr, nvme_q->asq_dma_addr);
        dma_free_coherent(nvme_q->dmadev, nvme_q->acq_depth,
            (void *)nvme_q->virt_acq_addr, nvme_q->acq_dma_addr);
    } else {
        LOG_NRM("NVME Controller is not set yet");
        return -EINVAL;
    }

    return SUCCESS;
}
/*
* create_admn_sq - This routine is called when the driver invokes the ioctl for
* admn sq creation. It will automatically reset the NVME controller as it has to
* toggle the CAP.EN flag to set the parameters. The timer call is implemented in
* this function which will call the timer handler when the timer expires and ASQ
* could not be set. It returns success if the submission q creation is success
* after dma_coherent_alloc else returns failure at any step which fails.
*/
int create_admn_sq(struct nvme_dev_entry *nvme_dev, u16 qsize,
        struct  metrics_sq  *pmetrics_sq_list)
{
    u16 asq_id;     /* Admin Submission Q Id                          */
    u32 aqa;        /* Admin Q attributes in 32 bits size             */
    u32 tmp_aqa;    /* Temp var to hold admin q attributes            */

    LOG_NRM("Creating Admin Submission Queue...");

    /* Check if the q structure is initialized else init here */
    if (!nvme_q) {
        /* Call the initialization function */
        if (nvme_queue_init(nvme_dev) < 0) {
            LOG_ERR("NVME Q Init Failed");
            return -ENOMEM;
        }
    }

    /* As the Admin Q ID is always 0*/
    asq_id = 0;

    /*
     * Checking for overflow or underflow.
     * TODO: Update design doc.
     */
    if (qsize > MAX_AQ_ENTRIES || qsize == 0) {
        LOG_ERR("ASQ entries is more than MAX Q size or specified NULL");
        return -EINVAL;
    }

    /*
    * As the qsize send is in number of entries this computes the no. of bytes
    * computed.
    */
    nvme_q->asq_depth = qsize*sizeof(u8)*64;

    LOG_DBG("ASQ Depth: 0x%x", nvme_q->asq_depth);

    /*
     * The function dma_alloc_coherent  maps the dma address for ASQ which gets
     * the DMA mapped address from the kernel virtual address.
     */
    nvme_q->virt_asq_addr = dma_alloc_coherent(nvme_q->dmadev,
        nvme_q->asq_depth, &nvme_q->asq_dma_addr, GFP_KERNEL);
    if (!nvme_q->virt_asq_addr) {
        LOG_ERR("Unable to allocate DMA Address for ASQ!!");
    return -ENOMEM;
    }

    LOG_NRM("Virtual ASQ DMA Address: 0x%llx", (u64)nvme_q->virt_asq_addr);

    LOG_NRM("ASQ DMA Address: 0x%llx", (u64)nvme_q->asq_dma_addr);

    /* Set the door bell or ASQ to 0x1000 as per spec 1.0b */
    nvme_dev->dbs = ((void __iomem *)nvme_dev->nvme_ctrl_space) + NVME_SQ0TBDL;

    /* Read, Modify, Write  the aqa as per the q size requested */
    aqa = qsize & ASQS_MASK;
    tmp_aqa = readl(&nvme_dev->nvme_ctrl_space->aqa);
    tmp_aqa &= ~ASQS_MASK;
    aqa |= tmp_aqa;

    LOG_DBG("Mod Attributes from AQA Reg = 0x%x", tmp_aqa);
    LOG_NRM("AQA Attributes in ASQ:0x%x", aqa);

    /* Write new ASQ size using AQA */
    writel(aqa, &nvme_dev->nvme_ctrl_space->aqa);

    /* Write the DMA address into ASQ base address */
    WRITEQ(nvme_q->asq_dma_addr, &nvme_dev->nvme_ctrl_space->asq);

#ifdef DEBUG
    /* Debug statements */
    LOG_DBG("Admin CQ Base Address = 0x%x",
        (u32)readl(&nvme_dev->nvme_ctrl_space->acq));
    /* Read the AQA attributes after writing and check */
    tmp_aqa = readl(&nvme_dev->nvme_ctrl_space->aqa);

    LOG_NRM("Reading AQA after writing = 0x%x", tmp_aqa);

    /* Read the status register and printout to log */
    tmp_aqa = readl(&nvme_dev->nvme_ctrl_space->csts);

    LOG_NRM("Reading status reg = 0x%x", tmp_aqa);
#endif

    pmetrics_sq_list->private_sq.vir_kern_addr = nvme_q->virt_asq_addr;
    pmetrics_sq_list->private_sq.size = nvme_q->asq_depth;
    pmetrics_sq_list->private_sq.unique_cmd_id = unique_cmd_id++;
    /* returns success or failure*/
    return SUCCESS;
}

/*
* create_admn_cq - This routine is called when the driver invokes the ioctl for
* admn cq creation. It will automatically reset the NVME controller as it has to
* toggle the CAP.EN flag to set the parameters. The timer call is implemented in
* this function which will call the timer handler when the timer expires and ACQ
* could not be set. It returns success if the completion q creation is success
* after dma_coherent_alloc else returns failure at any step which fails.
*/
int create_admn_cq(struct nvme_dev_entry *nvme_dev, u16 qsize,
        struct  metrics_cq  *pmetrics_cq_list)
{

    int ret_code = SUCCESS; /* Ret code set to SUCCESS check for otherwise */
    u16 acq_id;          /* Admin Submission Q Id                         */
    u32 aqa;        /* Admin Q attributes in 32 bits size             */
    u32 tmp_aqa;        /* Temp var to hold admin q attributes            */

    LOG_NRM("Creating Admin Completion Queue...");

    /* Check if the q structure is initialized else init here */
    if (!nvme_q) {
        /* Call the initialization function */
        if (nvme_queue_init(nvme_dev) < 0) {
            LOG_ERR("NVME Q Init Failed");
            return -ENOMEM;
        }
    }

    /* As the Admin Q ID is always 0*/
    acq_id = 0;

    /*
     * Checking for overflow or underflow.
     * TODO: Update design doc.
     */
    if (qsize > MAX_AQ_ENTRIES || qsize == 0) {
        LOG_ERR("ASQ size is more than MAX Q size or specified NULL");
        return -EINVAL;
    }
    /*
    * As the qsize send is in number of entries this computes the no. of bytes
    * computed.
    */
    nvme_q->acq_depth = qsize*sizeof(u8)*16;
    LOG_DBG("ACQ Depth: 0x%x", nvme_q->acq_depth);
    /*
     * The function dma_alloc_coherent  maps the dma address for ACQ which gets
     * the DMA mapped address from the kernel virtual address.
     */
    nvme_q->virt_acq_addr = dma_alloc_coherent(nvme_q->dmadev,
        nvme_q->acq_depth, &nvme_q->acq_dma_addr, GFP_KERNEL);
    if (!nvme_q->virt_acq_addr) {
        LOG_ERR("Unable to allocate DMA Address for ACQ!!");
        return -ENOMEM;
    }

    LOG_NRM("Virtual ACQ DMA Address: 0x%llx", (u64)nvme_q->virt_acq_addr);

    LOG_NRM("ACQ DMA Address: 0x%llx", (u64)nvme_q->acq_dma_addr);

    /* Read, Modify and write the Admin Q attributes */
    aqa = qsize << 16;
    aqa &= ACQS_MASK;
    tmp_aqa = readl(&nvme_dev->nvme_ctrl_space->aqa);
    tmp_aqa &= ~ACQS_MASK;

    /* Final value to write to AQA Register */
    aqa |= tmp_aqa;

    LOG_DBG("Modified Attributes (AQA) = 0x%x", tmp_aqa);
    LOG_NRM("AQA Attributes in ACQ:0x%x", aqa);

    /* Write new ASQ size using AQA */
    writel(aqa, &nvme_dev->nvme_ctrl_space->aqa);

    /* Write the DMA address into ACQ base address */
    WRITEQ(nvme_q->acq_dma_addr, &nvme_dev->nvme_ctrl_space->acq);

#ifdef DEBUG
    /* Read the AQA attributes after writing and check */
    tmp_aqa = readl(&nvme_dev->nvme_ctrl_space->aqa);

    LOG_NRM("Reading AQA after writing in ACQ = 0x%x\n", tmp_aqa);

#endif

    pmetrics_cq_list->private_cq.vir_kern_addr = nvme_q->virt_acq_addr;
    pmetrics_cq_list->private_cq.size = nvme_q->acq_depth;

    /* returns success or failure*/
    return ret_code;
}

int nvme_alloc_sq(struct  metrics_sq  *pmetrics_sq_list,
            struct nvme_dev_entry *pnvme_dev)
{
    int ret_code = SUCCESS;
    u32 ctrl_config = 0;

    /* Check if the q structure is initialized else init here */
    if (!nvme_q) {
        /* Call the initialization function */
        if (nvme_queue_init(pnvme_dev) < 0) {
            LOG_ERR("NVME Q Init Failed");
            return -ENOMEM;
        }
    }
    /*Read Controller Configuration CC register at offset 0x14h. */
    ctrl_config = readl(&pnvme_dev->nvme_ctrl_space->cc);
    /* Extract the IOSQES from CC */
    ctrl_config = (ctrl_config >> 16) & 0xF;
    LOG_NRM("CC.IOSQES = 0x%x, 2^x = %d", ctrl_config, (1 << ctrl_config));

    pmetrics_sq_list->private_sq.size = pmetrics_sq_list->public_sq.elements *
            (1 << ctrl_config);

    /*
     * call dma_alloc_coherent or SQ which gets DMA mapped address from
     * the kernel virtual address.
     */
    nvme_q->virt_asq_addr = dma_alloc_coherent(nvme_q->dmadev,
            pmetrics_sq_list->private_sq.size,
            &nvme_q->asq_dma_addr, GFP_KERNEL);
    if (!nvme_q->virt_asq_addr) {
        LOG_ERR("Unable to allocate DMA Address for IO SQ!!");
        return -ENOMEM;
    }
    pmetrics_sq_list->private_sq.vir_kern_addr = nvme_q->virt_asq_addr;
    pmetrics_sq_list->private_sq.unique_cmd_id = unique_cmd_id++;

    return ret_code;
}
