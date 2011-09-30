#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/unistd.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

#include "dnvme_ioctls.h"
#include "dnvme_interface.h"
#include "definitions.h"
#include "dnvme_reg.h"
#include "sysfuncproto.h"
#include "sysdnvme.h"
#include "dnvme_sts_chk.h"
#include "dnvme_queue.h"
#include "dnvme_cmds.h"
#include "dnvme_ds.h"

/* initialize the linked list headers */
LIST_HEAD(metrics_sq_ll);
LIST_HEAD(metrics_cq_ll);
LIST_HEAD(sq_cmd_ll);


/*
*  device_status_chk  - Generic error checking function
*  which checks error registers and set kernel
*  alert if a error is detected.
*/
int device_status_chk(struct pci_dev *pdev, int *status)
{
    /* Local variable declaration. */
    u16 data; /* Unsigned 16 bit data. */
    int ret_code = -EINVAL; /* initialize ret code to invalid */
    /*
    * Pointer for user data to be copied to user space from
    * kernel space. Initialize with user passed data pointer.
    */
    int __user *datap = (int __user *)status;

    /*
    * Read a word (16bit value) from the configuration register
    * and pass it to user.
    */
    ret_code = pci_read_config_word(pdev, PCI_DEVICE_STATUS, &data);
    /*
    * Check the return code to know if pci read is success.
    */
    if (ret_code < 0) {
        LOG_ERR("pci_read_config failed in driver error check");
        return -EINVAL;
    }

    /*
    * Get the Device Status from the PCI Header.
    */
    *status = device_status_pci(data);

    /* Print out to kernel log the device status */
    if (*status == SUCCESS) {
        LOG_NRM("PCI Device Status SUCCESS (STS)");
    } else {
        LOG_ERR("PCI Device Status FAIL (STS)");
    }

    *status = (*status == SUCCESS) ? device_status_next(pdev) : FAIL;

    /* Print out to kernel log the device status */
    if (*status == SUCCESS) {
        LOG_NRM("NEXT Capability Status SUCCESS.");
    } else {
        LOG_ERR("NEXT Capabilty status FAIL");
    }

    *status = (*status == SUCCESS) ? nvme_controller_status(pdev) : FAIL;
    /* Print out to kernel log the device status */
    if (*status == SUCCESS) {
        LOG_NRM("NVME Controller Status SUCCESS (CSTS)");
    } else {
        LOG_ERR("iNVME Controller Status FAIL (CSTS)");
    }

    /*
    *  Efficient way to copying data to user buffer datap
    *  using in a single copy function call.
    *  First parameter is copy to user buffer,
    *  second parameter is copy from location,
    *  third parameter give the number of bytes to copy.
    */
    ret_code = copy_to_user(status, datap, sizeof(status));

    return ret_code;
}

/*
*   driver_genric_read - Generic Read functionality for reading
*   NVME PCIe registers and memory mapped address
*/
int driver_generic_read(struct file *file, struct rw_generic *nvme_data,
   struct pci_dev *pdev)
{
    /* Local variable declaration. */
    u16 index; /* index for looping till the end. */
    int ret_code = -EINVAL; /* to verify if return code is success. */
    struct nvme_space nvme_ctrl_reg_space;
    struct nvme_dev_entry *nvme = NULL;
    unsigned char __user *datap = (unsigned char __user *)nvme_data->buffer;

    LOG_DBG("Inside Generic Read Function of the IOCTLs");

    nvme = kzalloc(sizeof(struct nvme_dev_entry), GFP_KERNEL);
    if (nvme == NULL) {
        LOG_ERR("Unable to allocate kernel mem in generic read");
        return -ENOMEM;
    }

