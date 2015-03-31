// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_APPLICATION_HOST_APPLICATION_LOADER_H_
#define CORE_PUBLIC_APPLICATION_HOST_APPLICATION_LOADER_H_

#include "base/callback_forward.h"
#include "base/memory/scoped_ptr.h"
#include "base/task_runner.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/application.mojom.h"

namespace core {

class EntryPoint;

class ApplicationLoader {
 public:
  // A Callback which receives the entry point for the loaded application, or
  // null if the application failed to load.
  using LoadCallback = base::Callback<void(scoped_ptr<EntryPoint>)>;

  virtual ~ApplicationLoader() {}

  // Constructs an ApplicationLoader which loads from a dynamic library.
  static scoped_ptr<ApplicationLoader> CreateForLibrary(
      const std::string& name);

  virtual void Load(
      mojo::InterfaceRequest<mojo::Application> application_request,
      const LoadCallback& callback) = 0;
};

}  // namespace core

#endif  // CORE_PUBLIC_APPLICATION_HOST_APPLICATION_LOADER_H_
