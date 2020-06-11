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
 * The Original Code is Copyright (C) 2008 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup obj
 */

#include "MEM_guardedalloc.h"

#include <stdio.h>
#include <vector>

#include "BKE_mesh.h"
#include "BKE_mesh_mapping.h"
#include "BKE_mesh_runtime.h"
#include "BKE_scene.h"

#include "BLI_math.h"
#include "BLI_vector.hh"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "IO_wavefront_obj.h"

#include "wavefront_obj.hh"
#include "wavefront_obj_exporter.hh"
#include "wavefront_obj_file_handler.hh"

namespace io {
namespace obj {

/**
 * Store the mesh vertex coordinates in data_to_export, in world coordinates.
 */
static void get_transformed_mesh_vertices(Mesh *me_eval,
                                          Object *ob_eval,
                                          OBJ_data_to_export *data_to_export)
{
  uint num_verts = data_to_export->tot_vertices = me_eval->totvert;
  float transformed_co[3];
  data_to_export->mvert = (MVert *)MEM_callocN(num_verts * sizeof(MVert),
                                               "OBJ vertex coordinates & normals");

  for (uint i = 0; i < num_verts; i++) {
    copy_v3_v3(transformed_co, me_eval->mvert[i].co);
    mul_m4_v3(ob_eval->obmat, transformed_co);
    copy_v3_v3(data_to_export->mvert[i].co, transformed_co);
  }
}

/**
 * Store the mesh vertex normals in data_to_export, in world coordinates.
 * Memory for data_to_export->mvert pre-allocated in get_transformed_mesh_vertices.
 */
static void get_transformed_vertex_normals(Mesh *me_eval,
                                           Object *ob_eval,
                                           OBJ_data_to_export *data_to_export)
{
  BKE_mesh_ensure_normals(me_eval);
  float transformed_normal[3];
  for (uint i = 0; i < me_eval->totvert; i++) {
    normal_short_to_float_v3(transformed_normal, me_eval->mvert[i].no);
    mul_mat3_m4_v3(ob_eval->obmat, transformed_normal);
    normal_float_to_short_v3(data_to_export->mvert[i].no, transformed_normal);
  }
}

/**
 * Store a polygon's vertex indices, indexing into the pre-defined
 * vertex coordinates list.
 */
static void get_polygon_vert_indices(Mesh *me_eval, OBJ_data_to_export *data_to_export)
{
  const MLoop *mloop;
  const MPoly *mpoly = me_eval->mpoly;

  data_to_export->tot_poly = me_eval->totpoly;
  data_to_export->polygon_list.resize(me_eval->totpoly);

  for (uint i = 0; i < me_eval->totpoly; i++, mpoly++) {
    mloop = &me_eval->mloop[mpoly->loopstart];
    data_to_export->polygon_list[i].total_vertices_per_poly = mpoly->totloop;

    data_to_export->polygon_list[i].vertex_index.resize(mpoly->totloop);

    for (int j = 0; j < mpoly->totloop; j++) {
      /* mloop->v is 0-based index. Indices in OBJ start from 1. */
      data_to_export->polygon_list[i].vertex_index[j] = (mloop + j)->v + 1;
    }
  }
}
/**
 * Store UV vertex coordinates in data_to_export->uv_coords as well as their indices, in
 * a polygon[i].uv_vertex_index.
 */
static void get_uv_coordinates(Mesh *me_eval, OBJ_data_to_export *data_to_export)
{
  const CustomData *ldata = &me_eval->ldata;

  for (int layer_idx = 0; layer_idx < ldata->totlayer; layer_idx++) {
    const CustomDataLayer *layer = &ldata->layers[layer_idx];
    if (layer->type != CD_MLOOPUV) {
      continue;
    }

    const MPoly *mpoly = me_eval->mpoly;
    const MLoop *mloop = me_eval->mloop;
    const MLoopUV *mloopuv = static_cast<MLoopUV *>(layer->data);
    const float limit[2] = {STD_UV_CONNECT_LIMIT, STD_UV_CONNECT_LIMIT};

    UvVertMap *uv_vert_map = BKE_mesh_uv_vert_map_create(
        mpoly, mloop, mloopuv, me_eval->totpoly, me_eval->totvert, limit, false, false);

    data_to_export->tot_uv_vertices = 0;
    for (int vertex_index = 0; vertex_index < me_eval->totvert; vertex_index++) {
      const UvMapVert *uv_vert = BKE_mesh_uv_vert_map_get_vert(uv_vert_map, vertex_index);
      while (uv_vert != NULL) {
        if (uv_vert->separate) {
          data_to_export->tot_uv_vertices++;
        }
        Polygon &polygon_of_uv_vert = data_to_export->polygon_list[uv_vert->poly_index];
        const uint vertices_in_poly = polygon_of_uv_vert.total_vertices_per_poly;
        /* Resize UV vertices index list. */
        polygon_of_uv_vert.uv_vertex_index.resize(vertices_in_poly);

        /* Fill up UV vertex index for current polygon's one vertex. */
        polygon_of_uv_vert.uv_vertex_index[uv_vert->loop_of_poly_index] =
            data_to_export->tot_uv_vertices;

        /* Fill up UV vertices' coordinates. We don't know how many unique vertices are there, so
         * need to push back everytime. */
        data_to_export->uv_coords.push_back(std::array<float, 2>());
        data_to_export->uv_coords[data_to_export->tot_uv_vertices][0] =
            mloopuv[mpoly[uv_vert->poly_index].loopstart + uv_vert->loop_of_poly_index].uv[0];
        data_to_export->uv_coords[data_to_export->tot_uv_vertices][1] =
            mloopuv[mpoly[uv_vert->poly_index].loopstart + uv_vert->loop_of_poly_index].uv[1];

        uv_vert = uv_vert->next;
      }
    }
    BKE_mesh_uv_vert_map_free(uv_vert_map);
    /* No need to go over other layers. */
    break;
  }
}

static void get_geometry_per_object(const OBJExportParams *export_params,
                                    OBJ_data_to_export *data_to_export)
{
  Depsgraph *depsgraph = data_to_export->depsgraph;
  Object *ob = CTX_data_active_object(data_to_export->C);
  Object *ob_eval = data_to_export->ob_eval = DEG_get_evaluated_object(depsgraph, ob);
  Mesh *me_eval = BKE_object_get_evaluated_mesh(ob_eval);

  get_transformed_mesh_vertices(me_eval, ob_eval, data_to_export);
  get_transformed_vertex_normals(me_eval, ob_eval, data_to_export);
  get_polygon_vert_indices(me_eval, data_to_export);
  get_uv_coordinates(me_eval, data_to_export);
}

/**
 * Central internal function to call geometry data preparation & writer functions.
 */
void exporter_main(bContext *C, const OBJExportParams *export_params)
{
  const char *filepath = export_params->filepath;
  struct OBJ_data_to_export data_to_export;
  data_to_export.C = C;
  data_to_export.depsgraph = CTX_data_ensure_evaluated_depsgraph(C);

  get_geometry_per_object(export_params, &data_to_export);

  /* Comment out either one of the following. */
  //  write_obj_data(filepath, &data_to_export);
  write_obj_data_fprintf(filepath, &data_to_export);
  MEM_freeN(data_to_export.mvert);
}
}  // namespace obj
}  // namespace io
