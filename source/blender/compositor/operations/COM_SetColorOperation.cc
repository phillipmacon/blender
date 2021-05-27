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
 * Copyright 2011, Blender Foundation.
 */

#include "COM_SetColorOperation.h"

namespace blender::compositor {

SetColorOperation::SetColorOperation()
{
  this->addOutputSocket(DataType::Color);
  flags.is_set_operation = true;
  flags.is_fullframe_operation = true;
}

void SetColorOperation::executePixelSampled(float output[4],
                                            float /*x*/,
                                            float /*y*/,
                                            PixelSampler /*sampler*/)
{
  copy_v4_v4(output, this->m_color);
}

void SetColorOperation::update_memory_buffer(MemoryBuffer *output,
                                             const rcti &output_rect,
                                             Span<MemoryBuffer *> inputs,
                                             ExecutionSystem &exec_system)
{
  BLI_assert(output->is_a_single_elem());
  float *out_elem = output->get_elem(output_rect.xmin, output_rect.ymin);
  copy_v4_v4(out_elem, m_color);
}

void SetColorOperation::determineResolution(unsigned int resolution[2],
                                            unsigned int preferredResolution[2])
{
  resolution[0] = preferredResolution[0];
  resolution[1] = preferredResolution[1];
}

}  // namespace blender::compositor
