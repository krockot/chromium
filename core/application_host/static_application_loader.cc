// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/application_host/static_application_loader.h"

#include "base/run_loop.h"
#include "core/public/application/application.h"

namespace core {

StaticApplicationLoader::StaticApplicationLoader(
    const core::ApplicationFactory& factory)
    : factory_(factory), weak_factory_(this) {
}

StaticApplicationLoader::~StaticApplicationLoader() {
}

void StaticApplicationLoader::Load(
    mojo::InterfaceRequest<mojo::Application> application_request,
    const SuccessCallback& success_callback,
    const FailureCallback& failure_callback) {
  success_callback.Run(make_scoped_ptr(new EntryPoint(
      application_request.Pass(),
      base::Bind(&StaticApplicationLoader::Start,
                 weak_factory_.GetWeakPtr()))));
}

void StaticApplicationLoader::Start(MojoHandle application_request_handle) {
  mojo::InterfaceRequest<mojo::Application> application_request =
      mojo::MakeRequest<mojo::Application>(mojo::MakeScopedHandle(
          mojo::MessagePipeHandle(application_request_handle)));
  scoped_ptr<core::Application> application =
      core::Application::Create(factory_.Run(), application_request.Pass());
  base::RunLoop run_loop;
  run_loop.Run();
}

// static
scoped_ptr<ApplicationLoader> ApplicationLoader::CreateForFactory(
    const ApplicationFactory& factory) {
  return scoped_ptr<ApplicationLoader>(new StaticApplicationLoader(
      factory));
}

}  // namespace core
