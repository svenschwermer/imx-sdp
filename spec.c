#include "spec.h"
#include "stages.h"
#include "steps.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <yaml.h>

enum spec_fsm
{
    STATE_INIT,
    STATE_ROOT_MAPPING,
    STATE_STAGES_KEY,
    STATE_STAGES_SEQ,
    STATE_STAGES_MAPPING,
    STATE_STEPS_KEY,
    STATE_STEPS_SEQ,
    STATE_STEPS_MAPPING,
    STATE_DONE,
};

static const char *fmt_event_type(yaml_event_type_t type)
{
    switch (type)
    {
    case YAML_NO_EVENT: 
        return "NO_EVENT";
    case YAML_STREAM_START_EVENT:
        return "STREAM_START_EVENT";
    case YAML_STREAM_END_EVENT:
        return "STREAM_END_EVENT";
    case YAML_DOCUMENT_START_EVENT:
        return "DOCUMENT_START_EVENT";
    case YAML_DOCUMENT_END_EVENT:
        return "DOCUMENT_END_EVENT";
    case YAML_ALIAS_EVENT:
        return "ALIAS_EVENT";
    case YAML_SCALAR_EVENT:
        return "SCALAR_EVENT";
    case YAML_SEQUENCE_START_EVENT:
        return "SEQUENCE_START_EVENT";
    case YAML_SEQUENCE_END_EVENT:
        return "SEQUENCE_END_EVENT";
    case YAML_MAPPING_START_EVENT:
        return "MAPPING_START_EVENT";
    case YAML_MAPPING_END_EVENT:
        return "MAPPING_END_EVENT";
    default:
        return "?";
    }
}

static void unexpected_event(const yaml_event_t *event)
{
    fprintf(stderr, "ERROR: Unexpected %s at line %zd (column %zd)\n",
        fmt_event_type(event->type), event->start_mark.line + 1,
        event->start_mark.column + 1);
}

static bool consume_scalar(yaml_parser_t *parser, yaml_event_t *event, const char **value)
{
    yaml_event_delete(event);
    if (!yaml_parser_parse(parser, event))
    {
        fprintf(stderr, "ERROR: Failed to parse YAML at line %zd (column %zd): %s\n",
            parser->problem_mark.line + 1, parser->problem_mark.column + 1, parser->problem);
        return false;
    }

    if (event->type == YAML_SCALAR_EVENT)
    {
        if (!value)
            return true;

        *value = strdup((const char *) event->data.scalar.value);
        if (!*value)
        {
            fprintf(stderr, "ERROR: Failed to allocate %zd bytes\n", event->data.scalar.length);
            return false;
        }
        return true;
    }

    unexpected_event(event);
    return false;
}

