#ifndef STAGES_H_
#define STAGES_H_

#include <stdbool.h>

struct sdp_stages_;
typedef struct sdp_stages_ sdp_stages;

sdp_stages *sdp_parse_stages(int count, char *s[]);
int sdp_execute_stages(sdp_stages *stages, bool initial_wait, const char *usb_path);
void sdp_free_stages(sdp_stages *stages);

#endif
