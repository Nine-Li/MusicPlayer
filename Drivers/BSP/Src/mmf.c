#include "mmf.h"
#include "mmf_config.h"

// ---------- 内存池区域预留 ----------
// 在内部 SRAM1 顶部预留 MEM_TOTAL_SIZE 字节作为内存池。
//__attribute__((used, zero_init, section(".mmf_pool")))
//volatile uint8_t mmf_pool_reserve[MEM_TOTAL_SIZE];

// ---------- 辅助函数 ----------

// 将表索引转换为对应的内存地址
void *index_to_addr(uint32_t idx) 
{
    return (void *)(MEM_BASE_ADDR + MEM_TOTAL_SIZE - (idx + 1) * MEM_UNIT_SIZE);
}

// 将内存地址转换为表索引（若地址无效则返回 MEM_TOTAL_UNITS）
uint32_t addr_to_index(volatile uint8_t *addr) 
{
    uint32_t offset = (uint32_t)addr - MEM_BASE_ADDR;
    if (offset >= MEM_TOTAL_SIZE || offset % MEM_UNIT_SIZE) return MEM_TOTAL_UNITS;
    return (MEM_TOTAL_SIZE - offset) / MEM_UNIT_SIZE - 1;
}

// 查找从 addr 开始的下一个空闲单元的距离（最大63）
static uint8_t find_next_free_distance(volatile uint8_t *addr) 
{
    if (addr >= (uint8_t *)MEM_TABLE_END) return 0;

    for (uint8_t i = 1; i <= 63; i++) 
    {
        if (*(addr + i) == 0) return i;
        if (addr + i >= (uint8_t *)MEM_TABLE_END) return 0;
    }
    return 63;  // 超过63则返回最大值
}

// 标记一段连续表项为“占用块”，start 和 end 为表指针（包含 end）
static void mark_block_occupied(volatile uint8_t *start, volatile uint8_t *end) 
{
    // 块内非末尾项
    for (volatile uint8_t *p = start; p < end; p++) 
    {
        uint8_t dist_to_end = (uint8_t)(end - p);
        if (dist_to_end > 63) dist_to_end = 63;
        *p = 0x40 | dist_to_end;  // 高两位 01
    }
    // 末尾项：高两位 11，低位记录到下一个空闲块的距离
    uint8_t nextfree = find_next_free_distance(end);
    *end = 0xC0 | nextfree;
}

// 清空一个块的所有表项（从 start 到 end 含）
static uint8_t clear_block(volatile uint8_t *start, volatile uint8_t *end) 
{
    if(start > end) return 1;

    for (volatile uint8_t *p = start; p <= end; p++) 
    {
        *p = 0;
    }

    return 0;
}

/**
 * @brief   释放后向左刷新紧邻占用块链，使各块尾 nextfree 指向新空闲区起始 free_start，
 *          保证 mmf_malloc 中 0xC0 的 nextfree 跳跃不会跨过新产生的空闲洞。
 *          遇到空闲单元(0x00)或异常值即停止——空闲单元左侧的块本就已指向该空闲区。
 * @param   free_start:空闲块起始地址
 */
static void refresh_left_nextfree(volatile uint8_t *free_start)
{
    if((uint32_t)free_start == MEM_TABLE_START) return;     //位于表开头，退出

    volatile uint8_t *cur = free_start - 1;

    while ((uint32_t)cur >= MEM_TABLE_START)
    {
        uint8_t v = *cur;
        if ((v & 0xC0) != 0xC0) break;                          // 空闲单元或异常，停止
        
        uint32_t d = (uint32_t)free_start - (uint32_t)cur;      // 到新空闲区距离（>=1）
        if (d > 63) d = 63;                                     // nextfree 仅 6 位
        *cur = (v & 0xC0) | (uint8_t)d;

        // 跳过本块内部 0x40 单元，定位到左侧前一个块
        cur--;
        while ((uint32_t)cur >= MEM_TABLE_START && (*cur & 0xC0) == 0x40)
        {
            cur--;
        }
    }
}

// ---------- 公开接口 ----------

uint8_t mmf_init(void) 
{
	volatile uint8_t *mmf_table = (volatile uint8_t *)MEM_TABLE_START;
    
    if (MEM_TABLE_SIZE > UINT32_MAX - (MEM_UNIT_SIZE - 1)) return 1;

    uint32_t need = (MEM_TABLE_SIZE + MEM_UNIT_SIZE - 1) / MEM_UNIT_SIZE;
    uint32_t index = MEM_TABLE_SIZE - 1;

    for (uint32_t i = 0; i < MEM_TOTAL_UNITS; i++)
    {
        *(mmf_table + i) = 0x00;
    }   

    //分配表最后面预留个分配表自己，且最高两位置10(0x80)
    for(uint32_t i = 0; i < need; i++)
    {
        *(mmf_table + (index - i)) = 0x80;
    }

    return 0;
}

