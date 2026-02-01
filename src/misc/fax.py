# SPDX-License-Identifier: 0BSD
# SPDX-FileCopyrightText: 2025 kaleido
import itertools
import enum
import typing
import sys

# Produces a Huffman lookup table for fax decoding.

# Fax huffman codes, copy-pasted from the Rec. T.4 PDF
bw_codes = '''
0 00110101 0 0000110111
1 000111 1 010
2 0111 2 11
3 1000 3 10
4 1011 4 011
5 1100 5 0011
6 1110 6 0010
7 1111 7 00011
8 10011 8 000101
9 10100 9 000100
10 00111 10 0000100
11 01000 11 0000101
12 001000 12 0000111
13 000011 13 00000100
14 110100 14 00000111
15 110101 15 000011000
16 101010 16 0000010111
17 101011 17 0000011000
18 0100111 18 0000001000
19 0001100 19 00001100111
20 0001000 20 00001101000
21 0010111 21 00001101100
22 0000011 22 00000110111
23 0000100 23 00000101000
24 0101000 24 00000010111
25 0101011 25 00000011000
26 0010011 26 000011001010
27 0100100 27 000011001011
28 0011000 28 000011001100
29 00000010 29 000011001101
30 00000011 30 000001101000
31 00011010 31 000001101001
32 00011011 32 000001101010

33 00010010 33 000001101011
34 00010011 34 000011010010
35 00010100 35 000011010011
36 00010101 36 000011010100
37 00010110 37 000011010101
38 00010111 38 000011010110
39 00101000 39 000011010111
40 00101001 40 000001101100
41 00101010 41 000001101101
42 00101011 42 000011011010
43 00101100 43 000011011011
44 00101101 44 000001010100
45 00000100 45 000001010101
46 00000101 46 000001010110
47 00001010 47 000001010111
48 00001011 48 000001100100
49 01010010 49 000001100101
50 01010011 50 000001010010
51 01010100 51 000001010011
52 01010101 52 000000100100
53 00100100 53 000000110111
54 00100101 54 000000111000
55 01011000 55 000000100111
56 01011001 56 000000101000
57 01011010 57 000001011000
58 01011011 58 000001011001
59 01001010 59 000000101011
60 01001011 60 000000101100
61 00110010 61 000001011010
62 00110011 62 000001100110
63 00110100 63 000001100111

64 11011 64 0000001111
128 10010 128 000011001000
192 010111 192 000011001001
256 0110111 256 000001011011
320 00110110 320 000000110011
384 00110111 384 000000110100
448 01100100 448 000000110101
512 01100101 512 0000001101100
576 01101000 576 0000001101101
640 01100111 640 0000001001010
704 011001100 704 0000001001011
768 011001101 768 0000001001100
832 011010010 832 0000001001101
896 011010011 896 0000001110010
960 011010100 960 0000001110011
1024 011010101 1024 0000001110100
1088 011010110 1088 0000001110101
1152 011010111 1152 0000001110110
1216 011011000 1216 0000001110111
1280 011011001 1280 0000001010010
1344 011011010 1344 0000001010011
1408 011011011 1408 0000001010100
1472 010011000 1472 0000001010101
1536 010011001 1536 0000001011010
1600 010011010 1600 0000001011011
1664 011000 1664 0000001100100
1728 010011011 1728 0000001100101
'''

long_codes = '''
1792 00000001000
1856 00000001100
1920 00000001101
1984 000000010010
2048 000000010011
2112 000000010100
2176 000000010101
2240 000000010110
2304 000000010111
2368 000000011100
2432 000000011101
2496 000000011110
2560 000000011111
'''

eol_code = '''EOL 000000000001'''

class FaxCode(typing.NamedTuple):
	'''A G3 code'''

	val: int
	'''Pixel run length'''

	bits: str
	'''Bit code'''

	def expand(self):
		val = -1 if self.val == "EOL" else int(self.val)
		return val, len(self.bits), int(self.bits, base=2)

@enum.unique
class TableStore(enum.StrEnum):
	'''Huffman table storage method'''

	Pack = 'pack'
	'''Store as an uint16_t array, using shifts and masks to extract values. Seems to be the fastest'''

	Fields = 'fields'
	'''Store as an struct array, each entry being 32-bits due to padding rules'''

	Bitfields = 'bitfields'
	'''Store as an struct array, using bitfields to reduce to 16-bits. Seems to be the slowest somehow'''

	def type(self):
		'''C type of table'''
		if self == TableStore.Pack:
			return 'uint16_t'
		return 'struct fax_huff_val'

	def struct(self):
		'''C struct definition'''
		if self == TableStore.Fields:
			tpl = ('uint16_t val', 'uint8_t bits')
		elif self == TableStore.Bitfields:
			tpl = ('unsigned val:12', 'unsigned bits:4')
		else:
			return ''
		return '{} {{\n\t{};\n\t{};\n}};'.format(
			self.type(), tpl[0], tpl[1])

	def fn_tmpl(self, ret_type, field, op):
		'''Accessor function template'''
		return '''{} fax_huffman_get_{}({} pack) {{\n\treturn {};\n}}'''.format(
			ret_type, field, self.type(), op)

	def val_fn(self):
		'''Val accessor function'''
		return self.fn_tmpl('uint16_t', 'val',
			'pack >> 4' if self == TableStore.Pack else 'pack.val')

	def bits_fn(self):
		'''Bit accessor function'''
		return self.fn_tmpl('uint8_t', 'bits',
			'pack & 0xf' if store == TableStore.Pack else 'pack.bits')

