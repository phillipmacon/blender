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

#pragma once

/** \file
 * \ingroup bke
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \see #BKE_mesh_calc_loop_tangent, same logic but used arrays instead of #BMesh data.
 *
 * \note This function is not so normal, its using #BMesh.ldata as input,
 * but output's to #Mesh.ldata.
 * This is done because #CD_TANGENT is cache data used only for drawing.
 */
void BKE_editmesh_loop_tangent_calc(BMEditMesh *em,
                                    bool calc_active_tangent,
                                    const char (*tangent_names)[MAX_NAME],
                                    int tangent_names_len,
                                    const float (*poly_normals)[3],
                                    const float (*loop_normals)[3],
                                    const float (*vert_orco)[3],
                                    CustomData *dm_loopdata_out,
                                    const uint dm_loopdata_out_len,
                                    short *tangent_mask_curr_p);

#ifdef __cplusplus
}
#endif
