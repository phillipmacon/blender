/*
 * Copyright 2011-2021 Blender Foundation
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

#include "render/pass_accessor.h"

#include "render/buffers.h"
#include "util/util_logging.h"

CCL_NAMESPACE_BEGIN

namespace {

/* Helper class which takes care of calculating sample scale and exposure scale for render passes,
 * taking adaptive sampling into account. */
class Scaler {
 public:
  Scaler(const PassAccessor *pass_accessor,
         RenderBuffers *render_buffers,
         const Pass *pass,
         const float *pass_buffer,
         const int num_samples,
         const float exposure)
      : pass_(pass),
        pass_stride_(render_buffers->params.pass_stride),
        num_samples_inv_(1.0f / num_samples),
        exposure_(exposure),
        sample_count_pass_(get_sample_count_pass(pass_accessor, render_buffers))
  {
    /* Special trick to only scale the samples count pass with the sample scale. Otherwise the pass
     * becomes a uniform 1.0. */
    if (sample_count_pass_ == pass_buffer) {
      sample_count_pass_ = nullptr;
    }

    /* Pre-calculate values when adaptive sampling is not used. */
    if (!sample_count_pass_) {
      scale_ = pass->filter ? num_samples_inv_ : 1.0f;
      scale_exposure_ = pass->exposure ? scale_ * exposure_ : scale_;
    }
  }

  inline float scale(const int pixel_index) const
  {
    if (!sample_count_pass_) {
      return scale_;
    }

    return (pass_->filter) ? 1.0f / (sample_count_pass_[pixel_index * pass_stride_]) : 1.0f;
  }

  inline float scale_exposure(const int pixel_index) const
  {
    if (!sample_count_pass_) {
      return scale_exposure_;
    }

    float scale, scale_exposure;
    scale_and_scale_exposure(pixel_index, scale, scale_exposure);

    return scale_exposure;
  }

  inline void scale_and_scale_exposure(int pixel_index, float &scale, float &scale_exposure) const
  {
    if (!sample_count_pass_) {
      scale = scale_;
      scale_exposure = scale_exposure_;
      return;
    }

    scale = this->scale(pixel_index);
    scale_exposure = (pass_->exposure) ? scale * exposure_ : scale;
  }

 protected:
  const float *get_sample_count_pass(const PassAccessor *pass_accessor,
                                     const RenderBuffers *render_buffers)
  {
    const int pass_sample_count = pass_accessor->get_pass_offset(PASS_SAMPLE_COUNT);
    if (pass_sample_count == PASS_UNUSED) {
      return nullptr;
    }

    return render_buffers->buffer.data() + pass_sample_count;
  }

  const Pass *pass_;
  const int pass_stride_;

  const float num_samples_inv_ = 1.0f;
  const float exposure_ = 1.0f;

  const float *sample_count_pass_ = nullptr;

  float scale_ = 0.0f;
  float scale_exposure_ = 0.0f;
};

} /* namespace */

PassAccessor::PassAccessor(const vector<Pass> &passes,
                           const string &pass_name,
                           int num_components,
                           float exposure,
                           int num_samples)
    : passes_(passes),
      pass_offset_(PASS_UNUSED),
      num_components_(num_components),
      exposure_(exposure),
      num_samples_(num_samples)
{
  int pass_offset = 0;
  for (const Pass &pass : passes_) {
    /* Pass is identified by both type and name, multiple of the same type may exist with a
     * different name. */
    if (pass.name == pass_name) {
      pass_offset_ = pass_offset;
      pass_ = &pass;
      break;
    }
    pass_offset += pass.components;
  }
}

bool PassAccessor::is_valid() const
{
  return pass_ != nullptr;
}

