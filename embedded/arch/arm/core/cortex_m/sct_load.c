/**
 * @file      sct_load.c
 * @brief     gcc_compress.py 配套的配置变量定义
 * @note      该文件中的变量会被 gcc_compress.py 后处理修改
 **/

#include <sct_load.h>

/* 该结构体与gcc_compress.py配合使用，后处理阶段会修改其值 */
const sct_load_config_t __sct_load_config__ __attribute__((used, section(".sct_load_config"))) = {
    .magic = 0x12345678,
};
