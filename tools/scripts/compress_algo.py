import sys

LOAD_TYPE_FIX  = 0
LOAD_TYPE_COPY = 1
LOAD_TYPE_RLEZ = 2
LOAD_TYPE_ZERO = 3

load_type_str = ['FIX','COPY',"RLEZ",'ZERO']

def copy_algo(data):
    return len(data), data

def rlez_algo(data):
    data_o = []
    flag = False
    dest_len = 0
    for ch in data:
        if flag == False:
            if ch == 0:
                flag = True
                data_o.append(0)
            else:
                data_o.append(ch)
        else:
            if ch != 0:
                flag = False
                data_o.append(dest_len)
                data_o.append(ch)
                dest_len = 0
            else:
                dest_len += 1
                if dest_len == 255:
                    flag = False
                    data_o.append(dest_len)
                    dest_len = 0
    if flag:
        data_o.append(dest_len)

    return len(data_o), bytearray(data_o)
