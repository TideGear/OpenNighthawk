/* observe_pose.h - a model's prepared pose, offered to a read-only consumer (docs/remastered-design.md, "R3 plan
 * revised").
 *
 * The game prepares a model's part matrices and positions (VGAME 120A:0317, model_parts_setup) before it projects the
 * model's vertices. The callback set here is called once per model request, at the first vertex that request projects,
 * which is the first moment the whole pose is in the data segment: with the model index (the request's stack argument,
 * SS:SP+4 at entry), the model bank's segment (DS:8590) and the position words, all noted at the request. The consumer may read the machine
 * (scene_pose_read takes the pieces it needs) and must not change it. A request that projects no vertex (culled) is
 * never reported. With no callback set nothing is recorded and the matched routines run exactly as before.
 *
 * This is a separate header, not a field of f117_observer, so that observe.h, which matched.c includes, is untouched. */
#ifndef F117R_OBSERVE_POSE_H
#define F117R_OBSERVE_POSE_H

#include "machine.h"

#include <stdint.h>

/* position: the request's three position words (SS:SP+6, +8, +10 at entry), which with the camera matrix at DS:7B28 pick the
 * detail level (scene_model_depth). */
typedef void (*observe_pose_fn)(void *user, uint64_t icount, machine_t *m, uint16_t model, uint16_t bank_segment,
                                const int16_t position[3]);

/* Install (or clear, with NULL) the callback. */
void observe_pose_set(observe_pose_fn fn, void *user);

#endif
