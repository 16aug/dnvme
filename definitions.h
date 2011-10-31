#ifndef _DEFINITIONS_H_
#define _DEFINITIONS_H_

#define SUCCESS              0
#define FAIL                -1
#define DEVICE_LIST_SIZE     20
#define CONFIG_PCI           1
#define NVME_DEV_INIT        0x3
/**
* @def PCI_CLASS_STORAGE_EXPRESS
* Set to value matching with NVME HW
*/
#define PCI_CLASS_STORAGE_EXPRESS    0x010802

#define NVME_MINORS            16

/**
* @def MAX_PCI_EXPRESS_CFG
* Maximum pcie config space.
*/
#define MAX_PCI_EXPRESS_CFG        0xFFF

/**
* @def MAX_PCI_CFG
* Maximum pci config space available
*/
#define MAX_PCI_CFG               0xFF

/**
* @def MAX_PCI_HDR
* Maximum pci header offset.
*/
#define MAX_PCI_HDR              0x3F

#define LOWER_16BITS             0xFFFF

/**
* @def CAP_REG
* Set to offset defined in NVME Spec 1.0b.
*/
#define CAP_REG                0x34

/**
* @def PMCS
* Set to offset defined in NVME Spec 1.0b.
*/
#define PMCS                   0x4

/**
* @def AER_ID_MASK
* Mask bits will extract last bit and that will help
* in determining what this bit corresponds to in terms of
* AER capability.
*/
#define AER_ID_MASK            0x1

/**
* @def AER_CAP_ID
* Indicate that this capability structure is an Advanced Error
* reporting capability.
*/
#define AER_CAP_ID             0x1

/**
 * @def MAX_METABUFF_SIZE
 * Indicates the Max Meta buff size allowed is 16KB
 */
#define MAX_METABUFF_SIZE       0x4000

#endif