bool PassAccessor::get_pass_rect(RenderBuffers *render_buffers, float *pixels)
{
  if (!pass_) {
    return false;
  }

  if (render_buffers->buffer.data() == nullptr) {
    return false;
  }

  const BufferParams &params = render_buffers->params;

  const float *buffer_data = render_buffers->buffer.data();
  const float *in = buffer_data + pass_offset_;
  const int pass_stride = params.pass_stride;
  const int size = params.width * params.height;

  const PassType type = pass_->type;
  const Scaler scaler(this, render_buffers, pass_, in, num_samples_, exposure_);

  if (num_components_ == 1 && type == PASS_RENDER_TIME) {
#if 0
    /* Render time is not stored by kernel, but measured per tile. */
    const float val = (float)(1000.0 * render_time / (params.width * params.height * sample));
    for (int i = 0; i < size; i++, pixels++) {
      pixels[0] = val;
    }
#endif
  }
  else if (num_components_ == 1) {
    DCHECK_EQ(pass_->components, num_components_)
        << "Number of components mismatch for pass " << pass_->name;

    /* Scalar */
    if (type == PASS_DEPTH) {
      for (int i = 0; i < size; i++, in += pass_stride, pixels++) {
        const float f = *in;
        pixels[0] = (f == 0.0f) ? 1e10f : f * scaler.scale_exposure(i);
      }
    }
    else if (type == PASS_MIST) {
      for (int i = 0; i < size; i++, in += pass_stride, pixels++) {
        const float f = *in;
        /* Note that we accumulate 1 - mist in the kernel to avoid having to
         * track the mist values in the integrator state. */
        pixels[0] = saturate(1.0f - f * scaler.scale_exposure(i));
      }
    }
#ifdef WITH_CYCLES_DEBUG
    else if (type == PASS_BVH_TRAVERSED_NODES || type == PASS_BVH_TRAVERSED_INSTANCES ||
             type == PASS_BVH_INTERSECTIONS || type == PASS_RAY_BOUNCES) {
      for (int i = 0; i < size; i++, in += pass_stride, pixels++) {
        const float f = *in;
        pixels[0] = f * scaler.scale_exposure(i);
      }
    }
#endif
    else {
      for (int i = 0; i < size; i++, in += pass_stride, pixels++) {
        const float f = *in;
        pixels[0] = f * scaler.scale_exposure(i);
      }
    }
  }
  else if (num_components_ == 3) {
    if (pass_->is_unaligned) {
      DCHECK_EQ(pass_->components, 3) << "Number of components mismatch for pass " << pass_->name;
    }
    else {
      DCHECK_EQ(pass_->components, 4) << "Number of components mismatch for pass " << pass_->name;
    }

    /* RGBA */
    if (type == PASS_SHADOW) {
      for (int i = 0; i < size; i++, in += pass_stride, pixels += 3) {
        const float weight = in[3];
        const float weight_inv = (weight > 0.0f) ? 1.0f / weight : 1.0f;

        const float3 shadow = make_float3(in[0], in[1], in[2]) * weight_inv;

        pixels[0] = shadow.x;
        pixels[1] = shadow.y;
        pixels[2] = shadow.z;
      }
    }
    else if (pass_->divide_type != PASS_NONE) {
      /* RGB lighting passes that need to divide out color */
      const int pass_divide = get_pass_offset(pass_->divide_type);
      DCHECK_NE(pass_divide, PASS_UNUSED);

      const float *in_divide = buffer_data + pass_divide;

      for (int i = 0; i < size; i++, in += pass_stride, in_divide += pass_stride, pixels += 3) {
        const float3 f = make_float3(in[0], in[1], in[2]);
        const float3 f_divide = make_float3(in_divide[0], in_divide[1], in_divide[2]);
        const float3 f_divided = safe_divide_even_color(f * exposure_, f_divide);

        pixels[0] = f_divided.x;
        pixels[1] = f_divided.y;
        pixels[2] = f_divided.z;
      }
    }
    else {
      /* RGB/vector */
      for (int i = 0; i < size; i++, in += pass_stride, pixels += 3) {
        const float scale_exposure = scaler.scale_exposure(i);
        const float3 f = make_float3(in[0], in[1], in[2]) * scale_exposure;

        pixels[0] = f.x;
        pixels[1] = f.y;
        pixels[2] = f.z;
      }
    }
  }
  else if (num_components_ == 4) {
    DCHECK_EQ(pass_->components, num_components_)
        << "Number of components mismatch for pass " << pass_->name;

    /* RGBA */
    if (type == PASS_SHADOW) {
      for (int i = 0; i < size; i++, in += pass_stride, pixels += 4) {
        const float weight = in[3];
        const float weight_inv = (weight > 0.0f) ? 1.0f / weight : 1.0f;

        const float3 shadow = make_float3(in[0], in[1], in[2]) * weight_inv;

        pixels[0] = shadow.x;
        pixels[1] = shadow.y;
        pixels[2] = shadow.z;
        pixels[3] = 1.0f;
      }
    }
    else if (type == PASS_MOTION) {
      /* need to normalize by number of samples accumulated for motion */
      const int pass_motion_weight = get_pass_offset(PASS_MOTION_WEIGHT);
      DCHECK_NE(pass_motion_weight, PASS_UNUSED);

      const float *in_weight = buffer_data + pass_motion_weight;

      for (int i = 0; i < size; i++, in += pass_stride, in_weight += pass_stride, pixels += 4) {
        const float weight = in_weight[0];
        const float weight_inv = (weight > 0.0f) ? 1.0f / weight : 0.0f;

        const float4 motion = make_float4(in[0], in[1], in[2], in[3]) * weight_inv;

        pixels[0] = motion.x;
        pixels[1] = motion.y;
        pixels[2] = motion.z;
        pixels[3] = motion.w;
      }
    }
    else if (type == PASS_CRYPTOMATTE) {
      for (int i = 0; i < size; i++, in += pass_stride, pixels += 4) {
        const float scale = scaler.scale(i);

        const float4 f = make_float4(in[0], in[1], in[2], in[3]);
        /* x and z contain integer IDs, don't rescale them.
           y and w contain matte weights, they get scaled. */
        pixels[0] = f.x;
        pixels[1] = f.y * scale;
        pixels[2] = f.z;
        pixels[3] = f.w * scale;
      }
    }
    else if (type == PASS_DENOISING_COLOR) {
      const int pass_combined = get_pass_offset(PASS_COMBINED);
      DCHECK_NE(pass_combined, PASS_UNUSED);

      const float *in_combined = buffer_data + pass_combined;

      /* Special code which converts noisy image pass from RGB to RGBA using alpha from the
       * combined pass. */
      for (int i = 0; i < size; i++, in += pass_stride, in_combined += pass_stride, pixels += 4) {
        float scale, scale_exposure;
        scaler.scale_and_scale_exposure(i, scale, scale_exposure);

        const float3 color = make_float3(in[0], in[1], in[2]) * scale_exposure;
        const float transparency = in_combined[3] * scale;

        pixels[0] = color.x;
        pixels[1] = color.y;
        pixels[2] = color.z;

        pixels[3] = saturate(1.0f - transparency);
      }
    }
    else {
      for (int i = 0; i < size; i++, in += pass_stride, pixels += 4) {
        float scale, scale_exposure;
        scaler.scale_and_scale_exposure(i, scale, scale_exposure);

        /* Note that 3rd channel contains transparency = 1 - alpha at this point. */
        const float3 color = make_float3(in[0], in[1], in[2]) * scale_exposure;
        const float transparency = in[3] * scale;

        pixels[0] = color.x;
        pixels[1] = color.y;
        pixels[2] = color.z;

        /* Clamp since alpha might end up outside of 0..1 due to Russian roulette. */
        pixels[3] = saturate(1.0f - transparency);
      }
    }
  }

  return true;
}

