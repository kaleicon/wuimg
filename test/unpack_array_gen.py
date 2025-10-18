#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
# SPDX-FileCopyrightText: 2025 kaleido
import itertools
import functools

def bin2hex(digits, tup):
	return '0x{:0{digits}x}'.format(int(''.join(tup), 2), digits=digits)

def unpack_array(b, bits):
	n = 4 if bits > 8 else 8
	b2h = functools.partial(bin2hex, 4 if bits > 8 else 2)
	return ', '.join(map(b2h, itertools.islice(itertools.batched(b, bits), n)))

n = 0xba9876543210fedc
b = '{:b}'.format(n)
for depth in range(1, 16):
	print(depth, unpack_array(b, depth), sep='\t')
