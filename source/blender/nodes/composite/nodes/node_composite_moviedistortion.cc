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
 * The Original Code is Copyright (C) 2011 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup cmpnodes
 */

#include "node_composite_util.hh"

#include "BKE_context.h"
#include "BKE_lib_id.h"

/* **************** Translate  ******************** */

static bNodeSocketTemplate cmp_node_moviedistortion_in[] = {
    {SOCK_RGBA, N_("Image"), 0.8f, 0.8f, 0.8f, 1.0f, 0.0f, 1.0f},
    {-1, ""},
};

static bNodeSocketTemplate cmp_node_moviedistortion_out[] = {
    {SOCK_RGBA, N_("Image")},
    {-1, ""},
};

static void cmp_node_moviedistortion_label(bNodeTree *UNUSED(ntree),
                                           bNode *node,
                                           char *label,
                                           int maxlen)
{
  if (node->custom1 == 0) {
    BLI_strncpy(label, IFACE_("Undistortion"), maxlen);
  }
  else {
    BLI_strncpy(label, IFACE_("Distortion"), maxlen);
  }
}

static void cmp_node_moviedistortion_init(const bContext *C, PointerRNA *ptr)
{
  bNode *node = (bNode *)ptr->data;
  Scene *scene = CTX_data_scene(C);

  node->id = (ID *)scene->clip;
  id_us_plus(node->id);
}

static void cmp_node_moviedistortion_storage_free(bNode *node)
{
  if (node->storage) {
    BKE_tracking_distortion_free((MovieDistortion *)node->storage);
  }

  node->storage = nullptr;
}

static void cmp_node_moviedistortion_storage_copy(bNodeTree *UNUSED(dest_ntree),
                                                  bNode *dest_node,
                                                  const bNode *src_node)
{
  if (src_node->storage) {
    dest_node->storage = BKE_tracking_distortion_copy((MovieDistortion *)src_node->storage);
  }
}

void register_node_type_cmp_moviedistortion(void)
{
  static bNodeType ntype;

  cmp_node_type_base(&ntype, CMP_NODE_MOVIEDISTORTION, "Movie Distortion", NODE_CLASS_DISTORT, 0);
  node_type_socket_templates(&ntype, cmp_node_moviedistortion_in, cmp_node_moviedistortion_out);
  node_type_label(&ntype, cmp_node_moviedistortion_label);

  ntype.initfunc_api = cmp_node_moviedistortion_init;
  node_type_storage(&ntype,
                    nullptr,
                    cmp_node_moviedistortion_storage_free,
                    cmp_node_moviedistortion_storage_copy);

  nodeRegisterType(&ntype);
}
