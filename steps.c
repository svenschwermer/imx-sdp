#include "steps.h"
#include "sdp.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

union step_run_data
{
	struct
	{
		const char *file_path;
		uint32_t address;
	} write_file;
	struct
	{
		uint32_t address;
	} jump_address;
};

struct sdp_step_
{
	int (*exec)(hid_device *, const union step_run_data *);
	union step_run_data data;
	struct sdp_step_ *next;
};

static int exec_write_file(hid_device *handle, const union step_run_data *data)
{
	return sdp_write_file(handle, data->write_file.file_path,
						  data->write_file.address);
}

static int exec_jump_address(hid_device *handle, const union step_run_data *data)
{
	return sdp_jump_address(handle, data->jump_address.address);
}

static int parse_uint32(const char *s, uint32_t *value)
{
	char *end;
	unsigned long ul = strtoul(s, &end, 16);
	if (s == end || ul > UINT32_MAX)
		return -1;
	*value = (uint32_t)ul;
	return 0;
}

sdp_step *sdp_parse_step(char *s)
{
	char *saveptr = NULL;
	const char *tok = strtok_r(s, ":", &saveptr);
	if (!tok)
	{
		fprintf(stderr, "ERROR: Missing step command\n");
		return NULL;
	}

	sdp_step *result = malloc(sizeof(sdp_step));
	if (!result)
	{
		fprintf(stderr, "ERROR: Allocation failed\n");
		return NULL;
	}
	result->next = NULL;

	if (!strcmp(tok, "write_file"))
	{
		const char *file_path = strtok_r(NULL, ":", &saveptr);
		const char *address = strtok_r(NULL, ":", &saveptr);
		if (!file_path || !address)
		{
			fprintf(stderr, "ERROR: Invalid write_file step\n");
			goto free_result;
		}
		result->exec = exec_write_file;
		if (parse_uint32(address, &result->data.write_file.address))
		{
			fprintf(stderr, "ERROR: Invalid write_file address\n");
			goto free_result;
		}
		result->data.write_file.file_path = strdup(file_path);
		if (!result->data.write_file.file_path)
		{
			fprintf(stderr, "ERROR: Failed to allocate file path\n");
			goto free_result;
		}
	}
	else if (!strcmp(tok, "jump_address"))
	{
		const char *address = strtok_r(NULL, ":", &saveptr);
		if (!address)
		{
			fprintf(stderr, "ERROR: Invalid jump_address step\n");
			goto free_result;
		}
		result->exec = exec_jump_address;
		if (parse_uint32(address, &result->data.jump_address.address))
		{
			fprintf(stderr, "ERROR: Invalid jump_address address\n");
			goto free_result;
		}
	}
	else
	{
		fprintf(stderr, "ERROR: Unknown step command \"%s\"\n", tok);
		goto free_result;
	}

	return result;

free_result:
	free(result);
	return NULL;
}

sdp_step *sdp_new_step(const char *op, const char *file_path, const char *address)
{
	if (!op)
	{
		fprintf(stderr, "ERROR: Step operation unset\n");
		return NULL;
	}

	sdp_step *result = malloc(sizeof(sdp_step));
	if (!result)
	{
		fprintf(stderr, "ERROR: Allocation failed\n");
		return NULL;
	}
	result->next = NULL;

	if (!strcmp(op, "write_file"))
	{
		if (!file_path || !address)
		{
			fprintf(stderr, "ERROR: Invalid write_file step\n");
			goto free_result;
		}
		result->exec = exec_write_file;
		if (parse_uint32(address, &result->data.write_file.address))
		{
			fprintf(stderr, "ERROR: Invalid write_file address\n");
			goto free_result;
		}
		result->data.write_file.file_path = strdup(file_path);
		if (!result->data.write_file.file_path)
		{
			fprintf(stderr, "ERROR: Failed to allocate file path\n");
			goto free_result;
		}
	}
	else if (!strcmp(op, "jump_address"))
	{
		if (!address)
		{
			fprintf(stderr, "ERROR: Invalid jump_address step\n");
			goto free_result;
		}
		result->exec = exec_jump_address;
		if (parse_uint32(address, &result->data.jump_address.address))
		{
			fprintf(stderr, "ERROR: Invalid jump_address address\n");
			goto free_result;
		}
	}
	else
	{
		fprintf(stderr, "ERROR: Unknown step command \"%s\"\n", op);
		goto free_result;
	}

	return result;

free_result:
	free(result);
	return NULL;
}

sdp_step *sdp_append_step(sdp_step *list, sdp_step *step)
{
	if (!list)
		return step;

	sdp_step *it = list;
	while (it->next)
		it = it->next;
	it->next = step;
	return list;
}

void sdp_free_steps(sdp_step *steps)
{
	while (steps)
	{
		if (steps->exec == exec_write_file)
			free((void *)steps->data.write_file.file_path);
		void *const to_be_freed = steps;
		steps = steps->next;
		free(to_be_freed);
	}
}

int sdp_execute_steps(hid_device *handle, sdp_step *step)
{
	for (int i = 1; step; ++i)
	{
		printf("[Step %d] ", i);
		if (step->exec(handle, &step->data))
		{
			fprintf(stderr, "ERROR: Failed to execute step %d\n", i);
			return 1;
		}
		step = step->next;
	}
	return 0;
}

sdp_step *sdp_next_step(sdp_step *step)
{
	return step->next;
}

void sdp_set_next_step(sdp_step *step, sdp_step *next)
{
	step->next = next;
}
