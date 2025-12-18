#ifndef STEPS_H_
#define STEPS_H_

#include <hidapi/hidapi.h>

struct sdp_step_;
typedef struct sdp_step_ sdp_step;

sdp_step *sdp_parse_step(char *s);
sdp_step *sdp_new_step(const char *op, const char *file_path, const char *address);
sdp_step *sdp_append_step(sdp_step *list, sdp_step *step);
void sdp_free_steps(sdp_step *steps);
int sdp_execute_steps(hid_device *handle, sdp_step *steo);
sdp_step *sdp_next_step(sdp_step *step);
void sdp_set_next_step(sdp_step *step, sdp_step *next);

#endif