    /*
    *  Switch based on the user passed read type using the
    *  enum type specified in struct rw_generic.
    */
    switch (nvme_data->type) {
    case NVMEIO_PCI_HDR: /* Switch case for NVME PCI Header type. */

        LOG_DBG("User App request to read  the PCI Header Space");
        LOG_DBG("Read request for bytes = 0x%x", nvme_data->nBytes);
        LOG_DBG("off:acc= 0x%x:0x%2x", nvme_data->offset, nvme_data->acc_type);
        /*
        * Loop through the number of bytes that are specified in the
        * bBytes parameter.
        */
        for (index = 0; index < nvme_data->nBytes;) {
            /* Check where you are reading */
            LOG_DBG("Reading for index = 0x%x", index);
            LOG_DBG("PCI Offset = 0x%x", nvme_data->offset + index);

            if ((nvme_data->offset + index) > MAX_PCI_EXPRESS_CFG) {
                LOG_ERR("Offset is more than the PCI Express");
                LOG_ERR("Extended config space...");
                return -EINVAL;
            }
            /*
            * Check the access width and access the PCI space as per
            * the requested width.
            */
            if ((nvme_data->acc_type == DWORD_LEN)
                    && (nvme_data->nBytes % 4 == 0)) {
                /* Read dword from the PCI register space. */
                ret_code = pci_read_config_dword(pdev,
                (nvme_data->offset + index), (u32 *)&datap[index]);
                    LOG_NRM("Reading PCI offset, data = 0x%x, 0x%x",
                        (nvme_data->offset + index), (u32)datap[index]);

                /* increment by dword size */
                index += 4;
            } else if ((nvme_data->acc_type == WORD_LEN)
                        && (nvme_data->nBytes % 2 == 0)) {
                /* Read a word from the PCI register space. */
                ret_code = pci_read_config_word(pdev,
                (nvme_data->offset + index), (u16 *)&datap[index]);
                LOG_NRM("Reading PCI offset, data = 0x%x, 0x%x",
                    (nvme_data->offset + index), (u16)datap[index]);

                /* increment by word size */
                index += 2;
            } else if (nvme_data->acc_type == BYTE_LEN) {
                /* Read a byte from the PCI register space. */
                ret_code = pci_read_config_byte(pdev,
                    (nvme_data->offset + index), (u8 *) &datap[index]);
                LOG_NRM("Reading PCI offset, data = 0x%x, 0x%x",
                    (nvme_data->offset + index), (u8)datap[index]);

                /* increment by byte size */
                index++;
            } else {
                LOG_ERR("PCI space accessed by DWORD, WORD or BYTE");
                LOG_ERR("Wrong PCI access width specified or");
                LOG_ERR("Wrong no. of bytes specified.");
                return -EINVAL;
            }

            /* Check if reading is successful */
            if (ret_code < 0) {
                LOG_ERR("pci_read_config failed");
                return ret_code;
            }
        }
        /* done required reading then break and return.     */
        break;

    case NVMEIO_BAR01:
        /* Registers are aligned and so */
        LOG_DBG("Invoking User App request to read from NVME space");

        /*
        * Checking for 4 bytes boundary. If either nBytes or offset is not
        * 4 bytes aligned return error.
        */
        if ((nvme_data->acc_type == DWORD_LEN) &&
                (((nvme_data->nBytes % 4) != 0) ||
                ((nvme_data->offset % 4) != 0))
           ) {
            LOG_ERR("Offset or nBytes is not DWORD Aligned");
            LOG_ERR("Provide them on 4 bytes Boundary");
            return -EINVAL;
        } else if ((nvme_data->acc_type == QUAD_LEN) &&
                (((nvme_data->nBytes % 8) != 0) ||
                ((nvme_data->offset % 4) != 0))
                ) {
            /*
             * Only nbytes need to be QUAD aligned and offset may be
             * DWORD or QUAD aligned.
             */
            LOG_ERR("Offset is not DWORD Aligned");
            LOG_ERR("nbytes is not QUAD Aligned");
            LOG_ERR("Provide them on 8 bytes Boundary");
            return -EINVAL;
        } else if ((nvme_data->acc_type == WORD_LEN) &&
                  ((nvme_data->nBytes % 2) != 0)) {
            LOG_ERR("nBytes is not WORD aligned");
            return -EINVAL;
        }

        /* Remap io mem for this device. */
        nvme->bar0mapped = ioremap(pci_resource_start(pdev, 0),
                               pci_resource_len(pdev, 0));

        /* Check if remap was success */
        if (!nvme->bar0mapped) {
            LOG_ERR("Unable to map io region nvme..exit");
            return -EINVAL;
        }

        LOG_NRM("Using Bar0 address: 0x%llx, length 0x%x",
            (uint64_t)nvme->bar0mapped, (int) pci_resource_len(pdev, 0));

        /* Assign the BAR remapped into nvme space control register */
        nvme_ctrl_reg_space.bar_dev = (void __iomem *)nvme->bar0mapped;

        /* Read NVME register space. */
        ret_code = read_nvme_reg_generic(
                              nvme_ctrl_reg_space, datap, nvme_data->nBytes,
                         nvme_data->offset, nvme_data->acc_type);

        if (ret_code < 0) {
            LOG_ERR("Read NVME Space failed");
            return -EINVAL;
        }
        /* done with nvme space reading break from this case .*/
        break;

    default:
        LOG_DBG("Could not find switch case using default");
    }

    /*
    *  Efficient way to copying data to user buffer datap
    *  using in a single copy function call.
    *  First parameter is copy to user buffer,
    *  second parameter is copy from location,
    *  third parameter give the number of bytes to copy.
    */
    ret_code = copy_to_user(&nvme_data->buffer[0], datap,
        nvme_data->nBytes * sizeof(u8));
    if (ret_code < 0) {
        LOG_ERR("Error copying to user buffer returning");
    }

