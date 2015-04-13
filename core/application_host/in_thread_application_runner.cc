// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/application_host/in_thread_application_runner.h"

#include "base/logging.h"

namespace core {

InThreadApplicationRunner::InThreadApplicationRunner(
    scoped_ptr<Application> application)
    : application_(application.Pass()) {
}

InThreadApplicationRunner::~InThreadApplicationRunner() {
}

void InThreadApplicationRunner::Start(const ExitCallback& exit_callback) {
  application_->AddObserver(this);
  exit_callback_ = exit_callback;
}

void InThreadApplicationRunner::OnQuit() {
  DCHECK(!exit_callback_.is_null());
  exit_callback_.Run();
}

}  // namespace core
