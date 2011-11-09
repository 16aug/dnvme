#ifndef _DNVME_INTERFACE_H_
#define _DNVME_INTERFACE_H_

/**
* These are the enum types used for branching to
* required offset as specified by either PCI space
* or a NVME space enum value defined here.
*/
enum nvme_io_space {
    NVMEIO_PCI_HDR,
    NVMEIO_BAR01,
    NVMEIO_FENCE    /* always must be the last element */
};

/**
* These are the enum types used for specifying the
* required access width of registers or memory space.
*/
enum nvme_acc_type {
    BYTE_LEN,
    WORD_LEN,
    DWORD_LEN,
    QUAD_LEN,
    ACC_FENCE
};

/**
* These enums define the type of interrupt scheme that the overall
* system uses.
*/
enum nvme_irq_type {
    INT_PIN,
    INT_MSI_SINGLE,
    INT_MSI_MULTI,
    INT_MSIX,
    INT_NONE,
    INT_FENCE    /* Last item to guard from loop run-overs */
};

/**
 * enums to define the q types.
 */
enum nvme_q_type {
    ADMIN_SQ,
    ADMIN_CQ,
};
/**
* This struct is the basic structure which has important
* parameter for the generic read  and write function to seek the correct
* offset and length while reading or writing to nvme card.
*/
struct rw_generic {
    enum nvme_io_space type;
    uint32_t  offset;
    uint32_t  nBytes;
    enum nvme_acc_type acc_type;
    uint8_t *buffer;
};

/**
* These enums are used while enabling or disabling or completely disabling the
* controller.
*/
enum nvme_state {
    ST_ENABLE,              /* Set the NVME Controller to enable state      */
    ST_DISABLE,             /* Controller reset without affecting Admin Q   */
    ST_DISABLE_COMPLETELY   /* Completely destroy even Admin Q's            */
};

/**
 * enum providing the definitions of the NVME commands.
 */
enum nvme_cmds {
    CMD_ADMIN,   /* Admin Command Set               */
    CMD_NVME,    /* NVME Command Set                */
    CMD_AON,     /* AON  Command Set                */
    CMD_FENCE,   /* last element for loop over-run  */
};

/* Enum specifying bitmask passed on to IOCTL_SEND_64B */
enum send_64b_bitmask {
    MASK_PRP1_PAGE = 1, /* PRP1 can point to a physical page */
    MASK_PRP1_LIST = 2, /* PRP1 can point to a PRP list */
    MASK_PRP2_PAGE = 4, /* PRP2 can point to a physical page */
    MASK_PRP2_LIST = 8, /* PRP2 can point to a PRP list */
    MASK_MPTR = 16, /* MPTR may be modified */
};

/**
* This struct is the basic structure which has important parameter for
* sending 64 Bytes command to both admin  and IO SQ's and CQ's
*/
struct nvme_64b_send {
    /* BIT MASK for PRP1,PRP2 and Metadata pointer */
    enum send_64b_bitmask bit_mask;
    uint32_t data_buf_size; /* Size of Data Buffer */
    /* Data Buffer or Discontiguous CQ/SQ's user space address */
    uint8_t const *data_buf_ptr;
    uint8_t *cmd_buf_ptr; /* Virtual Address pointer to 64B command */
    enum nvme_cmds cmd_set; /* Command set for the cmd_buf command */
    uint32_t meta_buf_id; /* Meta buffer ID when MASK_MPTR is set */
    uint16_t q_id; /* Queue ID where the cmd_buf command should go */
    uint8_t data_dir; /* Direction of DMA mapped memory 1/0 to/from device */
};

/**
 * This structure defines the overall interrupt scheme used and
 * defined parameters to specify the driver version and application
 * version. A verification is performed by driver and application to
 * check if these versions match.
 */
struct metrics_driver {
    uint32_t    driver_version;         /* dnvme driver version              */
    uint32_t    api_version;            /* tnvme test application version    */
};

/**
 * This structure defines the parameters required for creating any CQ.
 * It supports both Admin CQ and IO CQ.
 */
struct nvme_gen_cq {
    uint16_t    q_id;        /* even admin q's are supported here q_id = 0   */
    uint16_t    tail_ptr;    /* The value calculated for respective tail_ptr */
    uint16_t    head_ptr;    /* Actual value in CQxTDBL for this q_id        */
    uint16_t    elements;    /* pass the actual elements in this q           */
    uint8_t     pbit_new_entry; /* Indicates if a new entry is in CQ         */
};

/**
 * This structure defines the parameters required for creating any SQ.
 * It supports both Admin SQ and IO SQ.
 */
