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
 * The Original Code is Copyright (C) 2013 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup cmpnodes
 */

#include "node_composite_util.hh"

static bNodeSocketTemplate cmp_node_planetrackdeform_in[] = {
    {SOCK_RGBA, N_("Image"), 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, PROP_NONE}, {-1, ""}};

static bNodeSocketTemplate cmp_node_planetrackdeform_out[] = {
    {SOCK_RGBA, N_("Image")},
    {SOCK_FLOAT, N_("Plane")},
    {-1, ""},
};

static void node_cmp_planetrackdeform_init(bNodeTree *UNUSED(ntree), bNode *node)
{
  NodePlaneTrackDeformData *data = (NodePlaneTrackDeformData *)MEM_callocN(
      sizeof(NodePlaneTrackDeformData), "node plane track deform data");
  data->motion_blur_samples = 16;
  data->motion_blur_shutter = 0.5f;
  node->storage = data;
}

void register_node_type_cmp_planetrackdeform(void)
{
  static bNodeType ntype;

  cmp_node_type_base(
      &ntype, CMP_NODE_PLANETRACKDEFORM, "Plane Track Deform", NODE_CLASS_DISTORT, 0);
  node_type_socket_templates(&ntype, cmp_node_planetrackdeform_in, cmp_node_planetrackdeform_out);
  node_type_init(&ntype, node_cmp_planetrackdeform_init);
  node_type_storage(
      &ntype, "NodePlaneTrackDeformData", node_free_standard_storage, node_copy_standard_storage);

  nodeRegisterType(&ntype);
}
