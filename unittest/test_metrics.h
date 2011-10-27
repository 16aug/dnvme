
/*
 * q_type = 0 => CQ
 * q_type = 1 => SQ
 * g_id = 0 => Admin
 */
void ioctl_get_q_metrics(int file_desc, int q_id, int q_type, int size);
void test_drv_metrics(int file_desc);
void ioctl_prep_sq(int file_desc, uint16_t sq_id, uint16_t cq_id, uint16_t elem, uint8_t contig);
void ioctl_prep_cq(int file_desc, uint16_t cq_id, uint16_t elem, uint8_t contig);
void ioctl_tst_ring_dbl(int file_desc, int sq_id);
void ioctl_create_prp_one_page(int file_desc);
void ioctl_create_prp_less_than_one_page(int file_desc);
void ioctl_create_prp_more_than_two_page(int file_desc);
void ioctl_create_list_of_prp(int file_desc);
void ioctl_create_fill_list_of_prp(int file_desc);
void ioctl_reap_inquiry(int file_desc, int cq_id);
void ioctl_reap_cq(int file_desc, int cq_id, int elements, int size, int display);
void ioctl_disable_ctrl(int file_desc, enum nvme_state new_state);
void ioctl_enable_ctrl(int file_desc);
void set_admn(int file_desc);
void ioctl_create_acq(int file_desc);
void ioctl_create_asq(int file_desc);
