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
 * Copyright 2021, Blender Foundation.
 */

/** \file
 * \ingroup draw_engine
 */

#pragma once

#include "image_private.hh"

namespace blender::draw::image_engine {

class SpaceNodeAccessor : public AbstractSpaceAccessor {
  SpaceNode *snode;

 public:
  SpaceNodeAccessor(SpaceNode *snode) : snode(snode)
  {
  }

  Image *get_image(Main *bmain) override
  {
    return BKE_image_ensure_viewer(bmain, IMA_TYPE_COMPOSITE, "Viewer Node");
  }

  ImageUser *get_image_user() override
  {
    return nullptr;
  }

  ImBuf *acquire_image_buffer(Image *image, void **lock) override
  {
    return BKE_image_acquire_ibuf(image, nullptr, lock);
  }

  void release_buffer(Image *image, ImBuf *ibuf, void *lock) override
  {
    BKE_image_release_ibuf(image, ibuf, lock);
  }

  void get_shader_parameters(ShaderParameters &r_shader_parameters,
                             ImBuf *ibuf,
                             bool UNUSED(is_tiled)) override
  {
    if ((snode->flag & SNODE_USE_ALPHA) != 0) {
      /* Show RGBA */
      r_shader_parameters.flags |= IMAGE_DRAW_FLAG_SHOW_ALPHA | IMAGE_DRAW_FLAG_APPLY_ALPHA;
    }
    else if ((snode->flag & SNODE_SHOW_ALPHA) != 0) {
      r_shader_parameters.flags |= IMAGE_DRAW_FLAG_SHUFFLING;
      copy_v4_fl4(r_shader_parameters.shuffle, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    else if ((snode->flag & SNODE_SHOW_R) != 0) {
      r_shader_parameters.flags |= IMAGE_DRAW_FLAG_SHUFFLING;
      if (IMB_alpha_affects_rgb(ibuf)) {
        r_shader_parameters.flags |= IMAGE_DRAW_FLAG_APPLY_ALPHA;
      }
      copy_v4_fl4(r_shader_parameters.shuffle, 1.0f, 0.0f, 0.0f, 0.0f);
    }
    else if ((snode->flag & SNODE_SHOW_G) != 0) {
      r_shader_parameters.flags |= IMAGE_DRAW_FLAG_SHUFFLING;
      if (IMB_alpha_affects_rgb(ibuf)) {
        r_shader_parameters.flags |= IMAGE_DRAW_FLAG_APPLY_ALPHA;
      }
      copy_v4_fl4(r_shader_parameters.shuffle, 0.0f, 1.0f, 0.0f, 0.0f);
    }
    else if ((snode->flag & SNODE_SHOW_B) != 0) {
      r_shader_parameters.flags |= IMAGE_DRAW_FLAG_SHUFFLING;
      if (IMB_alpha_affects_rgb(ibuf)) {
        r_shader_parameters.flags |= IMAGE_DRAW_FLAG_APPLY_ALPHA;
      }
      copy_v4_fl4(r_shader_parameters.shuffle, 0.0f, 0.0f, 1.0f, 0.0f);
    }
    else /* RGB */ {
      if (IMB_alpha_affects_rgb(ibuf)) {
        r_shader_parameters.flags |= IMAGE_DRAW_FLAG_APPLY_ALPHA;
      }
    }
  }

  void get_gpu_textures(Image *image,
                        ImageUser *iuser,
                        ImBuf *ibuf,
                        GPUTexture **r_gpu_texture,
                        bool *r_owns_texture,
                        GPUTexture **r_tex_tile_data) override
  {
    *r_gpu_texture = BKE_image_get_gpu_texture(image, iuser, ibuf);
    *r_owns_texture = false;
    *r_tex_tile_data = nullptr;
  }

  bool use_tile_drawing() const override
  {
    return false;
  }

  /**
   * The backdrop of the node editor isn't drawn in screen space UV space. But is locked with the
   * screen.
   */
  void init_ss_to_texture_matrix(const ARegion *region,
                                 const float image_resolution[2],
                                 float r_uv_to_texture[4][4]) const override
  {
    unit_m4(r_uv_to_texture);
    float display_resolution[2];
    mul_v2_v2fl(display_resolution, image_resolution, snode->zoom);
    const float scale_x = display_resolution[0] / region->winx;
    const float scale_y = display_resolution[1] / region->winy;
    const float translate_x = 0.5f - 0.5f * scale_x + snode->xof / image_resolution[0];
    const float translate_y = 0.5 - 0.5f * scale_y + snode->yof / image_resolution[1];

    r_uv_to_texture[0][0] = scale_x;
    r_uv_to_texture[1][1] = scale_y;
    r_uv_to_texture[3][0] = translate_x;
    r_uv_to_texture[3][1] = translate_y;
  }
};

}  // namespace blender::draw::image_engine
