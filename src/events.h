// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#ifndef WU_EVENTS
#define WU_EVENTS

#include "window.h"

double event_exec(struct window_context *window);

void event_print_keys(FILE *out);

#endif /* WU_EVENTS */
