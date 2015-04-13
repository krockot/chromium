// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_APPLICATION_HOST_APPLICATION_LOADER_H_
#define CORE_PUBLIC_APPLICATION_HOST_APPLICATION_LOADER_H_

#include "base/callback_forward.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "core/public/application/application_delegate.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/application.mojom.h"

namespace core {

class ApplicationRunner;

// An ApplicationLoader is used to produce a new ApplicationRunner for each new
// instance of a specific Application to be run by an ApplicationHost. It is the
// Core embedder's responsibility to populate each ApplicationHost's registry
// with loaders for each supported application.
class ApplicationLoader {
 public:
  // Called by the implementation of |Load()| if loading was successful. The
  // provided ApplicationRunner is owned by the receiver and can be used to
  // start an instance of the Application.
  using SuccessCallback =
      base::Callback<void(scoped_ptr<ApplicationRunner> runner)>;

  // Called by the implementation of |Load()| if loading was unsuccessful. The
  // original mojo::Application InterfaceRequest is passed back to the receiver
  // so that it may be bound to an alternative application instance.
  using FailureCallback = base::Callback<void(
      mojo::InterfaceRequest<mojo::Application> application)>;

  ApplicationLoader() {}
  virtual ~ApplicationLoader() {}

  // Constructs an ApplicationLoader which loads an app statically using an
  // ApplicationDelegate factory.
  static scoped_ptr<ApplicationLoader> CreateForFactory(
      const ApplicationFactory& factory);

  // Attempts to load a new instance of this loader's Application.
  virtual void Load(
      mojo::InterfaceRequest<mojo::Application> application_request,
      const SuccessCallback& success_callback,
      const FailureCallback& failure_callback) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(ApplicationLoader);
};

}  // namespace core

#endif  // CORE_PUBLIC_APPLICATION_HOST_APPLICATION_LOADER_H_
