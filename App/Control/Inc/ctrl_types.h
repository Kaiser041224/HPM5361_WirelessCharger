/*
 * Control Common Types
 *
 * Shared types for closed-loop control modules.
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CTRL_TYPES_H
#define CTRL_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float value;
} ctrl_scalar_t;

#endif /* CTRL_TYPES_H */
