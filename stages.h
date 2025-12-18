#ifndef STAGES_H_
#define STAGES_H_

#include "steps.h"
#include <stdbool.h>

struct sdp_stage_;
typedef struct sdp_stage_ sdp_stages;

sdp_stages *sdp_parse_stages(int count, char *s[]);
sdp_stages *sdp_new_stage(const char *vid, const char *pid, sdp_step *steps);
sdp_stages *sdp_append_stage(sdp_stages *list, sdp_stages *stage);
int sdp_execute_stages(sdp_stages *stages, bool initial_wait, const char *usb_path);
void sdp_free_stages(sdp_stages *stages);

#endif