struct nvme_gen_sq {
    uint16_t    sq_id;    /* Admin SQ are supported with q_id = 0            */
    uint16_t    cq_id;    /* The CQ ID to which this SQ is associated        */
    uint16_t    tail_ptr;    /* Acutal value in SQxTDBL for this SQ id       */
    uint16_t    tail_ptr_virt; /* future SQxTDBL write value based on no.
        of new cmds copied to SQ */
    uint16_t    head_ptr;    /* Calculate this value based on cmds reaped    */
    uint16_t    elements;    /* total number of elements in this Q           */
};

/**
 * enum for metrics type. These enums are used when returning the device
 * metrics.
 */
enum metrics_type {
    METRICS_CQ,     /* Completion Q Metrics     */
    METRICS_SQ,     /* Submission Q Metrics     */
    MTERICS_FENCE,  /* Always last item.        */
};

/**
  * Interface structure for returning the Q metrics. The buffer is where the
  * data is stored for the user to copy from. This assumes that the user will
  * provide correct buffer space to store the required metrics.
  */
struct nvme_get_q_metrics {
    uint16_t    q_id;       /* Pass the Q id for which metrics is desired   */
    enum        metrics_type    type;   /* SQ or CQ metrics desired         */
    uint32_t    nBytes;     /* Number of bytes to copy into buffer          */
    uint8_t     *buffer;    /* to store the required data.                  */
};

/**
 * Interface structure for creating Admin Q's. The elements is a 1 based value.
 */
struct nvme_create_admn_q {
    enum        nvme_q_type     type;   /* Admin q type, ASQ or ACQ.    */
    uint16_t    elements;               /* No. of elements of size 64 B */
};

/**
 * Interface structure for allocating SQ memory. The elements are 1 based
 * values and the CC.IOSQES is 2^n based.
 */
struct nvme_prep_sq {
    uint16_t    elements;   /* Total number of entries that need kernal mem */
    uint16_t    sq_id;      /* The user specified unique SQ ID              */
    uint16_t    cq_id;      /* Existing or non-existing CQ ID.              */
    uint8_t     contig;     /* Indicates if SQ is contig or not, 1 = contig */
};

/**
 * Interface structure for allocating CQ memory. The elements are 1 based
 * values and the CC.IOSQES is 2^n based.
 */
struct nvme_prep_cq {
    uint16_t    elements;   /* Total number of entries that need kernal mem */
    uint16_t    cq_id;      /* Existing or non-existing CQ ID.              */
    uint8_t     contig;     /* Indicates if SQ is contig or not, 1 = contig */
};

/**
 * Interface structure for getting the metrics structure into a user file.
 * The filename and location are specified thought file_name parameter.
 */
struct nvme_file {
    uint16_t    flen; /* Length of file name, it is not the total bytes */
    const char *file_name; /* location and file name to copy metrics   */
};

/**
 * Interface structure for reap inquiry ioctl. It works well for both admin
 * and IO Q's.
 */
struct nvme_reap_inquiry {
    uint16_t    q_id;           /* CQ ID to reap commands for             */
    uint16_t    num_remaining;  /* return no of cmds waiting to be reaped */
};

/**
 * Interface structure for reap ioctl. Admin Q and all IO Q's are supported.
 */
struct nvme_reap {
    uint16_t q_id;          /* CQ ID to reap commands for             */
    uint16_t elements;      /* Get the no. of elements to be reaped   */
    uint16_t num_remaining; /* return no. of cmds waiting for this cq */
    uint16_t num_reaped;    /* retrun no. of elements reaped          */
    uint16_t size;          /* Size of buffer to fill data to         */
    uint8_t  *buffer;       /* Buffer to copy reaped data             */
};

/**
 * Format of general purpose nvme command DW0-DW9
 */
struct nvme_gen_cmd {
    uint8_t   opcode;
    uint8_t   flags;
    uint16_t  command_id;
    uint32_t  nsid;
    uint64_t  rsvd2;
    uint64_t  metadata;
    uint64_t  prp1;
    uint64_t  prp2;
};

/**
 * Specific structure for Create CQ command
 */
struct nvme_create_cq {
    uint8_t   opcode;
    uint8_t   flags;
    uint16_t  command_id;
    uint32_t  rsvd1[5];
    uint64_t  prp1;
    uint64_t  rsvd8;
    uint16_t  cqid;
    uint16_t  qsize;
    uint16_t  cq_flags;
    uint16_t  irq_vector;
    uint32_t  rsvd12[4];
};

/**
 * Specific structure for Create SQ command
 */
struct nvme_create_sq {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t command_id;
    uint32_t rsvd1[5];
    uint64_t prp1;
    uint64_t rsvd8;
    uint16_t sqid;
    uint16_t qsize;
    uint16_t sq_flags;
    uint16_t cqid;
    uint32_t rsvd12[4];
};

/**
 * Specific structure for Delete Q command
 */
struct nvme_del_q {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t command_id;
    uint32_t rsvd1[9];
    uint16_t qid;
    uint16_t rsvd10;
    uint32_t rsvd11[5];
};
#endif