    return ret_code;
}

/*
*   driver_generic_write - Generic write function for
*   NVME PCIe registers and memory mapped address
*/
int driver_generic_write(struct file *file, struct rw_generic *nvme_data,
    struct pci_dev *pdev)
{
    u16 index = 0; /* Index to loop */
    int ret_code = -EINVAL; /* return code to verify if written success. */

    struct nvme_space l_ctrl_reg_space;
    struct nvme_dev_entry *nvme_dev = NULL;

    /*
    * Pointer for user data to be copied to user space from
    * kernel space. Initialize with user passed data pointer.
    */
    unsigned char __user *datap = (unsigned char __user *)nvme_data->buffer;

    LOG_DBG("Inside Generic write Funtion of the IOCTLs");

    nvme_dev = kzalloc(sizeof(struct nvme_dev_entry), GFP_KERNEL);
    if (nvme_dev == NULL) {
        LOG_ERR("Unable to allocate kernel mem in generic read");
        LOG_ERR("Exiting from here...");
        return -ENOMEM;
    }

    /* allocate kernel memory to datap that is requested from user app */
    datap = kzalloc((nvme_data->nBytes * sizeof(u8)) , GFP_KERNEL);

    /*
    * Check if allocation of memory is not null else return
    * no memory.
    */
    if (!datap) {
        LOG_ERR("Unable to allocate kernel memory in driver generic write");
        return -ENOMEM;
    }

    /*
    * copy from user data buffer to kernel data buffer at single place
    * using copy_from_user for efficiency.
    */
    copy_from_user((u8 *)datap, (u8 *)nvme_data->buffer,
        nvme_data->nBytes * sizeof(u8));

    /*
    * Switch based on the type of requested write determined by nvme_data->data
    */
    switch (nvme_data->type) {
    case NVMEIO_PCI_HDR: /* Switch case for NVME PCI Header type. */

        LOG_DBG("Invoking User App request to write the PCI Header Space");
        /*
        * Loop through the number of bytes that are specified in the
        * bBytes parameter.
        */
        for (index = 0; index < nvme_data->nBytes; index++) {
            /*
            * write user data to pci config space at location
            * indicated by (offset + index) as per access width.
            */
            if ((nvme_data->acc_type == DWORD_LEN)
                && (nvme_data->nBytes % 4 == 0)) {
                /* Write a word to PCI register space. */
                ret_code = pci_write_config_dword(pdev,
                    (nvme_data->offset + index), datap[index]);
                LOG_NRM("Writing to PCI offset, data = 0x%x, 0x%x",
                    (nvme_data->offset + index), (u32)datap[index]);
                /* increment by dword size */
                index += 4;
            } else if ((nvme_data->acc_type == WORD_LEN)
                && (nvme_data->nBytes % 2 == 0)) {
                /* Write a word to PCI register space. */
                ret_code = pci_write_config_word(pdev,
                    (nvme_data->offset + index), datap[index]);
                LOG_NRM("Writing to PCI offset, data = 0x%x, 0x%x",
                    (nvme_data->offset + index), (u16)datap[index]);
                /* increment by word size */
                index += 2;
            } else if (nvme_data->acc_type == BYTE_LEN) {
                /* Write a byte from to PCI register space. */
                ret_code = pci_write_config_byte(pdev,
                    (nvme_data->offset + index), datap[index]);

                LOG_NRM("Writing to PCI offset, data = 0x%x, 0x%x",
                    (nvme_data->offset + index), (u8)datap[index]);
                /* increment by byte size */
                index++;
            } else {
                LOG_ERR("PCI space accessed by DWORD, WORD or BYTE");
                LOG_ERR("Wrong PCI access width specified or ");
                LOG_ERR("Wrong no of bytes specified");
                return -EINVAL;
            }
            /* Check if reading is successful */
            if (ret_code < 0) {
                LOG_ERR("pci_read_config failed");
                LOG_ERR("Unable to write to location = 0x%x data = 0%x",
                    (nvme_data->offset + index), datap[index]);
                return ret_code;
            }
        }
        /* Done writing user requested data, returning. */
        break;
    case NVMEIO_BAR01:
        LOG_DBG("Invoking User App request to write NVME Space using BAR01");

        /*
        * Checking for 4 bytes boundary. If either nBytes or offset is not
        * 4 bytes aligned return error.
        */
        if ((nvme_data->acc_type == DWORD_LEN) &&
                (((nvme_data->nBytes % 4) != 0) ||
                ((nvme_data->offset % 4) != 0))
            ) {
                LOG_ERR("Either Offset or nBytes is not DWORD Aligned");
                LOG_ERR("Provide them on 4 bytes Boundary");
                return -EINVAL;
            } else if ((nvme_data->acc_type == QUAD_LEN) &&
                (((nvme_data->nBytes % 8) != 0) ||
                ((nvme_data->offset % 4) != 0))
                ) {
                LOG_ERR("Either Offset or nBytes is not QUAD Aligned");
                LOG_ERR("Provide them on 8 bytes Boundary");
                return -EINVAL;
            } else if ((nvme_data->acc_type == WORD_LEN) &&
                ((nvme_data->nBytes % 2) != 0)) {
                LOG_ERR("nBytes is not WORD aligned");
                return -EINVAL;
            }

        /* Remap io mem for this device. */
        nvme_dev->bar0mapped = ioremap(pci_resource_start(pdev, 0),
            pci_resource_len(pdev, 0));

        /* Check if remap was success */
        if (!nvme_dev->bar0mapped) {
            LOG_ERR("Unable to map io region nvme..exit");
            return -EINVAL;
        }

        LOG_NRM("Using Bar0 address: 0x%llx, length 0x%x",
            (uint64_t)nvme_dev->bar0mapped, (int) pci_resource_len(pdev, 0));

        /* Assign the BAR remapped into nvme space control register */
        l_ctrl_reg_space.bar_dev = (void __iomem *)nvme_dev->bar0mapped;

        /*
         * Write NVME register space with datap from offset until
         * nBytes are written.
         */
        ret_code = write_nvme_reg_generic(l_ctrl_reg_space, (u8 *)datap,
            nvme_data->nBytes, nvme_data->offset, nvme_data->acc_type);

        /* done with nvme space writing break from this case .*/
        break;

    default:
        LOG_DBG("Could not find switch case using default");
    }

