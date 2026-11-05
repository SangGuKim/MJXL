"""Experiment with reversible coefficients for 8-bit baseline JPEG; reject unsupported structures."""
import struct
import zlib
from functools import lru_cache
import numpy as np


def tables_and_layout(data):
    pos, tables, components = 2, {}, {}
    if data[:2] != b'\xff\xd8':
        raise ValueError('Not JPEG')
    while pos < len(data):
        if data[pos] != 255:
            raise ValueError('Invalid marker')
        marker = data[pos + 1]
        length = int.from_bytes(data[pos + 2:pos + 4], 'big')
        body = data[pos + 4:pos + 2 + length]
        end = pos + 2 + length
        if marker == 0xc0:
            if body[0] != 8:
                raise ValueError('Only 8-bit baseline JPEG')
            height, width = struct.unpack('>HH', body[1:5])
            for c in range(body[5]):
                cid, sampling, _ = body[6 + c * 3:9 + c * 3]
                components[cid] = (sampling >> 4, sampling & 15)
        elif marker in (0xc1, 0xc2, 0xdd):
            raise ValueError('Unsupported JPEG coding or restart interval')
        elif marker == 0xc4:
            p = 0
            while p < len(body):
                ident = body[p]
                counts = body[p + 1:p + 17]
                p += 17
                decode, encode, code = {}, {}, 0
                for bits, count in enumerate(counts, 1):
                    for _ in range(count):
                        symbol = body[p]
                        p += 1
                        decode[(bits, code)] = symbol
                        encode[symbol] = (bits, code)
                        code += 1
                    code <<= 1
                tables[ident] = (decode, encode)
        elif marker == 0xda:
            if not components or body[0] != len(components) or body[-3:] != b'\x00\x3f\x00':
                raise ValueError('Only single interleaved baseline scan')
            layout = []
            for c in range(body[0]):
                cid, selectors = body[1 + c * 2:3 + c * 2]
                h, v = components[cid]
                layout.extend([(cid, selectors >> 4, 16 + (selectors & 15))] * (h * v))
            max_h = max(x[0] for x in components.values())
            max_v = max(x[1] for x in components.values())
            mcus = ((width + 8 * max_h - 1) // (8 * max_h)) * ((height + 8 * max_v - 1) // (8 * max_v))
            return end, tables, layout, mcus
        pos = end
    raise ValueError('Missing scan')


class Reader:
    def __init__(self, data, pos):
        self.data, self.pos, self.buffer, self.available = data, pos, 0, 0

    def bits(self, count):
        while self.available < count:
            byte = self.data[self.pos]
            self.pos += 1
            if byte == 255:
                if self.data[self.pos] != 0:
                    raise ValueError('Unexpected marker in entropy stream')
                self.pos += 1
            self.buffer = (self.buffer << 8) | byte
            self.available += 8
        self.available -= count
        value = (self.buffer >> self.available) & ((1 << count) - 1)
        self.buffer &= (1 << self.available) - 1
        return value

    def symbol(self, table):
        code = 0
        for length in range(1, 17):
            code = (code << 1) | self.bits(1)
            if (length, code) in table:
                return table[length, code]
        raise ValueError('Invalid Huffman code')

    def amplitude(self, size):
        value = self.bits(size)
        return value if size == 0 or value >= (1 << (size - 1)) else value - ((1 << size) - 1)


class Writer:
    def __init__(self):
        self.data, self.buffer, self.available = bytearray(), 0, 0

    def bits(self, value, count):
        if value < 0 or value >= (1 << count):
            raise ValueError('Amplitude does not fit original symbol')
        self.buffer = (self.buffer << count) | value
        self.available += count
        while self.available >= 8:
            self.available -= 8
            byte = (self.buffer >> self.available) & 255
            self.data.append(byte)
            if byte == 255:
                self.data.append(0)
            self.buffer &= (1 << self.available) - 1

    def amplitude(self, value, size):
        self.bits(value if value >= 0 else value + (1 << size) - 1, size)


@lru_cache(maxsize=3)
def split(data):
    start, tables, layout, mcus = tables_and_layout(data)
    reader = Reader(data, start)
    coefficients = np.zeros((mcus * len(layout), 64), dtype='<i4')
    symbols, predictors = bytearray(), {}
    for block, row in enumerate(coefficients):
        cid, dc, ac = layout[block % len(layout)]
        symbol = reader.symbol(tables[dc][0])
        if symbol > 11:
            raise ValueError('Invalid DC category')
        symbols.append(symbol)
        value = predictors.get(cid, 0) + reader.amplitude(symbol)
        predictors[cid] = value
        row[0] = value
        k = 1
        while k < 64:
            symbol = reader.symbol(tables[ac][0])
            symbols.append(symbol)
            if symbol == 0:
                break
            run, size = symbol >> 4, symbol & 15
            if size == 0:
                if run != 15:
                    raise ValueError('Invalid zero run')
                k += 16
            else:
                k += run
                if k >= 64 or size > 10:
                    raise ValueError('Invalid AC coefficient')
                row[k] = reader.amplitude(size)
                k += 1
    state = (data[:start], bytes(symbols), reader.available, reader.buffer, data[reader.pos:])
    if join(state, coefficients) != data:
        raise ValueError('Coefficient parser does not reproduce original JPEG')
    return state, coefficients


def join(state, coefficients):
    header, symbols, pad_count, pad_value, suffix = state
    _, tables, layout, mcus = tables_and_layout(header)
    if coefficients.shape != (mcus * len(layout), 64):
        raise ValueError('Coefficient shape mismatch')
    writer, predictors, index = Writer(), {}, 0
    for block, row in enumerate(coefficients):
        cid, dc, ac = layout[block % len(layout)]
        symbol = symbols[index]
        index += 1
        writer.bits(tables[dc][1][symbol][1], tables[dc][1][symbol][0])
        value = int(row[0])
        writer.amplitude(value - predictors.get(cid, 0), symbol)
        predictors[cid] = value
        k = 1
        while k < 64:
            symbol = symbols[index]
            index += 1
            bits, code = tables[ac][1][symbol]
            writer.bits(code, bits)
            if symbol == 0:
                break
            run, size = symbol >> 4, symbol & 15
            if size == 0:
                k += 16
            else:
                k += run
                writer.amplitude(int(row[k]), size)
                k += 1
    if index != len(symbols):
        raise ValueError('Unused Huffman symbols')
    writer.bits(pad_value, pad_count)
    if writer.available:
        raise ValueError('Entropy stream not byte aligned')
    return header + writer.data + suffix


def encode(current, previous):
    state, coeff = split(current)
    _, reference = split(previous)
    if reference.shape != coeff.shape:
        raise ValueError('Reference dimensions differ')
    header, symbols, pad_count, pad_value, suffix = state
    delta = coeff - reference
    if delta.min() < -32768 or delta.max() > 32767:
        raise ValueError('Residual out of range')
    parts = [header, symbols, suffix, delta.astype('<i2').tobytes()]
    payload = struct.pack('<IIIIBB', *(len(p) for p in parts), pad_count, pad_value) + b''.join(parts)
    return zlib.compress(payload, 9)


def decode(payload, previous):
    data = zlib.decompress(payload)
    h, s, t, d, pad_count, pad_value = struct.unpack('<IIIIBB', data[:18])
    if len(data) != 18 + h + s + t + d:
        raise ValueError('Invalid coefficient payload lengths')
    header = data[18:18 + h]
    symbols = data[18 + h:18 + h + s]
    suffix = data[18 + h + s:18 + h + s + t]
    _, reference = split(previous)
    delta = np.frombuffer(data[18 + h + s + t:], dtype='<i2').astype('<i4').reshape(reference.shape)
    return join((header, symbols, pad_count, pad_value, suffix), reference + delta)
