#ifndef __MMF_CONFIG_H
#define __MMF_CONFIG_H

//#define UINT32_MAX 0xffffffffU  /* 4294967295U */

// 内存池起始地址（表也放在这里）。
// 位于内部 SRAM1 顶部 10KB，由.sct 中的
// RW_MMFPOOL 区域（0x20019800..0x2001C000）预留
// 改动此值必须同步修改 .sct。
#define MEM_BASE_ADDR       0x2000C800u  // 建议字对齐
#define MEM_TOTAL_SIZE      0x0000F800u  // 内存池总大小（字节）

#define MEM_UNIT_SIZE       4            // 最小分配单元（字节），建议字对齐
#define MEM_TOTAL_UNITS     (MEM_TOTAL_SIZE / MEM_UNIT_SIZE)  // 总单元数

// 分配表占用的字节数（每个单元一个字节）
#define MEM_TABLE_SIZE      MEM_TOTAL_UNITS

// 表起始和结束地址（结束地址为最后一个表项地址）
#define MEM_TABLE_START     MEM_BASE_ADDR
#define MEM_TABLE_END       (MEM_TABLE_START + MEM_TABLE_SIZE - 1)

#endif
