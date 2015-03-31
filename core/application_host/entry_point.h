// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_APPLICATION_HOST_ENTRY_POINT_H_
#define CORE_APPLICATION_HOST_ENTRY_POINT_H_

#include "base/callback.h"
#include "base/macros.h"
#include "third_party/mojo/src/mojo/public/c/system/types.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/application.mojom.h"

namespace core {

// This wraps the entry point for a loaded application. It owns the pending
// Application request handle until |Run| is called, at which point the handle's
// ownership is transferred to the application itself.
class EntryPoint {
 public:
  using MainFunction = base::Callback<void(MojoHandle)>;

  EntryPoint(mojo::InterfaceRequest<mojo::Application> application_request,
             const MainFunction& main_function);
  ~EntryPoint();

  void Run();

 private:
  bool has_run_;
  mojo::InterfaceRequest<mojo::Application> application_request_;
  MainFunction main_function_;

  DISALLOW_COPY_AND_ASSIGN(EntryPoint);
};

}  // namespace core

#endif  // CORE_APPLICATION_HOST_ENTRY_POINT_H_
