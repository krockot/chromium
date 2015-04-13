// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PROCESS_PROCESS_BASE_H_
#define CORE_PROCESS_PROCESS_BASE_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "core/public/interfaces/application_host.mojom.h"
#include "core/public/interfaces/process.mojom.h"
#include "mojo/common/weak_binding_set.h"

namespace core {

// Base implementation for different core process types. This provides the
// necessary pieces to support the |core::Process| Mojo interface.
class ProcessBase : public Process {
 public:
  ProcessBase();
  ~ProcessBase() override;

  // Weakly binds the given request to this instance. This can be called
  // multiple times with different requests.
  void BindRequest(mojo::InterfaceRequest<Process> request);

 private:
  // Process:
  void GetApplicationHost(
      mojo::InterfaceRequest<ApplicationHost> request) override;
  void Kill(int32_t exit_code,
            bool wait,
            const KillCallback& callback) override;
  void WaitForExit(const WaitForExitCallback& callback) override;
  void GetTerminationStatus(
      const GetTerminationStatusCallback& callback) override;

  scoped_ptr<ApplicationHost> application_host_;

  mojo::WeakBindingSet<Process> process_bindings_;
  mojo::WeakBindingSet<ApplicationHost> application_host_bindings_;

  DISALLOW_COPY_AND_ASSIGN(ProcessBase);
};

}  // namespace core

#endif  // CORE_PROCESS_PROCESS_BASE_H_