    return ret_code;
}

/*
*   driver_create_asq - Driver Admin Submission Queue creation routine
*   from Q create ioctl.
*/
int driver_create_asq(struct nvme_create_admn_q *create_admn_q,
        struct nvme_device *pnvme_dev)
{
    int ret_code = -EINVAL; /* Set return value to error        */
    u8 admn_id = 0;         /* Always admin ID is 0             */
    struct  metrics_sq  *pmetrics_sq_list;  /* SQ linked list   */

    /*
     * Search if admin sq already exists.
     */
    LOG_NRM("Searching for Node in the sq_list_hd...");
    list_for_each_entry(pmetrics_sq_list, &metrics_sq_ll, sq_list_hd) {
        if (admn_id == pmetrics_sq_list->public_sq.sq_id) {
            LOG_DBG("ASQ already exists");
            return -EINVAL;
        }
    }

    LOG_NRM("Alloc mem for a node...");
    pmetrics_sq_list = kmalloc(sizeof(struct metrics_sq), GFP_KERNEL);
    if (pmetrics_sq_list == NULL) {
        LOG_ERR("failed mem alloc in ASQ creation.");
        return -ENOMEM;
    }
    /* Reset the data for this node */
    memset(pmetrics_sq_list, 0, sizeof(struct metrics_sq));

    /* Set Admin Q Id. */
    pmetrics_sq_list->public_sq.sq_id = admn_id;
    pmetrics_sq_list->public_sq.elements = create_admn_q->elements;
    /* Admin SQ is always associated with Admin CQ. */
    pmetrics_sq_list->public_sq.cq_id = admn_id;

    /* Call dma allocation, creation of contiguous memory for ASQ */
    ret_code = create_admn_sq(pnvme_dev, pmetrics_sq_list->public_sq.elements,
            pmetrics_sq_list);
    if (ret_code != SUCCESS) {
        LOG_ERR("Failed Admin Q creation!!");
    }

    LOG_NRM("Adding node for Admin SQ to the list.");
    /* Add an element to the end of the list */
    list_add_tail(&pmetrics_sq_list->sq_list_hd, &metrics_sq_ll);
    /*
     * Set the pointer in metrics device to point to this element
     * for this_device.
     */
    if (!pmetrics_device_list->metrics_sq_list) {
        pmetrics_device_list->metrics_sq_list = pmetrics_sq_list;
    }
    return ret_code;
}

/*
*  driver_create_acq - Driver Admin Completion Queue creation routine
*  from Q create ioctl.
*/
int driver_create_acq(struct nvme_create_admn_q *create_admn_q,
        struct nvme_device *pnvme_dev)
{
    int ret_code = -EINVAL; /* return value is invalid        */
    u8 admn_id = 0;         /* Always Admin ID is zero.       */
    struct  metrics_cq  *pmetrics_cq_list;  /* CQ linked list */

