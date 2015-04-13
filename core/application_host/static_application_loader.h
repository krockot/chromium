// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_APPLICATION_HOST_STATIC_APPLICATION_LOADER_H_
#define CORE_APPLICATION_HOST_STATIC_APPLICATION_LOADER_H_

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "core/public/application/application_delegate.h"
#include "core/public/application_host/application_loader.h"
#include "third_party/mojo/src/mojo/public/c/system/types.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/application.mojom.h"

namespace core {

// This is an implementation of ApplicationLoader which loads an application
// statically from an ApplicationFactory.
class StaticApplicationLoader : public core::ApplicationLoader {
 public:
  enum RunMode {
    IN_THREAD,
    NEW_THREAD,
  };

  StaticApplicationLoader(const core::ApplicationFactory& factory,
                          RunMode run_mode);
  ~StaticApplicationLoader() override;

 private:
  // ApplicationLoader:
  void Load(mojo::InterfaceRequest<mojo::Application> application_request,
            const SuccessCallback& success_callback,
            const FailureCallback& failure_callback) override;

  RunMode run_mode_;
  core::ApplicationFactory factory_;

  base::WeakPtrFactory<StaticApplicationLoader> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(StaticApplicationLoader);
};

}  // namespace core

#endif  // CORE_APPLICATION_HOST_STATIC_APPLICATION_LOADER_H_