sdp_stages *sdp_parse_spec(const char *spec_path, const char **usb_path)
{
    sdp_stages *stages = NULL;

    FILE *spec = fopen(spec_path, "r");
    if (!spec)
    {
        fprintf(stderr, "ERROR: Failed to open %s: %s\n", spec_path, strerror(errno));
        goto out;
    }

    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser))
    {
        fprintf(stderr, "ERROR: Failed to initialize YAML parser\n");
        goto close_file;
    }
    yaml_parser_set_input_file(&parser, spec);

    enum spec_fsm fsm = STATE_INIT;

    const char *vid = NULL;
    const char *pid = NULL;
    sdp_step *steps = NULL;
    const char *op = NULL;
    const char *file = NULL;
    const char *address = NULL;

    yaml_event_t event;
    bool done;
    do
    {
        if (!yaml_parser_parse(&parser, &event))
        {
            fprintf(stderr, "ERROR: Failed to parse YAML at line %zd (column %zd): %s\n",
                parser.problem_mark.line + 1, parser.problem_mark.column + 1, parser.problem);
            goto delete_parser;
        }

        switch (fsm)
        {
        case STATE_INIT:
            switch (event.type)
            {
            case YAML_STREAM_START_EVENT:
            case YAML_DOCUMENT_START_EVENT:
                // ignore
                break;
            case YAML_MAPPING_START_EVENT:
                fsm = STATE_ROOT_MAPPING;
                break;
            default:
                unexpected_event(&event);
                goto delete_event;
            }
            break;
        case STATE_ROOT_MAPPING:
            switch (event.type)
            {
            case YAML_SCALAR_EVENT:
                if (!strcmp("usb_path", (const char *) event.data.scalar.value))
                {
                    const char **p = usb_path;
                    if (*usb_path)
                    {
                        fprintf(stderr, "WARN: Ignoring USB path from spec file (command line takes precedence)\n");
                        p = NULL;
                    }
                    if (!consume_scalar(&parser, &event, p))
                    {
                        fprintf(stderr, "ERROR: Failed to read USB path\n");
                        goto delete_event;
                    }
                }
                else if (!strcmp("stages", (const char *) event.data.scalar.value))
                    fsm = STATE_STAGES_KEY;
                else
                {
                    fprintf(stderr, "ERROR: Unexpected key: %s\n", event.data.scalar.value);
                    goto delete_event;
                }
                break;
            case YAML_MAPPING_END_EVENT:
                fsm = STATE_DONE;
                break;
            default:
                unexpected_event(&event);
                goto delete_event;
            }
            break;
        case STATE_STAGES_KEY:
            switch (event.type)
            {
            case YAML_SEQUENCE_START_EVENT:
                fsm = STATE_STAGES_SEQ;
                break;
            default:
                unexpected_event(&event);
                goto delete_event;
            }
            break;
        case STATE_STAGES_SEQ:
            switch (event.type)
            {
            case YAML_MAPPING_START_EVENT:
                fsm = STATE_STAGES_MAPPING;
                break;
            case YAML_SEQUENCE_END_EVENT:
                fsm = STATE_ROOT_MAPPING;
                break;
            default:
                unexpected_event(&event);
                goto delete_event;
            }
            break;
        case STATE_STAGES_MAPPING:
            switch (event.type)
            {
            case YAML_SCALAR_EVENT:
                if (!strcmp("vid", (const char *) event.data.scalar.value))
                {
                    if (!consume_scalar(&parser, &event, &vid))
                    {
                        fprintf(stderr, "ERROR: Failed to read VID\n");
                        goto delete_event;
                    }
                }
                else if (!strcmp("pid", (const char *) event.data.scalar.value))
                {
                    if (!consume_scalar(&parser, &event, &pid))
                    {
                        fprintf(stderr, "ERROR: Failed to read PID\n");
                        goto delete_event;
                    }
                }
                else if (!strcmp("steps", (const char *) event.data.scalar.value))
                    fsm = STATE_STEPS_KEY;
                else
                {
                    fprintf(stderr, "ERROR: Unexpected key: %s\n", event.data.scalar.value);
                    goto delete_event;
                }
                break;
            case YAML_MAPPING_END_EVENT:
                {
                    sdp_stages *stage = sdp_new_stage(vid, pid, steps);
                    free((void*)vid);
                    vid = NULL;
                    free((void*)pid);
                    pid = NULL;
                    if (!stage)
                    {
                        sdp_free_steps(steps);
                        goto delete_event;
                    }
                    steps = NULL;
                    stages = sdp_append_stage(stages, stage);
                    fsm = STATE_STAGES_SEQ;
                }
                break;
            default:
                unexpected_event(&event);
                goto delete_event;
            }
            break;
        case STATE_STEPS_KEY:
            switch (event.type)
            {
            case YAML_SEQUENCE_START_EVENT:
                fsm = STATE_STEPS_SEQ;
                break;
            default:
                unexpected_event(&event);
                goto delete_event;
            }
            break;
        case STATE_STEPS_SEQ:
            switch (event.type)
            {
            case YAML_MAPPING_START_EVENT:
                fsm = STATE_STEPS_MAPPING;
                break;
            case YAML_SEQUENCE_END_EVENT:
                fsm = STATE_STAGES_MAPPING;
                break;
            default:
                unexpected_event(&event);
                goto delete_event;
            }
            break;
        case STATE_STEPS_MAPPING:
            switch (event.type)
            {
            case YAML_SCALAR_EVENT:
                if (!strcmp("op", (const char *) event.data.scalar.value))
                {
                    if (!consume_scalar(&parser, &event, &op))
                    {
                        fprintf(stderr, "ERROR: Failed to read operation\n");
                        goto delete_event;
                    }
                }
                else if (!strcmp("file", (const char *) event.data.scalar.value))
                {
                    if (!consume_scalar(&parser, &event, &file))
                    {
                        fprintf(stderr, "ERROR: Failed to read file\n");
                        goto delete_event;
                    }
                }
                else if (!strcmp("address", (const char *) event.data.scalar.value))
                {
                    if (!consume_scalar(&parser, &event, &address))
                    {
                        fprintf(stderr, "ERROR: Failed to read address\n");
                        goto delete_event;
                    }
                }
                else
                {
                    fprintf(stderr, "ERROR: Unexpected key: %s\n", event.data.scalar.value);
                    goto delete_event;
                }
                break;
            case YAML_MAPPING_END_EVENT:
                {
                    sdp_step *step = sdp_new_step(op, file, address);
                    free((void *)op);
                    op = NULL;
                    free((void *)file);
                    file = NULL;
                    free((void *)address);
                    address = NULL;
                    if (!step)
                        goto delete_event;
                    steps = sdp_append_step(steps, step);
                    fsm = STATE_STEPS_SEQ;
                }
                break;
            default:
                unexpected_event(&event);
                goto delete_event;
            }
            break;
        case STATE_DONE:
            switch (event.type)
            {
            case YAML_STREAM_END_EVENT:
            case YAML_DOCUMENT_END_EVENT:
                // ignore
                break;
            default:
                unexpected_event(&event);
                goto delete_event;
            }
            break;
        }

        done = event.type == YAML_STREAM_END_EVENT;
        yaml_event_delete(&event);
    }
    while (!done);

    if (!stages)
        fprintf(stderr, "ERROR: No stages defined\n");

delete_event:
    yaml_event_delete(&event);
delete_parser:
    yaml_parser_delete(&parser);
close_file:
    fclose(spec);
out:
    return stages;
}
