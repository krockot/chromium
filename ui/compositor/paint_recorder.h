// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_COMPOSITOR_PAINT_RECORDER_H_
#define UI_COMPOSITOR_PAINT_RECORDER_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "skia/ext/refptr.h"
#include "ui/compositor/compositor_export.h"

namespace cc {
class DisplayItemList;
}

namespace gfx {
class Canvas;
}

class SkCanvas;
class SkPictureRecorder;

namespace ui {
class PaintContext;

// A class to hide the complexity behind setting up a recording into a
// DisplayItem. This is meant to be short-lived within the scope of recording
// taking place, the DisplayItem should be removed from the PaintRecorder once
// recording is complete and can be cached.
class COMPOSITOR_EXPORT PaintRecorder {
 public:
  explicit PaintRecorder(const PaintContext& context);
  ~PaintRecorder();

  // Gets a gfx::Canvas for painting into.
  gfx::Canvas* canvas() { return canvas_; }

 private:
  gfx::Canvas* canvas_;
  cc::DisplayItemList* list_;
  SkPictureRecorder* recorder_;
  scoped_ptr<gfx::Canvas> owned_canvas_;

  DISALLOW_COPY_AND_ASSIGN(PaintRecorder);
};

}  // namespace ui

#endif  // UI_COMPOSITOR_PAINT_RECORDER_H_