    /*
     * Search if admin sq already exists.
     */
    LOG_NRM("Searching for Node in the cq_list_hd");
    /* Get the device from the linked list */
    list_for_each_entry(pmetrics_cq_list, &metrics_cq_ll, cq_list_hd) {
        if (admn_id == pmetrics_cq_list->public_cq.q_id) {
            LOG_DBG("ACQ already exists");
            return -EINVAL;
        }
    }
    LOG_NRM("Alloc mem for a Admin CQ node.");
    pmetrics_cq_list = kmalloc(sizeof(struct metrics_cq), GFP_KERNEL);
    if (pmetrics_cq_list == NULL) {
        LOG_ERR("failed mem alloc in ACQ creation.");
        return -ENOMEM;
    }
    /* Reset the data for this node */
    memset(pmetrics_cq_list, 0, sizeof(struct metrics_cq));

    /* Set Admin CQ Id. */
    pmetrics_cq_list->public_cq.q_id = admn_id;
    pmetrics_cq_list->public_cq.elements = create_admn_q->elements;
    LOG_NRM("Adding node for Admin CQ to the list.");

    /* Call dma allocation, creation of contiguous memory for ACQ */
    ret_code = create_admn_cq(pnvme_dev, pmetrics_cq_list->public_cq.elements,
            pmetrics_cq_list);

    if (ret_code != SUCCESS) {
        LOG_ERR("Admin CQ creation failed!!");
        return -EINVAL;
    }
    /* Add an element to the end of the list */
    list_add_tail(&pmetrics_cq_list->cq_list_hd, &metrics_cq_ll);
    /*
     * Set the pointer in metrics device to point to this element
     * for this_device.
     */
    if (!pmetrics_device_list->metrics_cq_list) {
        pmetrics_device_list->metrics_cq_list = pmetrics_cq_list;
    }
    LOG_DBG("Admin CQ successfully created and added to linked list...");
    return ret_code;
}
/*
*  driver_iotcl_init - Driver Initialization routine before starting to
*  issue  ioctls.
*/
int driver_ioctl_init(struct nvme_dev_entry *nvme_dev, struct pci_dev *pdev,
        struct metrics_device_list *pmetrics_device_list)
{
    struct device *dmadev; /* DMA device structure */

    LOG_DBG("Inside driver IOCTL init function");
    LOG_NRM("Initializing the BAR01 and NVME Controller Space");

    /* Remap io mem for this device. */
    nvme_dev->bar0mapped = ioremap(pci_resource_start(pdev, 0),
                          pci_resource_len(pdev, 0));
    /* Check if remap was success */
    if (!nvme_dev->bar0mapped) {
        LOG_ERR("IOCTL init failed");
        return -EINVAL;
    }

    /* Assign the 64bit address mapped to controller space in nvme_dev */
    nvme_dev->nvme_ctrl_space = (void __iomem *)nvme_dev->bar0mapped;

    /* Set the init flag to initialization done flag. */
    nvme_dev->init_flag = NVME_DEV_INIT;

    /* Set the pci device of the nvme_dev to point to pdev from ioctl */
    nvme_dev->pdev = pdev;

    /* Used to create Coherent DMA mapping for PRP List */
    dmadev = &nvme_dev->pdev->dev;
    nvme_dev->prp_page_pool = dma_pool_create("prp page", dmadev,
        PAGE_SIZE, PAGE_SIZE, 0);
    if (NULL == nvme_dev->prp_page_pool) {
        LOG_ERR("Creation of DMA Pool failed");
        return -ENOMEM;
    }
    /* Allocate mem fo nvme device with kernel memory */
    pmetrics_device_list->pnvme_device = kmalloc(sizeof(struct nvme_device),
            GFP_KERNEL);
    if (pmetrics_device_list->pnvme_device == NULL) {
        LOG_ERR("failed mem allocation for device.");
        return -ENOMEM;
    }

    /* Populate Metrics device list with this device */
    pmetrics_device_list->pnvme_device->pdev = pdev;
    pmetrics_device_list->pnvme_device->bar_0_mapped =
            nvme_dev->bar0mapped;
    pmetrics_device_list->pnvme_device->nvme_ctrl_space =
            (void __iomem *)nvme_dev->bar0mapped;
    pmetrics_device_list->pnvme_device->dmadev =
            &nvme_dev->pdev->dev;
    pmetrics_device_list->pnvme_device->device_no = 1;

    /*
     * IRQ initialization is with no interrupts. To determine if device uses
     * interrupt then, this gets updated in device status function where it
     * checks for device capabilities.
     */
    g_metrics_drv.irq = INT_NONE;

    LOG_NRM("IOCTL Init Success:Reg Space Location:  0x%llx",
        (uint64_t)nvme_dev->nvme_ctrl_space);

