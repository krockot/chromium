// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/public/application_host/entry_point.h"

#include "base/logging.h"

namespace core {

EntryPoint::EntryPoint(
    mojo::InterfaceRequest<mojo::Application> application_request,
    const MainFunction& main_function)
    : has_run_(false),
      application_request_(application_request.Pass()),
      main_function_(main_function) {
}

EntryPoint::~EntryPoint() {
}

void EntryPoint::Run() {
  DCHECK(!has_run_);
  DCHECK(application_request_.is_pending());
  has_run_ = true;
  // Let it goooo, let it GOOOO! We trust the application to take ownership.
  MojoHandle handle = application_request_.PassMessagePipe().release().value();
  main_function_.Run(handle);
}

}  // namespace core
