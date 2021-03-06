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
 *
 * The Original Code is Copyright (C) 2009 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup editors
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct ARegion;
struct SpaceText;
struct Text;
struct UndoStep;
struct UndoType;
struct bContext;

bool ED_text_activate_in_screen(struct bContext *C, struct Text *text);

/**
 * Moves the view to the cursor location, also used to make sure the view isn't outside the file.
 */
void ED_text_scroll_to_cursor(struct SpaceText *st, struct ARegion *region, bool center);

/**
 * Takes a cursor (row, character) and returns x,y pixel coords.
 */
bool ED_text_region_location_from_cursor(struct SpaceText *st,
                                         struct ARegion *region,
                                         const int cursor_co[2],
                                         int r_pixel_co[2]);

/* text_undo.c */

/** Export for ED_undo_sys. */
void ED_text_undosys_type(struct UndoType *ut);

/** Use operator system to finish the undo step. */
struct UndoStep *ED_text_undo_push_init(struct bContext *C);

/* text_format.c */
bool ED_text_is_syntax_highlight_supported(struct Text *text);

#ifdef __cplusplus
}
#endif
