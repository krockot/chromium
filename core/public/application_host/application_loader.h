// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_APPLICATION_HOST_APPLICATION_LOADER_H_
#define CORE_PUBLIC_APPLICATION_HOST_APPLICATION_LOADER_H_

#include "base/callback_forward.h"
#include "base/memory/scoped_ptr.h"
#include "base/task_runner.h"
#include "core/public/application/application_delegate.h"
#include "core/public/application_host/entry_point.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/application.mojom.h"

namespace core {

class ApplicationLoader {
 public:
  using SuccessCallback =
      base::Callback<void(scoped_ptr<EntryPoint> entry_point)>;
  using FailureCallback =
      base::Callback<void(
          mojo::InterfaceRequest<mojo::Application> application)>;

  virtual ~ApplicationLoader() {}

  // Constructs an ApplicationLoader which loads from a dynamic library.
  static scoped_ptr<ApplicationLoader> CreateForLibrary(
      const std::string& name);

  // Constructs an ApplicationLoader which loads an app statically using an
  // ApplicationDelegate factory.
  static scoped_ptr<ApplicationLoader> CreateForFactory(
      const ApplicationFactory& factory);

  virtual void Load(
      mojo::InterfaceRequest<mojo::Application> application_request,
      const SuccessCallback& success_callback,
      const FailureCallback& failure_callback) = 0;
};

}  // namespace core

#endif  // CORE_PUBLIC_APPLICATION_HOST_APPLICATION_LOADER_H_
