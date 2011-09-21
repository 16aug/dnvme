
/*
 * q_type = 0 => CQ
 * q_type = 1 => SQ
 * g_id = 0 => Admin
 */
void ioctl_get_q_metrics(int file_desc, int q_id, int q_type, int size);

void ioctl_alloc_sq(int file_desc, uint16_t sq_id, uint16_t cq_id, uint16_t elem);
