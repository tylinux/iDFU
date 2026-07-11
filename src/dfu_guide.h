/* Copyright 2026 iDFU authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Interactive DFU entry guide and DFU/Recovery -> normal helpers.
 */
#ifndef IDFU_DFU_GUIDE_H
#define IDFU_DFU_GUIDE_H

#include <stdbool.h>

/* Interactive DFU entry guide. Returns true once DFU (0x1227) is detected. */
bool dfu_guide_run(void);

/* Exit Recovery/DFU toward normal mode (irecovery -n semantics where possible). */
bool dfu_exit_normal_run(void);

#endif /* IDFU_DFU_GUIDE_H */
