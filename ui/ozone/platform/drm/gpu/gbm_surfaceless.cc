// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/drm/gpu/gbm_surfaceless.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "ui/ozone/platform/drm/gpu/drm_device.h"
#include "ui/ozone/platform/drm/gpu/drm_device_manager.h"
#include "ui/ozone/platform/drm/gpu/drm_vsync_provider.h"
#include "ui/ozone/platform/drm/gpu/drm_window.h"
#include "ui/ozone/platform/drm/gpu/gbm_buffer.h"
#include "ui/ozone/platform/drm/gpu/hardware_display_controller.h"

namespace ui {

GbmSurfaceless::GbmSurfaceless(DrmWindow* window_delegate,
                               DrmDeviceManager* drm_device_manager)
    : window_delegate_(window_delegate),
      drm_device_manager_(drm_device_manager) {
}

GbmSurfaceless::~GbmSurfaceless() {
}

intptr_t GbmSurfaceless::GetNativeWindow() {
  NOTREACHED();
  return 0;
}

bool GbmSurfaceless::ResizeNativeWindow(const gfx::Size& viewport_size) {
  return true;
}

bool GbmSurfaceless::OnSwapBuffers() {
  return window_delegate_->SchedulePageFlip(true /* is_sync */,
                                            base::Bind(&base::DoNothing));
}

bool GbmSurfaceless::OnSwapBuffersAsync(
    const SwapCompletionCallback& callback) {
  return window_delegate_->SchedulePageFlip(false /* is_sync */, callback);
}

scoped_ptr<gfx::VSyncProvider> GbmSurfaceless::CreateVSyncProvider() {
  return make_scoped_ptr(new DrmVSyncProvider(window_delegate_));
}

bool GbmSurfaceless::IsUniversalDisplayLinkDevice() {
  if (!drm_device_manager_)
    return false;
  scoped_refptr<DrmDevice> drm_primary =
      drm_device_manager_->GetDrmDevice(gfx::kNullAcceleratedWidget);
  DCHECK(drm_primary);

  HardwareDisplayController* controller = window_delegate_->GetController();
  if (!controller)
    return false;
  scoped_refptr<DrmDevice> drm = controller->GetAllocationDrmDevice();
  DCHECK(drm);

  return drm_primary != drm;
}

}  // namespace ui
