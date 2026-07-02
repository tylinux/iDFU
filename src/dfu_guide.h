/* Copyright 2026 iDFU authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Interactive text-only DFU entry guide. Polls the USB backend for the
 * Apple Recovery (PID 0x1281) and DFU (PID 0x1227) enumerations to drive
 * the user through the button timing sequence, like checkra1n's CLI.
 */
#ifndef IDFU_DFU_GUIDE_H
#	define IDFU_DFU_GUIDE_H
#	include <stdbool.h>

/* Run the interactive DFU entry guide. Returns true once a DFU-mode
 * device (VID 0x5AC, PID 0x1227) is detected. */
bool dfu_guide_run(void);

#endif /* IDFU_DFU_GUIDE_H */