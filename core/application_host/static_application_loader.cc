// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/application_host/static_application_loader.h"

#include "base/run_loop.h"
#include "core/application_host/in_thread_application_runner.h"
#include "core/public/application/application.h"

namespace core {

StaticApplicationLoader::StaticApplicationLoader(
    const core::ApplicationFactory& factory,
    RunMode run_mode)
    : run_mode_(run_mode), factory_(factory), weak_factory_(this) {
}

StaticApplicationLoader::~StaticApplicationLoader() {
}

void StaticApplicationLoader::Load(
    mojo::InterfaceRequest<mojo::Application> application_request,
    const SuccessCallback& success_callback,
    const FailureCallback& failure_callback) {
  DCHECK(run_mode_ == IN_THREAD)
      << "Only in-thread applications are supported at the moment.";
  scoped_ptr<ApplicationRunner> runner(new InThreadApplicationRunner(
      Application::Create(factory_.Run(), application_request.Pass())));
  success_callback.Run(runner.Pass());
}

// static
scoped_ptr<ApplicationLoader> ApplicationLoader::CreateForFactory(
    const ApplicationFactory& factory) {
  return scoped_ptr<ApplicationLoader>(
      new StaticApplicationLoader(factory, StaticApplicationLoader::IN_THREAD));
}

}  // namespace core
