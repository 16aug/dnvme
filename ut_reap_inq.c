#include <linux/kernel.h>
#include <linux/module.h>

#include "definitions.h"
#include "sysdnvme.h"
#include "dnvme_ds.h"
#include "dnvme_queue.h"
#include "ut_reap_inq.h"

#undef TEST_IO_SQ

/*
 * Function to set up completion q entries as if they are really placed by
 * the h/w.
 */
void unit_test_reap_inq(struct  metrics_device_list *pmetrics_device)
{
    struct metrics_cq  *pmetrics_cq_node;   /* ptr to cq node       */
    u8 *q_head_ptr;                         /* head ptr in cq       */
    struct cq_completion *cq_entry;         /* cq entry format      */
    u16 comp_entry_size;
    u16 num = 0;
    u16 tmpbit = 1;

    /* Lookup the CQ for which the reap inquiry is requested */
    list_for_each_entry(pmetrics_cq_node, &pmetrics_device->metrics_cq_list,
            cq_list_hd) {
        /* Test: CQ Empty, CQ ID = 1 */
        /* Empty already . */

        /* Test: CQ Full, CQ ID = 2 */
        if (pmetrics_cq_node->public_cq.q_id == 2) {
            comp_entry_size = (pmetrics_cq_node->private_cq.size) /
                                    (pmetrics_cq_node->public_cq.elements);
            /* point the head ptr to corresponding head ptr */
            q_head_ptr = pmetrics_cq_node->private_cq.vir_kern_addr +
                    (comp_entry_size * pmetrics_cq_node->public_cq.head_ptr);

            while (1) {
                cq_entry = (struct cq_completion *)q_head_ptr;
                cq_entry->phase_bit = 1;
                q_head_ptr += comp_entry_size;
                if (q_head_ptr == pmetrics_cq_node->private_cq.vir_kern_addr +
                                    pmetrics_cq_node->private_cq.size) {
                    break;
                }
            }
        }
        /* Test: CQ 10 Elements CQ ID = 3 */
        if (pmetrics_cq_node->public_cq.q_id == 3) {
            comp_entry_size = (pmetrics_cq_node->private_cq.size) /
                                    (pmetrics_cq_node->public_cq.elements);
            /* point the head ptr to corresponding head ptr */
            q_head_ptr = pmetrics_cq_node->private_cq.vir_kern_addr +
                    (comp_entry_size * pmetrics_cq_node->public_cq.head_ptr);
            num = 0;
            while (1) {
                cq_entry = (struct cq_completion *)q_head_ptr;
                cq_entry->phase_bit = 1;
                q_head_ptr += comp_entry_size;
                num++;
                if ((q_head_ptr == pmetrics_cq_node->private_cq.vir_kern_addr +
                                    pmetrics_cq_node->private_cq.size) ||
                                    (num == 10)) {
                    LOG_NRM("Num = %d", num);
                    break;
                }
            }
        }
        /* Test: CQ Wrapped around CQ ID = 4 */
        if (pmetrics_cq_node->public_cq.q_id == 4) {
            comp_entry_size = (pmetrics_cq_node->private_cq.size) /
                                    (pmetrics_cq_node->public_cq.elements);

            /* point the head ptr to corresponding head ptr */
            q_head_ptr = pmetrics_cq_node->private_cq.vir_kern_addr +
                    (comp_entry_size * pmetrics_cq_node->public_cq.head_ptr);
            LOG_DBG("CQ 4 hd ptr = %llx", (u64)q_head_ptr);
            num = 0;
            tmpbit = 1;
            while (1) {
                cq_entry = (struct cq_completion *)q_head_ptr;
                cq_entry->phase_bit = tmpbit;
                q_head_ptr += comp_entry_size;
                num++;
                if (q_head_ptr >= pmetrics_cq_node->private_cq.vir_kern_addr +
                                    pmetrics_cq_node->private_cq.size) {
                    /* Roll over */
                    q_head_ptr = pmetrics_cq_node->private_cq.vir_kern_addr;
                    tmpbit = 0;
                    LOG_DBG("Roll CQ 4 hd ptr = %llx", (u64)q_head_ptr);
                }

                if (num == 101) {
                    LOG_NRM("Entries in Q = %d", num);
                    break;
                }
            }
            /* Set Head Pointer in between the Q */
            pmetrics_cq_node->public_cq.head_ptr = 99;
        }
        /* Test: CQ tail_ptr is last element CQ ID = 5 */
        if (pmetrics_cq_node->public_cq.q_id == 5) {
            comp_entry_size = (pmetrics_cq_node->private_cq.size) /
                                    (pmetrics_cq_node->public_cq.elements);

            /* point the head ptr to corresponding head ptr */
            q_head_ptr = pmetrics_cq_node->private_cq.vir_kern_addr +
                    (comp_entry_size * pmetrics_cq_node->public_cq.head_ptr);
            LOG_DBG("CQ 5 hd ptr = %llx", (u64)q_head_ptr);
            num = 0;
            tmpbit = 1;
            while (1) {
                cq_entry = (struct cq_completion *)q_head_ptr;
                cq_entry->phase_bit = tmpbit;
                q_head_ptr += comp_entry_size;
                num++;
                if (q_head_ptr == pmetrics_cq_node->private_cq.vir_kern_addr +
                                    pmetrics_cq_node->private_cq.size) {
                    /* Roll over */
                    q_head_ptr = pmetrics_cq_node->private_cq.vir_kern_addr;
                    tmpbit = 0;
                }

                if (num == 101) {
                    LOG_NRM("Entries in Q = %d", num);
                    break;
                }
            }
            /* Set Head Pointer in between the Q */
            pmetrics_cq_node->public_cq.head_ptr = 98;
        }
        /* Test: CQ tail ptr is first elemnet CQ ID = 6 */
        if (pmetrics_cq_node->public_cq.q_id == 6) {
            comp_entry_size = (pmetrics_cq_node->private_cq.size) /
                                    (pmetrics_cq_node->public_cq.elements);

            /* point the head ptr to corresponding head ptr */
            q_head_ptr = pmetrics_cq_node->private_cq.vir_kern_addr +
                    (comp_entry_size * pmetrics_cq_node->public_cq.head_ptr);
            LOG_DBG("CQ 6 hd ptr = %llx", (u64)q_head_ptr);
            num = 0;
            tmpbit = 1;
            while (1) {
                cq_entry = (struct cq_completion *)q_head_ptr;
                cq_entry->phase_bit = tmpbit;
                q_head_ptr += comp_entry_size;
                num++;
                if (q_head_ptr == pmetrics_cq_node->private_cq.vir_kern_addr +
                                    pmetrics_cq_node->private_cq.size) {
                    /* Roll over */
                    q_head_ptr = pmetrics_cq_node->private_cq.vir_kern_addr;
                    tmpbit = 0;
                }

                if (num == 1) {
                    LOG_NRM("Entries in Q = %d", num);
                    break;
                }
            }
        }
    }
}

