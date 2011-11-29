#ifndef _DNVME_DS_H_
#define _DNVME_DS_H_

#include <linux/cdev.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>

#include "dnvme_interface.h"

/* 0.0.01 */
#define    DRIVER_VERSION           0x00000001
#define    DRIVER_VERSION_STR(VER)  #VER

/* To store the max vector locations */
#define    MAX_VEC_SLT              2048
/*
 * Strucutre used to define all the essential parameters
 * related to PRP1, PRP2 and PRP List
 */
struct nvme_prps {
    u32 npages; /* No. of pages inside the PRP List */
    u32 type; /* refers to types of PRP Possible */
    /* List of virtual pointers to PRP List pages */
    __le64 **vir_prp_list;
    __u8 *vir_kern_addr; /* K.V.A for pinned down pages */
    __le64 prp1; /* Physical address in PRP1 of command */
    __le64 prp2; /* Physical address in PRP2 of command */
    dma_addr_t first_dma; /* First entry in PRP List */
    /* Size of data buffer for the specific command */
    u32 data_buf_size;
    /* Pointer to SG list generated */
    struct scatterlist *sg;
    /* Number of pages mapped to DMA area */
    __u32 dma_mapped_pgs;
    /* Address of data buffer for the specific command */
    unsigned long data_buf_addr;
    u8 data_dir; /* Flow of Data to/from device 1/0 */
};

/*
 * structure for the CQ tracking params with virtual address and size.
 */
struct nvme_trk_cq {
    u8          *vir_kern_addr; /* phy addr ptr to the q's alloc to kern mem*/
    dma_addr_t  cq_dma_addr;    /* dma mapped address using dma_alloc       */
    u32         size;           /* length in bytes of the alloc Q in kernel */
    u32 __iomem *dbs;           /* Door Bell stride                         */
    struct nvme_prps  prp_persist; /* PRP element in CQ                     */
    u8          contig;         /* Indicates if prp list is contig or not   */
    u8          bit_mask;       /* bitmask added for unique ID creation */
};

/*
 *    Structure definition for tracking the commands.
 */
struct cmd_track {
    struct list_head cmd_list_hd; /* link-list using the kernel list */
    u16    unique_id; /* driver assigned unique id for a particuler cmd. */
    u16    persist_q_id; /* Q ID used for Create/Delete Queues */
    u8     opcode; /* command opcode as per spec */
    enum   nvme_cmds   cmd_set; /* what cmd set does this opcode belong to */
    struct nvme_prps prp_nonpersist; /* Non persistent PRP entries */
};

/*
 * structure definition for SQ tracking parameters.
 */
struct nvme_trk_sq {
    void        *vir_kern_addr; /* virtual kernal address using kmalloc    */
    dma_addr_t  sq_dma_addr;    /* dma mapped address using dma_alloc      */
    u32         size;           /* len in bytes of allocated Q in kernel   */
    u32 __iomem *dbs;           /* Door Bell stride                        */
    struct nvme_prps  prp_persist; /* PRP element in CQ */
    struct list_head cmd_track_list; /* link-list head for cmd_track list  */
    u16         unique_cmd_id;  /* unique counter for each comand in SQ    */
    u8          contig;         /* Indicates if prp list is contig or not  */
    u8          bit_mask;       /* bitmask added for unique ID creation */
};

/*
 * Structure with Metrics of CQ. Has a node which makes it work with
 * kernel linked lists.
 */
struct metrics_cq {
    struct    list_head    cq_list_hd; /* link-list using the kernel list  */
    struct    nvme_gen_cq  public_cq;  /* parameters in nvme_gen_cq        */
    struct    nvme_trk_cq  private_cq; /* parameters in nvme_trk_cq        */
};

/*
 * Structure with Metrics of SQ. Has a node which makes it work with
 * kernel linked lists.
 */
struct metrics_sq {
    struct    list_head    sq_list_hd;  /* link-list using the kernel list  */
    struct    nvme_gen_sq  public_sq;   /* parameters in nvme_gen_sq        */
    struct    nvme_trk_sq  private_sq;  /* parameters in nvme_trk_sq        */
};

/*
 * Structure with cq track parameters for interrupt related functionality.
 */
struct irq_cq_track {
    struct  list_head   irq_cq_head;    /* linked list head for irq CQ trk   */
    u16     cq_id;                      /* Completion Q id                   */
    u8      isr_fired;                  /* flag to indicate if irq has fired */
    u32     isr_count;                  /* total no. of times irq fired      */
};

/*
 * Structure with parameters of IRQ vector, CQ track linked list and irq_no
 */
struct irq_track {
    struct  list_head   irq_list_hd;    /* list head for irq track list   */
    u16     irq_no;                     /* idx in list; always 0 based    */
    u16     int_vec;                    /* vec number; assigned by OS     */
    struct  list_head   irq_cq_track;   /* linked list of IRQ CQ nodes    */
};

/*
 * structure for meta data per device parameters.
 */
struct metrics_meta_data {
    struct list_head meta_trk_list;
    struct dma_pool *meta_dmapool_ptr;
    u16    meta_buf_size;
};

/*
 * Structure for meta data buffer allocations.
 */
struct metrics_meta {
    struct list_head meta_list_hd;
    u32         meta_id;
    void        *vir_kern_addr;
    dma_addr_t  meta_dma_addr;
};

/*
 * Structure with device related parameters.
 */
struct nvme_device {
    struct pci_dev  *pdev;           /* Pointer to the device in PCI space  */
    struct nvme_ctrl_reg __iomem *nvme_ctrl_space;  /* Pointer to reg space */
    struct dma_pool *prp_page_pool;  /* Mem for PRP List */
    u8  *bar_0_mapped;               /* Bar 0 IO re-mapped value            */
    struct device   *dmadev;         /* Pointer to the dma device from pdev */
    int minor_no;                    /* Minor no. of the device being used  */
    u8 open_flag;                    /* Allows device opening only once     */
    struct interrupts irq_active;    /* active irq vectors and irq type     */
};

/*
 * Work container which holds vectors and scheduled work queue item
 */
struct work_container {
    struct  work_struct sched_wq;     /* Work Struct item used in bh */
    u16     int_vec_ctx[MAX_VEC_SLT]; /* Array to indicate irq fired */
};

/*
 * Irq Processing structure to hold all the irq parameters per device.
 */
struct irq_processing {
    struct  mutex       irq_track_mtx;     /* Mutex for locking irq list    */
    struct  work_container wrk_sched;      /* work struct container for bh  */
    spinlock_t isr_spin_lock;              /* isr spin lock per device      */
    struct  list_head   irq_track_list;    /* IRQ list; sorted by irq_no    */
};

/*
 * Structure which defines the device list for all the data structures
 * that are defined.
 */
struct metrics_device_list {
    struct  list_head   metrics_device_hd; /* metrics linked list head      */
    struct  list_head   metrics_cq_list;   /* CQ linked list                */
    struct  list_head   metrics_sq_list;   /* SQ linked list                */
    struct  nvme_device *metrics_device;   /* Pointer to this nvme device   */
    struct  mutex       metrics_mtx;       /* Mutex for locking per device  */
    struct  metrics_meta_data metrics_meta; /* Pointer to meta data buff    */
    struct  irq_processing irq_process;    /* IRQ processing structure      */
};

/* extern device metrics linked list for exporting to project files */
extern struct metrics_driver g_metrics_drv;

/* Global linked list for the entire data structure for all devices. */
extern struct list_head metrics_dev_ll;

#endif
