// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/process/process_base.h"

#include "core/application_host/application_host_impl.h"

namespace core {

ProcessBase::ProcessBase() : application_host_(new ApplicationHostImpl) {
}

ProcessBase::~ProcessBase() {
}

void ProcessBase::BindRequest(mojo::InterfaceRequest<Process> request) {
  process_bindings_.AddBinding(this, request.Pass());
}

void ProcessBase::GetApplicationHost(
    mojo::InterfaceRequest<ApplicationHost> request) {
  application_host_bindings_.AddBinding(application_host_.get(),
                                        request.Pass());
}

void ProcessBase::Kill(int32_t exit_code,
                       bool wait,
                       const KillCallback& callback) {
  callback.Run();
}

void ProcessBase::WaitForExit(const WaitForExitCallback& callback) {
  callback.Run(0);
}

void ProcessBase::GetTerminationStatus(
    const GetTerminationStatusCallback& callback) {
  callback.Run(TERMINATION_STATUS_STILL_RUNNING);
}

}  // namespace core
