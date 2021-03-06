/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/** \file
 * \ingroup editors
 */

#pragma once

#define SNAP_MIN_DISTANCE 30

/* For enum. */
#include "DNA_space_types.h"

bool peelObjectsTransform(struct TransInfo *t,
                          const float mval[2],
                          const bool use_peel_object,
                          /* return args */
                          float r_loc[3],
                          float r_no[3],
                          float *r_thickness);

short snapObjectsTransform(struct TransInfo *t,
                           const float mval[2],
                           float *dist_px,
                           /* return args */
                           float r_loc[3],
                           float r_no[3]);
bool snapNodesTransform(struct TransInfo *t,
                        const int mval[2],
                        /* return args */
                        float r_loc[2],
                        float *r_dist_px,
                        char *r_node_border);

bool transformModeUseSnap(const TransInfo *t);

bool transform_snap_increment_ex(const TransInfo *t, bool use_local_space, float *r_val);
bool transform_snap_increment(const TransInfo *t, float *val);
bool transform_snap_grid(TransInfo *t, float *val);

bool activeSnap(const TransInfo *t);
bool activeSnap_with_project(const TransInfo *t);

bool validSnap(const TransInfo *t);

void initSnapping(struct TransInfo *t, struct wmOperator *op);
void freeSnapping(struct TransInfo *t);
void applyProject(TransInfo *t);
void applyGridAbsolute(TransInfo *t);
void applySnapping(TransInfo *t, float *vec);
void resetSnapping(TransInfo *t);
eRedrawFlag handleSnapping(TransInfo *t, const struct wmEvent *event);
void drawSnapping(const struct bContext *C, TransInfo *t);
bool usingSnappingNormal(const TransInfo *t);
bool validSnappingNormal(const TransInfo *t);

void getSnapPoint(const TransInfo *t, float vec[3]);
void addSnapPoint(TransInfo *t);
eRedrawFlag updateSelectedSnapPoint(TransInfo *t);
void removeSnapPoint(TransInfo *t);

float transform_snap_distance_len_squared_fn(TransInfo *t, const float p1[3], const float p2[3]);

/* transform_snap_sequencer.c */

struct TransSeqSnapData *transform_snap_sequencer_data_alloc(const TransInfo *t);
void transform_snap_sequencer_data_free(struct TransSeqSnapData *data);
bool transform_snap_sequencer_calc(struct TransInfo *t);
void transform_snap_sequencer_apply_translate(TransInfo *t, float *vec);

/* transform_snap_animation.c */

/**
 * This function returns the snapping 'mode' for Animation Editors only.
 * We cannot use the standard snapping due to NLA-strip scaling complexities.
 *
 * TODO: these modifier checks should be accessible from the key-map.
 */
short getAnimEdit_SnapMode(TransInfo *t);
void snapFrameTransform(TransInfo *t,
                        const eAnimEdit_AutoSnap autosnap,
                        const float val_initial,
                        const float val_final,
                        float *r_val_final);
/**
 * This function is used by Animation Editor specific transform functions to do
 * the Snap Keyframe to Nearest Frame/Marker
 */
void transform_snap_anim_flush_data(TransInfo *t,
                                    TransData *td,
                                    const eAnimEdit_AutoSnap autosnap,
                                    float *r_val_final);
