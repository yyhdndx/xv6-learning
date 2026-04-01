// kernel/e1000_dev.h
#ifndef _E1000_DEV_H_
#define _E1000_DEV_H_

#define TX_RING_SIZE 16
#define RX_RING_SIZE 16

#include "types.h"
// --------------------
// TX/RX descriptors
// --------------------
struct tx_desc {
  uint64 addr;
  uint16 length;
  uint8  cso;
  uint8  cmd;
  uint8  status;
  uint8  css;
  uint16 special;
};

struct rx_desc {
  uint64 addr;
  uint16 length;
  uint16 csum;
  uint8  status;
  uint8  errors;
  uint16 special;
};

// --------------------
// MMIO register offsets
// --------------------
#define E1000_CTL      (0x00000/4)
#define E1000_TCTL     (0x00400/4)
#define E1000_TIPG     (0x00410/4)
#define E1000_RCTL     (0x00100/4)

#define E1000_TDBAL    (0x03800/4)
#define E1000_TDBAH    (0x03804/4)
#define E1000_TDLEN    (0x03808/4)
#define E1000_TDH      (0x03810/4)
#define E1000_TDT      (0x03818/4)

#define E1000_RDBAL    (0x02800/4)
#define E1000_RDBAH    (0x02804/4)
#define E1000_RDLEN    (0x02808/4)
#define E1000_RDH      (0x02810/4)
#define E1000_RDT      (0x02818/4)

#define E1000_IMS      (0x000D0/4)
#define E1000_ICR      (0x000C0/4)

// --------------------
// Bit definitions
// --------------------
#define E1000_CTL_RST          0x04000000

#define E1000_TCTL_EN          0x00000002
#define E1000_TCTL_PSP         0x00000008
#define E1000_CT_SHIFT         4
#define E1000_COLD_SHIFT       12

#define E1000_RCTL_EN          0x00000002
#define E1000_RCTL_BAM         0x00008000
#define E1000_RCTL_SECRC       0x04000000
#define E1000_RCTL_SZ_2048     0x00000000

#define E1000_TXD_CMD_EOP      0x01
#define E1000_TXD_CMD_RS       0x08
#define E1000_TXD_STAT_DD      0x01

#define E1000_RXD_STAT_DD      0x01

#define E1000_IMS_RXT0         0x00000080
#define E1000_IMS_RXDW         0x00000001

#endif