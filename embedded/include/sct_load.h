/**
 * @file      sct_load.h
 * @brief     gcc_compress.py配套的启动文件
 * @details   适配 C/C++ 混合编译
 * @copyright QISUI 2022
 * @author    qi.sui(mr.suiqi@qq.com)
 * @version   V1.1
 * @date      2022-03-13 20:24
 * @note      encode with utf8
 **/

#ifndef _SCT_LOAD_H
#define _SCT_LOAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOAD_CONFIG_MAGIC 0x20220315

typedef enum
{
    LOAD_TYPE_FIX  = 0,
    LOAD_TYPE_COPY = 1,
    LOAD_TYPE_RLEZ = 2,
    LOAD_TYPE_ZERO = 3,
} load_type_t;

#pragma pack(1)

typedef struct
{
    uint8_t *lma;  ///< 加载地址
    uint8_t *vma;  ///< 执行地址
    uint32_t len;  ///< 压缩后的长度
    uint8_t  type; ///< 加载类型
    uint8_t  reserve[3];
} sct_load_node_t;

typedef struct
{
    uint32_t         magic;
    sct_load_node_t *node_head;
    uint32_t         node_num;
    uint32_t         sum_check;
} sct_load_config_t;

#pragma pack()

/*
don't edit!
该结构体与gcc_compress.py配合使用
后处理阶段会修改该结构体的值以标记参数已写入
*/
extern const sct_load_config_t __sct_load_config__;

static inline void mem_copy(uint8_t *dest, uint8_t *src, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
    {
        dest[i] = src[i];
    }
}

static inline void mem_setzero(uint8_t *dest, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
    {
        dest[i] = 0;
    }
}

static inline void mem_rlez(uint8_t *dest, uint8_t *src, uint32_t len)
{
    uint32_t i, j;
    for (i = 0; i < len; i++)
    {
        if (src[i] == 0)
        {
            for (j = 0; j < src[i + 1] + 1; j++)
            {
                *dest = 0;
                dest++;
            }
            i++;
        }
        else
        {
            *dest = src[i];
            dest++;
        }
    }
}

static inline void sct_load(void)
{
    volatile sct_load_config_t *config = (sct_load_config_t *)&__sct_load_config__;
    uint32_t i;

    if ((config->magic != LOAD_CONFIG_MAGIC) ||
        (config->sum_check != (config->magic + (uint32_t)config->node_head + config->node_num)))
    {
        while (1)
            ;
    }

    for (i = 0; i < config->node_num; i++)
    {
        switch (config->node_head[i].type)
        {
        case LOAD_TYPE_COPY:
            mem_copy(config->node_head[i].vma, config->node_head[i].lma, config->node_head[i].len);
            break;
        case LOAD_TYPE_RLEZ:
            mem_rlez(config->node_head[i].vma, config->node_head[i].lma, config->node_head[i].len);
            break;
        case LOAD_TYPE_ZERO:
            mem_setzero(config->node_head[i].vma, config->node_head[i].len);
            break;
        default:
            while (1)
                ;
        }
    }
}

#ifdef __cplusplus
}
#endif

#endif /* _SCT_LOAD_H */