    return SUCCESS;
}

/*
*  driver_send_64b - Routine for sending 64 bytes command into
*  admin/IO SQ/CQ's
*/
int driver_send_64b(struct nvme_dev_entry *nvme_dev,
    struct nvme_64b_send *nvme_64b_send)
{
    /* ret code to verify status of sending 64 bytes command */
    int ret_code = -EINVAL;
    /* TODO: make the function more generic while implementing complete IOCTL */
    ret_code = submit_command(nvme_dev, nvme_64b_send->queue_id,
        nvme_64b_send->data_buf_ptr, nvme_64b_send->data_buf_size);
    return ret_code;
}


/*
* driver_default_ioctl - Default if none of the switch
* in ioctl gets called.
*/
int driver_default_ioctl(struct file *file, unsigned long buffer,
    size_t length)
{
    return 0;
}
/*
 * nvme_get_q_metrics will return the q metrics from the global data
 * structures if the q_id send down matches any q_id for this device.
 * If the Q id does not exist in the list then it returns error.
 * This function also returns error when kernel cannot allocate for
 * at-least one element memory of public_sq or public_cq.
 */
int nvme_get_q_metrics(struct nvme_get_q_metrics *get_q_metrics)
{
    int ret_code = SUCCESS;
    u16 q_id;               /* tmp variable for q id          */
    struct  metrics_sq  *pmetrics_sq_list;  /* SQ linked list */
    struct  metrics_cq  *pmetrics_cq_list;  /* CQ linked list */

    u8 __user *datap = (u8 __user *)get_q_metrics->buffer;
                                            /* lcl usr buff  */
    /* Get the q_id to lcl var */
    q_id = get_q_metrics->q_id;

    /* Determine the type of Q for which the metrics was needed */
    if (get_q_metrics->type == METRICS_SQ) {
        /* Determine the SQ Metrics */
        LOG_DBG("SQ Metrics requested.");
        /* Check if Q was admin Q? */
        if (get_q_metrics->q_id == 0) {
            LOG_DBG("Admin SQ Metrics...");
         } else {
            LOG_DBG("IO SQ Metrics...");
        }
        /* Get the device from the linked list */
        list_for_each_entry(pmetrics_sq_list, &metrics_sq_ll, sq_list_hd) {
            /* Check if the Q Id matches */
            if (q_id == pmetrics_sq_list->public_sq.sq_id) {
                LOG_NRM("SQ_ID = %d is found in the list...",
                    pmetrics_sq_list->public_sq.sq_id);
                LOG_DBG("SQ Elements = %d",
                              pmetrics_sq_list->public_sq.elements);
                if (get_q_metrics->nBytes < sizeof(struct nvme_gen_sq)) {
                    LOG_ERR("Not sufficient buffer size to copy SQ metrics");
                    return -EINVAL;
                }
                /* Copy to user space linked pointer buffer */
                memcpy((u8 *)&datap[0], (u8 *)&pmetrics_sq_list->public_sq,
                        sizeof(struct nvme_gen_sq));
                /* Copy data to user space */
                ret_code = copy_to_user(&get_q_metrics->buffer[0], datap,
                        sizeof(struct nvme_gen_sq));
                return ret_code;
            }
        }
        LOG_DBG("SQ_ID = %d not found in the list", q_id);
        return -EINVAL;
    } else if (get_q_metrics->type == METRICS_CQ) {
        /* Determine the CQ Metrics */
        LOG_DBG("CQ Metrics requested.");
        /* Check if Q was admin Q? */
        if (get_q_metrics->q_id == 0) {
            LOG_DBG("Admin CQ Metrics..");
        } else {
            LOG_DBG("IO CQ Metrics...");
        }
        /* Get the device from the linked list */
        list_for_each_entry(pmetrics_cq_list, &metrics_cq_ll, cq_list_hd) {
            /* check if a q id matches in the list */
            if (q_id == pmetrics_cq_list->public_cq.q_id) {
                LOG_DBG("CQ_ID = %d is found in the list...",
                    pmetrics_cq_list->public_cq.q_id);
                LOG_DBG("CQ Elements = %d",
                              pmetrics_cq_list->public_cq.elements);
                if (get_q_metrics->nBytes < sizeof(struct nvme_gen_cq)) {
                    LOG_ERR("Not sufficient buffer size to copy CQ metrics");
                    return -EINVAL;
                }
                /* Copy to user space linked pointer buffer */
                memcpy((u8 *)&datap[0], (u8 *)&pmetrics_cq_list->public_cq,
                        sizeof(struct nvme_gen_cq));
                /* Copy data to user space */
                ret_code = copy_to_user(&get_q_metrics->buffer[0], datap,
                        get_q_metrics->nBytes * sizeof(u8));
                return ret_code;
            }
        }
        LOG_DBG("CQ_ID = %d not found in the list", q_id);
        return -EINVAL;
    } else {
        /* The Q type is not SQ or CQ, so error out */
        LOG_ERR("Error in metrics Type...");
        LOG_ERR("Metrics Type: METRICS_SQ/METRICS_CQ only");
        return -EINVAL;
    }
    return SUCCESS;
}

