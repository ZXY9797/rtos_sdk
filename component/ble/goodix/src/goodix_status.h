#pragma once

#include "ble/ble_types.h"

extern "C" {
#include "ble_error.h"
}

namespace ble::goodix {

[[nodiscard]] inline Status status_from_sdk(sdk_err_t error)
{
    switch (error) {
    case SDK_SUCCESS:
        return Status::Ok;
    case SDK_ERR_BUSY:
    case SDK_ERR_LIST_FULL:
    case SDK_ERR_NO_RESOURCES:
        return Status::Busy;
    case SDK_ERR_NTF_DISABLED:
    case SDK_ERR_IND_DISABLED:
    case SDK_ERR_DISCONNECTED:
    case SDK_ERR_INVALID_CONN_IDX:
        return Status::NotConnected;
    case SDK_ERR_INVALID_PARAM:
    case SDK_ERR_POINTER_NULL:
    case SDK_ERR_INVALID_BUFF_LENGTH:
    case SDK_ERR_INVALID_DATA_LENGTH:
        return Status::InvalidParam;
    default:
        return Status::Error;
    }
}

} // namespace ble::goodix
