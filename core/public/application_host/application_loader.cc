// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/public/application_host/application_loader.h"

namespace core {

ApplicationLoader::Result::Result(scoped_ptr<EntryPoint> entry_point)
    : entry_point_(entry_point.Pass()) {
}

ApplicationLoader::Result::Result(
    mojo::InterfaceRequest<mojo::Application> application_request)
    : application_request_(application_request.Pass()) {
}

ApplicationLoader::Result::~Result() {
}

bool ApplicationLoader::Result::Failed() const {
  return !entry_point_;
}

scoped_ptr<EntryPoint> ApplicationLoader::Result::PassEntryPoint() {
  return entry_point_.Pass();
}

mojo::InterfaceRequest<mojo::Application>
ApplicationLoader::Result::PassRequest() {
  return application_request_.Pass();
}

scoped_ptr<ApplicationLoader::Result> ApplicationLoader::CreateSuccessResult(
    mojo::InterfaceRequest<mojo::Application> application_request,
    const EntryPoint::MainFunction& main_function) {
  return make_scoped_ptr(new Result(make_scoped_ptr(
      new EntryPoint(application_request.Pass(), main_function))));
}

scoped_ptr<ApplicationLoader::Result> ApplicationLoader::CreateFailureResult(
    mojo::InterfaceRequest<mojo::Application> application_request) {
  return make_scoped_ptr(new Result(application_request.Pass()));
}

}  // namespace core
