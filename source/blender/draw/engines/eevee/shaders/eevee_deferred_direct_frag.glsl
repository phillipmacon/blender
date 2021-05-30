
/**
 * Direct lighting evaluation: Evaluate lights and light-probes contributions for all bsdfs.
 **/

#pragma BLENDER_REQUIRE(common_view_lib.glsl)
#pragma BLENDER_REQUIRE(common_math_lib.glsl)
#pragma BLENDER_REQUIRE(eevee_culling_iter_lib.glsl)
#pragma BLENDER_REQUIRE(eevee_gbuffer_lib.glsl)
#pragma BLENDER_REQUIRE(eevee_light_lib.glsl)
#pragma BLENDER_REQUIRE(eevee_lightprobe_eval_cubemap_lib.glsl)
#pragma BLENDER_REQUIRE(eevee_lightprobe_eval_grid_lib.glsl)
#pragma BLENDER_REQUIRE(eevee_sampling_lib.glsl)
#pragma BLENDER_REQUIRE(eevee_shader_shared.hh)
#pragma BLENDER_REQUIRE(eevee_shadow_lib.glsl)

layout(std140) uniform sampling_block
{
  SamplingData sampling;
};

layout(std140) uniform lights_block
{
  LightData lights[CULLING_ITEM_BATCH];
};

layout(std140) uniform lights_culling_block
{
  CullingData light_culling;
};

layout(std140) uniform shadows_punctual_block
{
  ShadowPunctualData shadows_punctual[CULLING_ITEM_BATCH];
};

layout(std140) uniform grids_block
{
  GridData grids[GRID_MAX];
};

layout(std140) uniform cubes_block
{
  CubemapData cubes[CULLING_ITEM_BATCH];
};

layout(std140) uniform lightprobes_info_block
{
  LightProbeInfoData probes_info;
};

uniform sampler2D depth_tx;
uniform sampler2D emission_data_tx;
uniform usampler2D diffuse_data_tx;
uniform usampler2D reflection_data_tx;
uniform usampler2D lights_culling_tx;
uniform sampler2DArray utility_tx;
uniform sampler2DShadow shadow_atlas_tx;
uniform sampler2DArray lightprobe_grid_tx;
uniform samplerCubeArray lightprobe_cube_tx;

utility_tx_fetch_define(utility_tx);
utility_tx_sample_define(utility_tx);

in vec4 uvcoordsvar;

layout(location = 0) out vec4 out_combined;
layout(location = 1) out vec3 out_diffuse;
layout(location = 2) out vec3 out_specular;

void main(void)
{
  float gbuffer_depth = texture(depth_tx, uvcoordsvar.xy).r;
  vec3 vP = get_view_space_from_depth(uvcoordsvar.xy, gbuffer_depth);
  vec3 P = point_view_to_world(vP);
  vec3 V = cameraVec(P);

  ClosureEmission emission = gbuffer_load_emission_data(emission_data_tx, uvcoordsvar.xy);
  ClosureDiffuse diffuse = gbuffer_load_diffuse_data(diffuse_data_tx, uvcoordsvar.xy);
  ClosureReflection reflection = gbuffer_load_reflection_data(reflection_data_tx, uvcoordsvar.xy);

  vec2 uv = vec2(reflection.roughness, safe_sqrt(1.0 - dot(reflection.N, V)));
  uv = uv * UTIL_TEX_UV_SCALE + UTIL_TEX_UV_BIAS;
  vec4 ltc_mat = utility_tx_sample(uv, UTIL_LTC_MAT_LAYER);
  float ltc_mag = utility_tx_sample(uv, UTIL_LTC_MAG_LAYER).x;

  vec3 radiance_diffuse = vec3(0);
  vec3 radiance_reflection = vec3(0);

  LIGHTS_EVAL(lights,
              shadow_atlas_tx,
              shadows_punctual,
              utility_tx,
              light_culling,
              lights_culling_tx,
              vP.z,
              P,
              V,
              diffuse,
              reflection,
              ltc_mat,
              radiance_diffuse,
              radiance_reflection);

  {
    float noise_offset = sampling_rng_1D_get(sampling, SAMPLING_LIGHTPROBE);
    float noise = utility_tx_fetch(gl_FragCoord.xy, UTIL_BLUE_NOISE_LAYER).r;
    float random_probe = fract(noise + noise_offset);

    int grid_index;
    lightprobe_grid_select(probes_info.grids, grids, P, random_probe, grid_index);

    radiance_diffuse += lightprobe_grid_evaluate(
        probes_info.grids, lightprobe_grid_tx, grids[grid_index], P, diffuse.N);

    int cube_index;
    lightprobe_cubemap_select(probes_info.cubes, cubes, P, random_probe, cube_index);

    vec3 R = -reflect(V, reflection.N);
    radiance_reflection += lightprobe_cubemap_evaluate(
        probes_info.cubes, lightprobe_cube_tx, cubes[cube_index], P, R, reflection.roughness);
  }

  out_diffuse = radiance_diffuse * diffuse.color;
  out_specular = radiance_reflection * reflection.color;
  out_combined = vec4(out_diffuse + out_specular + emission.emission, 0.0);
}