void *mmf_malloc(size_t size) 
{
    volatile uint8_t *mmf_table = (volatile uint8_t *)MEM_TABLE_START;
    
    if (size == 0) return NULL;
    // 计算需要的单元数（向上取整）

    if (size > UINT32_MAX - (MEM_UNIT_SIZE - 1)) return NULL;
    uint32_t need = (size + MEM_UNIT_SIZE - 1) / MEM_UNIT_SIZE;

    uint32_t i = 0;
    uint32_t count = 0;
    uint32_t start_idx = 0;

    while (i < MEM_TOTAL_UNITS) 
    {
        uint8_t val = mmf_table[i];
        if (val == 0) 
        {   // 空闲单元
            if (count == 0) start_idx = i;
            count++;
            if (count == need) 
            {
                // 找到足够连续空闲单元
                volatile uint8_t *start_ptr = mmf_table + start_idx;
                volatile uint8_t *end_ptr = mmf_table + i;
                mark_block_occupied(start_ptr, end_ptr);
                return index_to_addr(i);
            }
            i++;
        } 
        else 
        {   
            if ((val & 0xC0) == 0x80) 
				return NULL;
            uint8_t skip = val & 0x3F;
            i += (skip == 0 ? 1 : skip);  // 若低位为0，至少前进1
            count = 0;
        }
    }
    return NULL;
}

uint8_t mmf_free(void *addr) 
{
    volatile uint8_t *mmf_table = (volatile uint8_t *)MEM_TABLE_START;

    if (addr == NULL) return 1;
    uint32_t idx = addr_to_index(addr);
    if (idx >= MEM_TOTAL_UNITS) return 1;

    volatile uint8_t *end_ptr = mmf_table + idx;
    volatile uint8_t *start_ptr = end_ptr;
    uint8_t val = 0;

    if ((*end_ptr & 0xC0) != 0xC0) return 1;    //非分配表尾部，地址错误
	if ((*start_ptr & 0xC0) == 0x80) return 1;   // 侵入分配表所在区域

    // 找到该块的起始（分配表）位置（遍历直到遇到起始）
    while ((uint32_t)start_ptr != MEM_TABLE_START) 
    {
        start_ptr--;
        val = *start_ptr;
        if ((val & 0xC0) == 0xC0 || (val & 0xC0) == 0x00) 
        {
            start_ptr++;
            break;
        }   // 找到起始位置
    }

    // 清空该块
    if(clear_block(start_ptr, end_ptr)) return 1;

    // ---- 合并相邻空闲块 ----
    // 1. 向前查找前一个块（可能为空闲块）

    // ---- 合并相邻空闲块 ----
    if ((uint32_t)start_ptr > MEM_TABLE_START)
    {
        refresh_left_nextfree(start_ptr);
    }

    return 0;
}

//清除不完整的块
uint8_t mmf_fix_table(void)
{
    volatile uint8_t *mmf_table = (volatile uint8_t *)MEM_TABLE_START;
    uint32_t count = 0; //统计内存块单元数
    uint32_t start_index = 0;   //内存块开始指引

    for(uint32_t i = 0; i < MEM_TABLE_SIZE; i++)
    {
        if((mmf_table[i] & 0xC0) == 0x00)
        {
            if(count)
            {
                if(clear_block(mmf_table + start_index, mmf_table + (i - 1))) return 1;
                count = 0;

                volatile uint8_t *start_ptr = mmf_table + start_index;
                start_index = 0;
                refresh_left_nextfree(start_ptr);      
            }
        }
        else if((mmf_table[i] & 0xC0) == 0x40)
        {
            if(!count) start_index = i;
            count++;
        }
        else if((mmf_table[i] & 0xC0) == 0xC0)
        {
            count = 0;
            start_index = 0;
        }
        else if((mmf_table[i] & 0xC0) == 0x80)
        {
            if(count)
            {
                if(clear_block(mmf_table + start_index, mmf_table + (i - 1))) return 1;
                count = 0;

                volatile uint8_t *start_ptr = mmf_table + start_index;
                start_index = 0;
                refresh_left_nextfree(start_ptr);      
            }

            return 0;  //遇到写保护单元（分配表自身占用），退出
        } 
    }

    return 0;
}
