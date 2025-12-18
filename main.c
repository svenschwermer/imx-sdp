#include "config.h"
#include "stages.h"
#include "spec.h"
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *progname);

static const struct option longopts[] = {
	{"directory", no_argument, NULL, 'C'},
	{"help", no_argument, NULL, 'h'},
	{"path", required_argument, NULL, 'p'},
	{"spec", required_argument, NULL, 's'},
	{"version", no_argument, NULL, 'V'},
	{"wait", no_argument, NULL, 'w'},
	{0},
};

int main(int argc, char *argv[])
{
	/* Make stdout and stderr line-buffered */
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

	int opt;
	const char *dir = NULL;
	const char *usb_path = NULL;
	const char *spec = NULL;
	bool initial_wait = false;

	while ((opt = getopt_long(argc, argv, "hC:p:s:wV", longopts, NULL)) != -1)
	{
		switch (opt)
		{
		case 'C':
			dir = optarg;
			break;
		case 'h':
			usage(argv[0]);
			return EXIT_SUCCESS;
		case 'p':
			usb_path = optarg;
			break;
		case 's':
			spec = optarg;
			break;
		case 'w':
			initial_wait = true;
			break;
		case 'V':
			puts(VERSION);
			return EXIT_SUCCESS;
		default:
			return EXIT_FAILURE;
		}
	}

	sdp_stages *stages;
	if (spec)
	{
		if (optind < argc)
		{
			fprintf(stderr, "ERROR: Arguments not allowed when --spec is used\n");
			return EXIT_FAILURE;
		}

		stages = sdp_parse_spec(spec, &usb_path);
		if (!stages)
		{
			fprintf(stderr, "ERROR: Failed to parse spec file\n");
			return EXIT_FAILURE;
		}
	}
	else
	{
		if (optind >= argc)
		{
			fprintf(stderr, "ERROR: Expected at least one stage\n");
			usage(argv[0]);
			return EXIT_FAILURE;
		}

		stages = sdp_parse_stages(argc - optind, argv + optind);
		if (!stages)
		{
			fprintf(stderr, "ERROR: Failed to parse stages\n");
			return EXIT_FAILURE;
		}
	}

	if (dir && chdir(dir))
	{
		fprintf(stderr, "ERROR: Failed to change directory: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	int result = sdp_execute_stages(stages, initial_wait, usb_path);

	sdp_free_stages(stages);

	return result;
}

static void usage(const char *progname)
{
	printf(
		"Usage: %s [OPTION...] [STAGE...]\n"
		"\n"
		"The following OPTIONs are available:\n"
		"\n"
		"  -C, --directory  change working directory, after spec is read\n"
		"  -h, --help  print this usage message\n"
		"  -p, --path  specify the USB device path, e.g. 3-1.1\n"
		"  -s, --spec  stage/step spec file\n"
		"  -V, --version  print version\n"
		"  -w, --wait  wait for the first stage\n"
		"\n"
		"The STAGEs have the following format:\n"
		"\n"
		"  <VID>:<PID>[,<STEP>...]\n"
		"    VID  USB Vendor ID as 4-digit hex number\n"
		"    PID  USB Product ID as 4-digit hex number\n"
		"\n"
		"The STEPs can be one of the following operations:\n"
		"\n"
		"  write_file:<FILE>:<ADDRESS>\n"
		"    Write the contents of FILE to ADDRESS\n"
		"  jump_address:<ADDRESS>\n"
		"    Jump to the IMX image located at ADDRESS\n"
		"\n"
		"Instead of specifying the stages and steps on the command line, they can be\n"
		"specified in a YAML file instead (--spec option). Note, that providing the spec\n"
		"on the command line and in a file are mutually exclusive.\n",
		progname);
}