/*
 * identify_unique - This routine checkes if the q_id requested is already
 * in the linked list. If present then it will return error. If its not in
 * the list this returns success.
 */
int identify_unique(u16 q_id, enum metrics_type type)
{
    struct  metrics_sq  *pmetrics_sq_list;  /* SQ linked list */
    struct  metrics_cq  *pmetrics_cq_list;  /* CQ linked list */

    /* Determine the type of Q for which the metrics was needed */
    if (type == METRICS_SQ) {
        /* Get the device from the linked list */
        list_for_each_entry(pmetrics_sq_list, &metrics_sq_ll, sq_list_hd) {
            /* Check if the Q Id matches */
            if (q_id == pmetrics_sq_list->public_sq.sq_id) {
                LOG_ERR("SQ ID is not unique. SQ_ID = %d already created.",
                    pmetrics_sq_list->public_sq.sq_id);
                return -EINVAL;
            }
        }
    } else if (type == METRICS_CQ) {
        list_for_each_entry(pmetrics_cq_list, &metrics_cq_ll, cq_list_hd) {
            /* check if a q id matches in the list */
            if (q_id == pmetrics_cq_list->public_cq.q_id) {
                LOG_DBG("CQ ID is not unique. CQ_ID = %d already created.",
                    pmetrics_cq_list->public_cq.q_id);
                return -EINVAL;
            }
        }
    }
    return SUCCESS;
}

/*
 * driver_nvme_prep_sq - This function will try to allocate kernel
 * space for the corresponding unique SQ. If the sq_id is duplicated
 * this will return error to the caller. If the kernel memory is not
 * available then fail and return NOMEM error code.
 */
int driver_nvme_prep_sq(struct nvme_prep_sq *prep_sq,
        struct nvme_device *pnvme_dev)
{
    int ret_code = SUCCESS;
    struct  metrics_sq  *pmetrics_sq_list;  /* SQ linked list */

    ret_code = identify_unique(prep_sq->sq_id, METRICS_SQ);
    if (ret_code != SUCCESS) {
        LOG_ERR("SQ ID is not unique.");
        return ret_code;
    }

    LOG_DBG("Allocating SQ node in linked list.");
    pmetrics_sq_list = kmalloc(sizeof(struct metrics_sq), GFP_KERNEL);
    if (pmetrics_sq_list == NULL) {
        LOG_ERR("failed allocating kernel memory in SQ allocation.");
        return -ENOMEM;
    }
    /* Set the pointer in metrics device to point to this element */
    if (!pmetrics_device_list->metrics_sq_list) {
        pmetrics_device_list->metrics_sq_list = pmetrics_sq_list;
    }

    /* Reset the data for this node */
    memset(pmetrics_sq_list, 0, sizeof(struct metrics_sq));

    /* Filling the data elements of sq metrics. */
    pmetrics_sq_list->public_sq.sq_id = prep_sq->sq_id;
    pmetrics_sq_list->public_sq.cq_id = prep_sq->cq_id;
    pmetrics_sq_list->public_sq.elements = prep_sq->elements;
    pmetrics_sq_list->private_sq.contig = prep_sq->contig;

    ret_code = nvme_prepare_sq(pmetrics_sq_list, pnvme_dev);

    if (ret_code < 0) {
        LOG_ERR("Failed nvme_prep_sq call!!");
        return ret_code;
    }

#ifdef DEBUG
    LOG_NRM("Printing IO SQ Details After Success:");
    LOG_NRM("Public Elements:");
    LOG_NRM("\tIO SQ ID = %d", pmetrics_sq_list->public_sq.sq_id);
    LOG_NRM("\tAssociated CQ ID = %d", pmetrics_sq_list->public_sq.cq_id);
    LOG_NRM("\telements = %d", pmetrics_sq_list->public_sq.elements);
    LOG_NRM("\tHead Ptr = %d", pmetrics_sq_list->public_sq.head_ptr);
    LOG_NRM("\tTail Ptr = %d", pmetrics_sq_list->public_sq.tail_ptr);
    LOG_NRM("\tVirt Tail Ptr = %d", pmetrics_sq_list->public_sq.tail_ptr_virt);
    LOG_NRM("Private Elements:");
    LOG_NRM("\tvir_kern_addr = 0x%llx",
            (u64)pmetrics_sq_list->private_sq.vir_kern_addr);
    LOG_NRM("\tContiguous = %d", pmetrics_sq_list->private_sq.contig);
    LOG_NRM("\tSize Allocated = %d", pmetrics_sq_list->private_sq.size);
    LOG_NRM("\tDBS= 0x%llx", (u64)pmetrics_sq_list->private_sq.dbs);
#endif

