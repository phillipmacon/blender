/*
 * Copyright 2019 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

CCL_NAMESPACE_BEGIN

/* Check whether the pixel has converged and should not be sampled anymore. */

ccl_device_forceinline bool kernel_need_sample_pixel(INTEGRATOR_STATE_CONST_ARGS,
                                                     ccl_global float *render_buffer)
{
  if (kernel_data.film.pass_adaptive_aux_buffer == PASS_UNUSED) {
    return true;
  }

  const uint32_t render_pixel_index = INTEGRATOR_STATE(path, render_pixel_index);
  const uint64_t render_buffer_offset = (uint64_t)render_pixel_index *
                                        kernel_data.film.pass_stride;
  ccl_global float *buffer = render_buffer + render_buffer_offset;

  ccl_global float4 *aux = (ccl_global float4 *)(buffer +
                                                 kernel_data.film.pass_adaptive_aux_buffer);
  return (*aux).w == 0.0f;
}

/* Determines whether to continue sampling a given pixel or if it has sufficiently converged. */

ccl_device bool kernel_adaptive_sampling_convergence_check(const KernelGlobals *kg,
                                                           ccl_global float *render_buffer,
                                                           int x,
                                                           int y,
                                                           int sample,
                                                           int offset,
                                                           int stride)
{
  kernel_assert(kernel_data.film.pass_adaptive_aux_buffer != PASS_UNUSED);

  const int render_pixel_index = offset + x + y * stride;
  ccl_global float *buffer = render_buffer +
                             (uint64_t)render_pixel_index * kernel_data.film.pass_stride;

  /* TODO(Stefan): Is this better in linear, sRGB or something else? */

  const float4 A = *(ccl_global float4 *)(buffer + kernel_data.film.pass_adaptive_aux_buffer);
  if (A.w != 0.0f) {
    /* If the pixel was considered converged, its state will not change in this kernmel. Early
     * output before doing any math.
     *
     * TODO(sergey): On a GPU it might be better to keep thread alive for better coherency? */
    return true;
  }

  const float4 I = *((ccl_global float4 *)buffer);

  /* The per pixel error as seen in section 2.1 of
   * "A hierarchical automatic stopping condition for Monte Carlo global illumination"
   * A small epsilon is added to the divisor to prevent division by zero. */
  const float error = (fabsf(I.x - A.x) + fabsf(I.y - A.y) + fabsf(I.z - A.z)) /
                      (sample * 0.0001f + sqrtf(I.x + I.y + I.z));
  if (error < kernel_data.integrator.adaptive_threshold * (float)sample) {
    /* Set the fourth component to non-zero value to indicate that this pixel has converged. */
    buffer[kernel_data.film.pass_adaptive_aux_buffer + 3] += 1.0f;
    return true;
  }

  return false;
}

/* This is a simple box filter in two passes.
 * When a pixel demands more adaptive samples, let its neighboring pixels draw more samples too. */

ccl_device void kernel_adaptive_sampling_filter_x(const KernelGlobals *kg,
                                                  ccl_global float *render_buffer,
                                                  int y,
                                                  int start_x,
                                                  int width,
                                                  int offset,
                                                  int stride)
{
  kernel_assert(kernel_data.film.pass_adaptive_aux_buffer != PASS_UNUSED);

  bool prev = false;
  for (int x = start_x; x < start_x + width; ++x) {
    int index = offset + x + y * stride;
    ccl_global float *buffer = render_buffer + index * kernel_data.film.pass_stride;
    ccl_global float4 *aux = (ccl_global float4 *)(buffer +
                                                   kernel_data.film.pass_adaptive_aux_buffer);
    if ((*aux).w == 0.0f) {
      if (x > start_x && !prev) {
        index = index - 1;
        buffer = render_buffer + index * kernel_data.film.pass_stride;
        aux = (ccl_global float4 *)(buffer + kernel_data.film.pass_adaptive_aux_buffer);
        (*aux).w = 0.0f;
      }
      prev = true;
    }
    else {
      if (prev) {
        (*aux).w = 0.0f;
      }
      prev = false;
    }
  }
}

ccl_device void kernel_adaptive_sampling_filter_y(const KernelGlobals *kg,
                                                  ccl_global float *render_buffer,
                                                  int x,
                                                  int start_y,
                                                  int height,
                                                  int offset,
                                                  int stride)
{
  kernel_assert(kernel_data.film.pass_adaptive_aux_buffer != PASS_UNUSED);

  bool prev = false;
  for (int y = start_y; y < start_y + height; ++y) {
    int index = offset + x + y * stride;
    ccl_global float *buffer = render_buffer + index * kernel_data.film.pass_stride;
    ccl_global float4 *aux = (ccl_global float4 *)(buffer +
                                                   kernel_data.film.pass_adaptive_aux_buffer);
    if ((*aux).w == 0.0f) {
      if (y > start_y && !prev) {
        index = index - stride;
        buffer = render_buffer + index * kernel_data.film.pass_stride;
        aux = (ccl_global float4 *)(buffer + kernel_data.film.pass_adaptive_aux_buffer);
        (*aux).w = 0.0f;
      }
      prev = true;
    }
    else {
      if (prev) {
        (*aux).w = 0.0f;
      }
      prev = false;
    }
  }
}

CCL_NAMESPACE_END
