#include "irq.h"
#include "stdint.h"

void Reset_Handler() {

}

const uintptr_t _irq_vector_table[150] = {

    ((uintptr_t)&Reset_Handler),

};