/*
 * function to submit data to SQ.
 */
void unit_test_mmap(struct  metrics_device_list *pmetrics_device)
{
    struct metrics_sq  *pmetrics_sq_node;   /* ptr to cq node       */
    struct metrics_cq  *pmetrics_cq_node;   /* ptr to cq node       */
    u64 *q_head_ptr;                         /* head ptr in cq       */
    u16 num = 0;

    /* Lookup the CQ for which the reap inquiry is requested */
    list_for_each_entry(pmetrics_sq_node, &pmetrics_device->metrics_sq_list,
            sq_list_hd) {
        /* Test: SQ Pattern 0xaa55, SQ ID = 0 */
        if (pmetrics_sq_node->public_sq.sq_id == 0) {
            /* point to corresponding head ptr */
            q_head_ptr = pmetrics_sq_node->private_sq.vir_kern_addr +
                    (64 * pmetrics_sq_node->public_sq.head_ptr);
            while (1) {
                *q_head_ptr = (0xaa55 + num);
                LOG_NRM("Addr:val-0x%llx:%llx", (u64)q_head_ptr,
                        (u64)*q_head_ptr);
                q_head_ptr++; /* increment to next location */
                num++;
                if (q_head_ptr == pmetrics_sq_node->private_sq.vir_kern_addr +
                                    pmetrics_sq_node->private_sq.size) {
                    /* Roll over */
                    q_head_ptr = pmetrics_sq_node->private_sq.vir_kern_addr;
                }
                if (num == 15) {
                    LOG_NRM("Entries in Q = %d", num);
                    break;
                }
            }
        }

#ifdef TEST_IO_SQ
        num = 0;
        /* Test: SQ Pattern 0xbb77, SQ ID = 1 */
        if (pmetrics_sq_node->public_sq.sq_id == 1) {
            /* point to corresponding head ptr */
            q_head_ptr = pmetrics_sq_node->private_sq.vir_kern_addr +
                    (64 * pmetrics_sq_node->public_sq.head_ptr);
            while (1) {
                *q_head_ptr = (0xbb77 + num);
                LOG_NRM("Addr:val-0x%llx:%llx", (u64)q_head_ptr,
                        (u64)*q_head_ptr);
                q_head_ptr++; /* increment to next location */
                num++;
                if (q_head_ptr == pmetrics_sq_node->private_sq.vir_kern_addr +
                                    pmetrics_sq_node->private_sq.size) {
                    /* Roll over */
                    q_head_ptr = pmetrics_sq_node->private_sq.vir_kern_addr;
                }
                if (num == 15) {
                    LOG_NRM("Entries in Q = %d", num);
                    break;
                }
            }
        }
#endif

    }

    num = 0;
    /* Lookup the CQ for which the reap inquiry is requested */
    list_for_each_entry(pmetrics_cq_node, &pmetrics_device->metrics_cq_list,
            cq_list_hd) {
        /* Test: CQ Pattern 0x1122, CQ ID = 0 */
        if (pmetrics_cq_node->public_cq.q_id == 0) {
            /* point to corresponding head ptr */
            q_head_ptr = (u64 *)(pmetrics_cq_node->private_cq.vir_kern_addr +
                    (16 * pmetrics_cq_node->public_cq.head_ptr));
            while (1) {
                *q_head_ptr = (0x3344 + num);
                LOG_NRM("Addr:val-0x%llx:%llx", (u64)q_head_ptr,
                        (u64)*q_head_ptr);
                q_head_ptr++; /* increment to next location */
                num++;
                if (q_head_ptr == (u64 *)(pmetrics_cq_node->private_cq.
                        vir_kern_addr + pmetrics_cq_node->private_cq.size)) {
                    /* Roll over */
                    q_head_ptr = (u64 *)pmetrics_cq_node->private_cq.
                            vir_kern_addr;
                }
                if (num == 25) {
                    LOG_NRM("Entries in Q = %d", num);
                    break;
                }
            }
        }
    }
}
