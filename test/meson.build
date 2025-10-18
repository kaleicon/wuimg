# SPDX-License-Identifier: 0BSD
# SPDX-FileCopyrightText: 2025 kaleido
test('main',
	executable('main', 'main.c', include_directories : incdir, link_with : libuwu),
	# The fast_math tests are actually quite slow, so grant time enough for
	# them to complete on an old processor at 800MHz
	timeout : 90)
