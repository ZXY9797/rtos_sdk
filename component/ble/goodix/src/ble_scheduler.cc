extern "C" {
#include "gr55xx_pwr.h"
}

extern "C" void ble_scheduler_run(void)
{
    pwr_mgmt_schedule();
}
