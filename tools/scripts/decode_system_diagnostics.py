#!/usr/bin/env python3
"""Decode command-set diagnostic command 2 payload (excluding Link ACK status)."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct


class Reader:
    def __init__(self, data): self.data, self.offset = data, 0
    def take(self, size):
        if size > len(self.data) - self.offset: raise ValueError('truncated diagnostics payload')
        result = self.data[self.offset:self.offset + size]; self.offset += size
        return result
    def u8(self): return self.take(1)[0]
    def u16(self): return struct.unpack('<H', self.take(2))[0]
    def u32(self): return struct.unpack('<I', self.take(4))[0]
    def text(self): return self.take(self.u8()).decode('utf-8', errors='replace')


def decode(data: bytes) -> dict:
    reader = Reader(data)
    if reader.u8() != 1: raise ValueError('unsupported system diagnostics version')
    value = {'build_id': reader.text(), 'uptime_ms': reader.u32(),
             'memory_available': bool(reader.u8()), 'heap_free': reader.u32(),
             'heap_minimum_free': reader.u32(), 'fault_valid': bool(reader.u8()),
             'fault_reason': reader.u32(), 'fault_pc': reader.u32(),
             'device_total': reader.u16(), 'health_client_total': reader.u8(),
             'devices': [], 'clients': [], 'timings': [], 'unknown': []}
    while reader.offset < len(data):
        kind, size = reader.u8(), reader.u16()
        raw = reader.take(size)
        record = Reader(raw)
        if kind == 1:
            value['devices'].append({'id': record.u32(), 'capabilities': record.u32(), 'ready': bool(record.u8())})
        elif kind == 2:
            value['clients'].append({'name': record.text(), 'silence_ms': record.u32(),
                                     'maximum_silence_ms': record.u32(), 'overdue': bool(record.u8())})
        elif kind == 3:
            item = {'name': record.text(), 'count': record.u32(), 'max_execution_us': record.u32(),
                    'max_latency_us': record.u32(), 'budget_misses': record.u32(),
                    'deadline_misses': record.u32(), 'latency_histogram': [record.u32() for _ in range(8)]}
            value['timings'].append(item)
        elif kind == 4:
            value['fault_build_id_prefix'] = record.text()
            value['fault_detail'] = record.u32()
            value['fault_context_hash'] = record.u32()
        elif kind == 5:
            value['boot_state_valid'] = bool(record.u8())
            value['boot_state'] = record.u8()
        else:
            value['unknown'].append({'type': kind, 'payload': raw.hex()})
            continue
        if record.offset != size: raise ValueError('unexpected diagnostic record length')
    value['truncated'] = (len(value['devices']) != value['device_total']
                          or len(value['clients']) != value['health_client_total'])
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('payload', type=Path)
    args = parser.parse_args()
    print(json.dumps(decode(args.payload.read_bytes()), indent=2))


if __name__ == '__main__': main()