#if 0
bool PassAccessor::set_pass_rect(PassType type, int components, float *pixels, int samples)
{
  if (buffer.data() == NULL) {
    return false;
  }

  int pass_offset = 0;

  for (size_t j = 0; j < params.passes.size(); j++) {
    Pass &pass = params.passes[j];

    if (pass.type != type) {
      pass_offset += pass.components;
      continue;
    }

    float *out = buffer.data() + pass_offset;
    const int pass_stride = params.passes_size;
    const int size = params.width * params.height;

    DCHECK_EQ(pass.components, components)
        << "Number of components mismatch for pass " << pass.name;

    for (int i = 0; i < size; i++, out += pass_stride, pixels += components) {
      if (pass.filter) {
        /* Scale by the number of samples, inverse of what we do in get_pass_rect.
         * A better solution would be to remove the need for set_pass_rect entirely,
         * and change baking to bake multiple objects in a tile at once. */
        for (int j = 0; j < components; j++) {
          out[j] = pixels[j] * samples;
        }
      }
      else {
        /* For non-filtered passes just straight copy, these may contain non-float data. */
        memcpy(out, pixels, sizeof(float) * components);
      }
    }

    return true;
  }

  return false;
}
#endif

int PassAccessor::get_pass_offset(PassType type) const
{
  int pass_offset = 0;

  for (const Pass &pass : passes_) {
    if (pass.type == type) {
      return pass_offset;
    }
    pass_offset += pass.components;
  }

  return PASS_UNUSED;
}

CCL_NAMESPACE_END
