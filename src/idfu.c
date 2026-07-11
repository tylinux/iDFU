/* Copyright 2026 iDFU authors
 *
 * Licensed under the Apache License, Version 2.0.
 *
 * iDFU — DFU entry guide and exit-to-normal only.
 */
#include "dfu_guide.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IDFU_VERSION "0.2.0"

static void
usage(const char *prog) {
	printf("Usage: %s <command>\n", prog);
	puts("");
	puts("Commands:");
	puts("  guide     Interactive walk-through into DFU mode");
	puts("            (normal -> Recovery -> soft reboot + buttons).");
	puts("  exit      Leave Recovery/DFU toward normal mode");
	puts("            (auto-boot=true + reboot; BootROM DFU needs force-restart).");
	puts("  version   Print version.");
}

static int
cmd_guide(void) {
	return dfu_guide_run() ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int
cmd_exit(void) {
	return dfu_exit_normal_run() ? EXIT_SUCCESS : EXIT_FAILURE;
}

int
main(int argc, char **argv) {
	if(argc < 2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	if(strcmp(argv[1], "guide") == 0)
		return cmd_guide();
	if(strcmp(argv[1], "exit") == 0 || strcmp(argv[1], "normal") == 0)
		return cmd_exit();
	if(strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
		printf("iDFU %s (DFU guide + exit-to-normal; limd/palera1n-style)\n", IDFU_VERSION);
		return EXIT_SUCCESS;
	}

	usage(argv[0]);
	return EXIT_FAILURE;
}
