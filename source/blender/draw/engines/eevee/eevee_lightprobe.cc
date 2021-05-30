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
 * Copyright 2018, Blender Foundation.
 */

/** \file
 * \ingroup eevee
 */

#include "eevee_instance.hh"

namespace blender::eevee {

void LightProbeModule::init()
{
  SceneEEVEE &sce_eevee = inst_.scene->eevee;

  lightcache_ = static_cast<LightCache *>(sce_eevee.light_cache_data);

  // if (use_lookdev || lightcache_ == nullptr || lightcache_->validate() == false) {
  if (lookdev_lightcache_ == nullptr) {
    int cube_len = 1;
    int grid_len = 1;
    int irr_samples_len = 1;

    ivec3 irr_size;
    LightCache::irradiance_cache_size_get(
        sce_eevee.gi_visibility_resolution, irr_samples_len, irr_size);

    lookdev_lightcache_ = new LightCache(grid_len,
                                         cube_len,
                                         sce_eevee.gi_cubemap_resolution,
                                         sce_eevee.gi_visibility_resolution,
                                         irr_size);

    do_world_update_ = true;
  }
  lightcache_ = lookdev_lightcache_;
  // }
  // else {
  // OBJECT_GUARDED_SAFE_DELETE(lookdev_lightcache_, LightCache);
  // }

  for (DRWView *&view : face_view_) {
    view = nullptr;
  }

  info_data_.grids.irradiance_cells_per_row = lookdev_lightcache_->irradiance_cells_per_row_get();
  info_data_.grids.visibility_size = lookdev_lightcache_->vis_res;
  info_data_.grids.visibility_cells_per_row = lookdev_lightcache_->grid_tx.tex_size[0] /
                                              info_data_.grids.visibility_size;
  info_data_.grids.visibility_cells_per_layer = (lookdev_lightcache_->grid_tx.tex_size[1] /
                                                 info_data_.grids.visibility_size) *
                                                info_data_.grids.visibility_cells_per_row;

  glossy_clamp_ = sce_eevee.gi_glossy_clamp;
  filter_quality_ = clamp_f(sce_eevee.gi_filter_quality, 1.0f, 8.0f);
}

void LightProbeModule::begin_sync()
{
  {
    cube_downsample_ps_ = DRW_pass_create("Downsample.Cube", DRW_STATE_WRITE_COLOR);

    GPUShader *sh = inst_.shaders.static_shader_get(LIGHTPROBE_FILTER_DOWNSAMPLE_CUBE);
    DRWShadingGroup *grp = DRW_shgroup_create(sh, cube_downsample_ps_);
    DRW_shgroup_uniform_texture_ref(grp, "input_tx", &cube_downsample_input_tx_);
    DRW_shgroup_uniform_block(grp, "filter_block", filter_data_.ubo_get());
    DRW_shgroup_call_procedural_triangles(grp, nullptr, 6);
  }
  {
    filter_glossy_ps_ = DRW_pass_create("Filter.GlossyMip", DRW_STATE_WRITE_COLOR);

    GPUShader *sh = inst_.shaders.static_shader_get(LIGHTPROBE_FILTER_GLOSSY);
    DRWShadingGroup *grp = DRW_shgroup_create(sh, filter_glossy_ps_);
    DRW_shgroup_uniform_texture_ref(grp, "radiance_tx", &cube_downsample_input_tx_);
    DRW_shgroup_uniform_block(grp, "filter_block", filter_data_.ubo_get());
    DRW_shgroup_call_procedural_triangles(grp, nullptr, 6);
  }
  {
    filter_diffuse_ps_ = DRW_pass_create("Filter.Diffuse", DRW_STATE_WRITE_COLOR);

    GPUShader *sh = inst_.shaders.static_shader_get(LIGHTPROBE_FILTER_DIFFUSE);
    DRWShadingGroup *grp = DRW_shgroup_create(sh, filter_diffuse_ps_);
    DRW_shgroup_uniform_texture_ref(grp, "radiance_tx", &cube_downsample_input_tx_);
    DRW_shgroup_uniform_block(grp, "filter_block", filter_data_.ubo_get());
    DRW_shgroup_call_procedural_triangles(grp, nullptr, 1);
  }
  {
    filter_visibility_ps_ = DRW_pass_create("Filter.Visibility", DRW_STATE_WRITE_COLOR);

    GPUShader *sh = inst_.shaders.static_shader_get(LIGHTPROBE_FILTER_VISIBILITY);
    DRWShadingGroup *grp = DRW_shgroup_create(sh, filter_visibility_ps_);
    DRW_shgroup_uniform_texture_ref(grp, "depth_tx", &cube_downsample_input_tx_);
    DRW_shgroup_uniform_block(grp, "filter_block", filter_data_.ubo_get());
    DRW_shgroup_call_procedural_triangles(grp, nullptr, 1);
  }
}

void LightProbeModule::cubeface_winmat_get(mat4 &winmat, float near, float far)
{
  /* Simple 90° FOV projection. */
  perspective_m4(winmat, -near, near, -near, near, near, far);
}

void LightProbeModule::cubemap_prepare(vec3 &position, float near, float far)
{
  SceneEEVEE &sce_eevee = inst_.scene->eevee;
  int cube_res = sce_eevee.gi_cubemap_resolution;
  int cube_mip_count = (int)log2_ceil_u(cube_res);

  mat4 viewmat;
  unit_m4(viewmat);
  negate_v3_v3(viewmat[3], position);

  /* TODO(fclem) We might want to have theses as temporary textures. */
  cube_depth_tx_.ensure_cubemap("CubemapDepth", cube_res, cube_mip_count, GPU_DEPTH_COMPONENT32F);
  cube_color_tx_.ensure_cubemap("CubemapColor", cube_res, cube_mip_count, GPU_RGBA16F);
  GPU_texture_mipmap_mode(cube_color_tx_, true, true);

  cube_downsample_fb_.ensure(GPU_ATTACHMENT_TEXTURE(cube_depth_tx_),
                             GPU_ATTACHMENT_TEXTURE(cube_color_tx_));

  filter_cube_fb_.ensure(GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(lightcache_->cube_tx.tex));
  filter_grid_fb_.ensure(GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(lightcache_->grid_tx.tex));

  mat4 winmat;
  cubeface_winmat_get(winmat, near, far);
  for (int face : IndexRange(6)) {
    face_fb_[face].ensure(GPU_ATTACHMENT_TEXTURE_CUBEFACE_MIP(cube_depth_tx_, face, 0),
                          GPU_ATTACHMENT_TEXTURE_CUBEFACE_MIP(cube_color_tx_, face, 0));
    mat4 facemat;
    mul_m4_m4m4(facemat, winmat, cubeface_mat[face]);

    DRWView *&view = face_view_[face];
    if (view == nullptr) {
      view = DRW_view_create(viewmat, facemat, nullptr, nullptr, nullptr);
    }
    else {
      DRW_view_update(view, viewmat, facemat, nullptr, nullptr);
    }
  }
}

void LightProbeModule::filter_glossy(int cube_index, float intensity)
{
  DRW_stats_group_start("Filter.Glossy");

  filter_data_.instensity_fac = intensity;
  filter_data_.target_layer = cube_index * 6;

  int level_max = lightcache_->mips_len;
  for (int level = 0; level <= level_max; level++) {
    filter_data_.luma_max = (glossy_clamp_ > 0.0f) ? glossy_clamp_ : 1e16f;
    /* Disney Roughness. */
    filter_data_.roughness = square_f(level / (float)level_max);
    /* Distribute Roughness across lod more evenly. */
    filter_data_.roughness = square_f(filter_data_.roughness);
    /* Avoid artifacts. */
    filter_data_.roughness = clamp_f(filter_data_.roughness, 1e-4f, 0.9999f);
    /* Variable sample count and bias to make first levels faster. */
    switch (level) {
      case 0:
        filter_data_.sample_count = 1.0f;
        filter_data_.lod_bias = -1.0f;
        break;
      case 1:
        filter_data_.sample_count = filter_quality_ * 32.0f;
        filter_data_.lod_bias = 1.0f;
        break;
      case 2:
        filter_data_.sample_count = filter_quality_ * 40.0f;
        filter_data_.lod_bias = 2.0f;
        break;
      case 3:
        filter_data_.sample_count = filter_quality_ * 64.0f;
        filter_data_.lod_bias = 2.0f;
        break;
      default:
        filter_data_.sample_count = filter_quality_ * 128.0f;
        filter_data_.lod_bias = 2.0f;
        break;
    }
    /* Add automatic LOD bias (based on target size). */
    filter_data_.lod_bias += lod_bias_from_cubemap();

    filter_data_.push_update();

    filter_cube_fb_.ensure(GPU_ATTACHMENT_NONE,
                           GPU_ATTACHMENT_TEXTURE_MIP(lightcache_->cube_tx.tex, level));
    GPU_framebuffer_bind(filter_cube_fb_);
    DRW_draw_pass(filter_glossy_ps_);
  }

  DRW_stats_group_end();
}

void LightProbeModule::filter_diffuse(int sample_index, float intensity)
{
  filter_data_.instensity_fac = intensity;
  filter_data_.target_layer = 0;
  filter_data_.luma_max = 1e16f;
  filter_data_.sample_count = 1024.0f;
  filter_data_.lod_bias = lod_bias_from_cubemap();

  filter_data_.push_update();

  ivec2 extent = ivec2(3, 2);
  ivec2 offset = extent;
  offset.x *= sample_index % info_data_.grids.irradiance_cells_per_row;
  offset.y *= sample_index / info_data_.grids.irradiance_cells_per_row;

  GPU_framebuffer_bind(filter_grid_fb_);
  // GPU_framebuffer_viewport_set(filter_grid_fb_, UNPACK2(offset), UNPACK2(extent));
  DRW_draw_pass(filter_diffuse_ps_);
  // GPU_framebuffer_viewport_reset(filter_grid_fb_);
}

void LightProbeModule::filter_visibility(int sample_index,
                                         float visibility_blur,
                                         float visibility_range)
{
  ivec2 extent = ivec2(3, 2);
  ivec2 offset = extent;
  offset.x *= sample_index % info_data_.grids.visibility_cells_per_row;
  offset.y *= (sample_index / info_data_.grids.visibility_cells_per_row) %
              info_data_.grids.visibility_cells_per_layer;

  filter_data_.target_layer = 1 + sample_index / info_data_.grids.visibility_cells_per_layer;
  filter_data_.sample_count = 512.0f; /* TODO refine */
  filter_data_.visibility_blur = visibility_blur;
  filter_data_.visibility_range = visibility_range;

  filter_data_.push_update();

  GPU_framebuffer_bind(filter_grid_fb_);
  GPU_framebuffer_viewport_set(filter_grid_fb_, UNPACK2(offset), UNPACK2(extent));
  DRW_draw_pass(filter_visibility_ps_);
  GPU_framebuffer_viewport_reset(filter_grid_fb_);
}

void LightProbeModule::update_world_cache()
{
  DRW_stats_group_start("LightProbe.world");

  const DRWView *view_active = DRW_view_get_active();

  vec3 position(0.0f);
  cubemap_prepare(position, 0.01f, 1.0f);

  auto probe_render = [&]() { inst_.shading_passes.background.render(); };
  cubemap_render(probe_render);

  filter_glossy(0, 1.0f);

  /* TODO(fclem) Change ray type. */
  /* OPTI(fclem) Only re-render if there is a light path node in the world material. */
  // cubemap_render(probe_render);

  filter_diffuse(0, 1.0f);

  if (view_active != nullptr) {
    DRW_view_set_active(view_active);
  }

  do_world_update_ = false;

  DRW_stats_group_end();
}

/* Push world probe to first grid and cubemap slots. */
void LightProbeModule::update_world_data(const DRWView *view)
{
  BoundSphere view_bounds = DRW_view_frustum_bsphere_get(view);
  /* Playing safe. The fake grid needs to be bigger than the frustum. */
  view_bounds.radius = clamp_f(view_bounds.radius * 2.0, 0.0f, FLT_MAX);

  CubemapData &cube = cube_data_[0];
  GridData &grid = grid_data_[0];

  scale_m4_fl(grid.local_mat, view_bounds.radius);
  negate_v3_v3(grid.local_mat[3], view_bounds.center);
  copy_m4_m4(cube.object_mat, grid.local_mat);
  copy_m4_m4(cube.parallax_mat, cube.object_mat);

  grid.resolution = ivec3(1);
  grid.offset = 0;
  grid.level_skip = 0;
  grid.attenuation_bias = 0.001f;
  grid.attenuation_scale = 1.0f;
  grid.visibility_range = 1.0f;
  grid.visibility_bleed = 0.001f;
  grid.visibility_bias = 0.0f;
  grid.increment_x = vec3(0.0f);
  grid.increment_y = vec3(0.0f);
  grid.increment_z = vec3(0.0f);
  grid.corner = vec3(0.0f);

  cube._parallax_type = CUBEMAP_SHAPE_SPHERE;
  cube._layer = 0.0;
}

void LightProbeModule::set_view(const DRWView *view, const ivec2 UNUSED(extent))
{
  if (do_world_update_) {
    update_world_cache();
  }

  update_world_data(view);

  info_data_.grids.grid_count = 1;
  info_data_.cubes.cube_count = 1;
  info_data_.cubes.roughness_max_lod = lightcache_->mips_len;

  active_grid_tx_ = lightcache_->grid_tx.tex;
  active_cube_tx_ = lightcache_->cube_tx.tex;

  info_data_.push_update();
  grid_data_.push_update();
  cube_data_.push_update();
}

}  // namespace blender::eevee
