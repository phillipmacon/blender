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
 * Copyright 2012, Blender Foundation.
 */

#pragma once

#include "COM_NodeOperation.h"

namespace blender::compositor {

class DisplaceSimpleOperation : public NodeOperation {
 private:
  /**
   * Cached reference to the inputProgram
   */
  SocketReader *m_inputColorProgram;
  SocketReader *m_inputVectorProgram;
  SocketReader *m_inputScaleXProgram;
  SocketReader *m_inputScaleYProgram;

  float m_width_x4;
  float m_height_x4;

 public:
  DisplaceSimpleOperation();

  /**
   * we need a full buffer for the image
   */
  bool determineDependingAreaOfInterest(rcti *input,
                                        ReadBufferOperation *readOperation,
                                        rcti *output) override;

  /**
   * The inner loop of this operation.
   */
  void executePixelSampled(float output[4], float x, float y, PixelSampler sampler) override;

  /**
   * Initialize the execution
   */
  void initExecution() override;

  /**
   * Deinitialize the execution
   */
  void deinitExecution() override;
};

}  // namespace blender::compositor