    /* Add this element to the end of the list */
    list_add_tail(&pmetrics_sq_list->sq_list_hd, &metrics_sq_ll);
    return ret_code;
}

/*
 * driver_nvme_prep_cq - This function will try to allocate kernel
 * space for the corresponding unique CQ. If the cq_id is duplicated
 * this will return error to the caller. If the kernel memory is not
 * available then fail and return NOMEM error code.
 */
int driver_nvme_prep_cq(struct nvme_prep_cq *prep_cq,
        struct nvme_device *pnvme_dev)
{
    int ret_code = SUCCESS;
    struct  metrics_cq  *pmetrics_cq_list;  /* CQ linked list */

    ret_code = identify_unique(prep_cq->cq_id, METRICS_CQ);
    if (ret_code != SUCCESS) {
        LOG_ERR("CQ ID is not unique.");
        return ret_code;
    }

    LOG_DBG("Allocating CQ node in linked list.");
    pmetrics_cq_list = kmalloc(sizeof(struct metrics_cq), GFP_KERNEL);
    if (pmetrics_cq_list == NULL) {
        LOG_ERR("failed allocating kernel memory in CQ allocation.");
        return -ENOMEM;
    }
    /* Set the pointer in metrics device to point to this element */
    if (!pmetrics_device_list->metrics_cq_list) {
        pmetrics_device_list->metrics_cq_list = pmetrics_cq_list;
    }

    /* Reset the data for this node */
    memset(pmetrics_cq_list, 0, sizeof(struct metrics_cq));

    /* Filling the data elements of sq metrics. */
    pmetrics_cq_list->public_cq.q_id = prep_cq->cq_id;
    pmetrics_cq_list->public_cq.elements = prep_cq->elements;
    pmetrics_cq_list->private_cq.contig = prep_cq->contig;

    ret_code = nvme_prepare_cq(pmetrics_cq_list, pnvme_dev);

    if (ret_code < 0) {
        LOG_ERR("Failed nvme_prep_cq call!!");
        return ret_code;
    }

#ifdef DEBUG
    LOG_NRM("Printing IO CQ Details After Success:");
    LOG_NRM("Public Elements:");
    LOG_NRM("\tIO CQ ID = %d", pmetrics_cq_list->public_cq.q_id);
    LOG_NRM("\telements = %d", pmetrics_cq_list->public_cq.elements);
    LOG_NRM("\tHead Ptr = %d", pmetrics_cq_list->public_cq.head_ptr);
    LOG_NRM("\tTail Ptr = %d", pmetrics_cq_list->public_cq.tail_ptr);
    LOG_NRM("Private Elements:");
    LOG_NRM("\tvir_kern_addr = 0x%llx",
            (u64)pmetrics_cq_list->private_cq.vir_kern_addr);
    LOG_NRM("\tContiguous = %d", pmetrics_cq_list->private_cq.contig);
    LOG_NRM("\tSize Allocated = %d", pmetrics_cq_list->private_cq.size);
    LOG_NRM("\tDBS= 0x%llx", (u64)pmetrics_cq_list->private_cq.dbs);
#endif

    /* Add this element to the end of the list */
    list_add_tail(&pmetrics_cq_list->cq_list_hd, &metrics_cq_ll);
    return ret_code;
}

/*
 * free_allqs - This will clear the allocated linked list for the SQs
 * and CQs including the admin Q's
 */
void free_allqs(void)
{
    struct  metrics_sq  *pmetrics_sq_list;  /* SQ linked list */
    struct  metrics_cq  *pmetrics_cq_list;  /* CQ linked list */
#if DEBUG
    /* Get the device from the linked list and release */
    list_for_each_entry(pmetrics_sq_list, &metrics_sq_ll, sq_list_hd) {
        LOG_DBG("SQ_ID = %d", pmetrics_sq_list->public_sq.sq_id);
    }
    /* Get the device from the linked list and release */
    list_for_each_entry(pmetrics_cq_list, &metrics_cq_ll, cq_list_hd) {
        LOG_DBG("CQ_ID = %d", pmetrics_cq_list->public_cq.q_id);
    }
#endif
    LOG_DBG("Deleting SQ and CQ linked lists");
    list_del(&metrics_sq_ll);
    list_del(&metrics_cq_ll);
}

