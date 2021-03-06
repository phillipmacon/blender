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
 * \ingroup modifiers
 */

#pragma once

/* so modifier types match their defines */
#include "MOD_modifiertypes.h"

#include "DEG_depsgraph_build.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ARegionType;
struct PanelType;
struct bContext;
struct uiLayout;
typedef void (*PanelDrawFn)(const bContext *, struct Panel *);

void modifier_panel_buttons(const struct bContext *C, struct Panel *panel);

/**
 * Helper function for modifier layouts to draw vertex group settings.
 */
void modifier_vgroup_ui(struct uiLayout *layout,
                        struct PointerRNA *ptr,
                        struct PointerRNA *ob_ptr,
                        const char *vgroup_prop,
                        const char *invert_vgroup_prop,
                        const char *text);

/**
 * Draw modifier error message.
 */
void modifier_panel_end(struct uiLayout *layout, PointerRNA *ptr);

struct PointerRNA *modifier_panel_get_property_pointers(struct Panel *panel,
                                                        struct PointerRNA *r_ob_ptr);

/**
 * Create a panel in the context's region
 */
struct PanelType *modifier_panel_register(struct ARegionType *region_type,
                                          ModifierType type,
                                          PanelDrawFn draw);

/**
 * Add a child panel to the parent.
 *
 * \note To create the panel type's idname, it appends the \a name argument to the \a parent's
 * idname.
 */
struct PanelType *modifier_subpanel_register(struct ARegionType *region_type,
                                             const char *name,
                                             const char *label,
                                             PanelDrawFn draw_header,
                                             PanelDrawFn draw,
                                             struct PanelType *parent);

#ifdef __cplusplus
}
#endif