class TableEntry(typing.NamedTuple):
	'''Huffman table entry'''

	val: int
	'''Pixel run length, or index to second-level table if `bits` is 0'''

	bits: int
	'''Bits to advance the stream. If 0, use the table bit size'''

	def render(self, store):
		'''Entry declaration'''
		if store == TableStore.Pack:
			return f'({self.val} << 4) | {self.bits},'
		return f'{{ .val={self.val}, .bits={self.bits} }},'

def print_table(tab):
	cnt = [0] * 13
	for n, t in enumerate(tab):
		val, bits, code = t.expand()
		print(n, val, bits, "{:0{}b}".format(code, bits), sep='\t')
		cnt[bits - 1] += 1
	for n, c in enumerate(cnt):
		print(n + 1, c, sep='\t')

def get_max_bits(tab):
	return max(map(lambda t: len(t.bits), tab))

def make_huffman_table(tab, size, total, store, ofp, offset=0):
	over = total - size
	mask = ((1 << over) - 1)
	huff = [TableEntry(0, 0)] * (1 << size)
	dhuff = [] # second level tables
	for n, t in enumerate(tab):
		val, bits, code = t.expand()
		align = total - bits
		norm = code << align
		prefix = norm >> over
		if bits <= size:
			v = TableEntry(val, bits)
			for n in range(1 << (size - bits)):
				huff[prefix + n] = v
		else:
			base = norm & mask
			tgt = None
			for nn, dh in enumerate(dhuff):
				if dh[0] == prefix:
					tgt = dh[1]
					idx = nn
			if not tgt:
				tgt = [TableEntry(0,0)] * (1 << over)
				idx = len(dhuff)
				dhuff.append((prefix, tgt))
			v = TableEntry(0, 0) if val < 0 else TableEntry(val, bits - size)
			for n in range(1 << align):
				tgt[base + n] = v
			huff[prefix] = TableEntry(idx, 0)

	for n, t in enumerate(huff):
		print(f'\t[{n+offset}]', t.render(store), sep=' = ', file=ofp)
	for m, dh in enumerate(dhuff):
		for n, t in enumerate(dh[1]):
			print(f'\t[{(1 << size)} + {(m << over)} + {n+offset}]',
				t.render(store), sep=' = ', file=ofp)
	return (1 << size) + len(dhuff) * (1 << over)

def parse_code_strs(short, long):
	white = []
	black = []
	for t in itertools.batched(short.split(), 4):
		white.append(FaxCode._make(t[:2]))
		black.append(FaxCode._make(t[2:]))
	tup = tuple(map(FaxCode._make, itertools.batched(long.split() + eol_code.split(), 2)))
	white += tup
	black += tup
	return white, black

def print_offset(name, off, fp):
	print('static const uint16_t ', name, ' = ', off, ';', sep='', file=fp)

def print_table_dims(lvl1, total, fp):
	print('static const uint8_t FAX_LEVEL1_BITS = ', lvl1, ';', sep='', file=fp)
	print('static const uint8_t FAX_LEVEL2_BITS = ', total - lvl1, ';', sep='', file=fp)

if __name__ == "__main__":
	white, black = parse_code_strs(bw_codes, long_codes)
	if len(sys.argv) == 3:
		store = TableStore.Pack
		lvl1_bits = 8 # Produces the smallest table
		max_bits = max(get_max_bits(white), get_max_bits(black))
		table_name = 'FAX_HUFFMAN_TABLE'
		with open(sys.argv[1], "w") as hfp, open(sys.argv[2], "w") as cfp:
			print(store.struct(), file=hfp)
			print_table_dims(lvl1_bits, max_bits, hfp)
			print('extern const ', store.type(), ' ', table_name, '[];',
				sep='', file=hfp)

			print('#include <stdint.h>', file=cfp)
			print('const ', store.type(), ' ', table_name, '[] = {',
				sep='', file=cfp)
			wentries = make_huffman_table(white, lvl1_bits, max_bits, store, cfp)
			bentries = make_huffman_table(black, lvl1_bits, max_bits, store, cfp,
				wentries)
			print('};', file=cfp)

			print_offset('FAX_WHITE_OFFSET', 0, hfp)
			print_offset('FAX_BLACK_OFFSET', wentries, hfp)
			print('static', store.val_fn(), file=hfp)
			print('static', store.bits_fn(), file=hfp)
	else:
		print('Usage:', sys.argv[0], 'out.h', 'out.c')
		#print_table(white)
		#print_table(black)
