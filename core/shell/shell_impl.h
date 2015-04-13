// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_SHELL_SHELL_IMPL_H_
#define CORE_SHELL_SHELL_IMPL_H_

#include <map>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "core/public/interfaces/application_broker.mojom.h"
#include "core/public/interfaces/application_host.mojom.h"
#include "core/public/interfaces/process.mojom.h"
#include "core/public/interfaces/shell_private.mojom.h"
#include "core/public/shell/shell.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/binding.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/application.mojom.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/service_provider.mojom.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/shell.mojom.h"

class GURL;

namespace mojo {
class ErrorHandler;
}

namespace core {

class ApplicationConnection;
class ShellProcess;

class ShellImpl : public Shell {
 public:
  ShellImpl();
  ~ShellImpl() override;

  mojo::Shell* browser_shell_proxy() const {
    return browser_shell_proxy_.get();
  }

  void GetShellProcess(mojo::InterfaceRequest<Process> request);

  void ConnectApplications(
      const GURL& to_url,
      mojo::InterfaceRequest<mojo::ServiceProvider> services,
      mojo::ServiceProviderPtr exposed_services,
      const GURL& from_url);

 private:
  void CreatePrivateService(mojo::InterfaceRequest<core::ShellPrivate> request);
  void OnStartupApplicationLaunched(LaunchError result);
  void OnFatalError();

  scoped_ptr<mojo::ErrorHandler> fatal_error_handler_;

  scoped_ptr<ShellProcess> process_;
  ProcessPtr process_proxy_;
  ApplicationHostPtr in_process_application_host_;

  mojo::ShellPtr browser_shell_proxy_;
  scoped_ptr<mojo::Shell> browser_shell_;

  mojo::ApplicationPtr startup_application_proxy_;
  scoped_ptr<ApplicationConnection> startup_application_connection_;
  ApplicationBrokerPtr application_broker_;

  scoped_ptr<ShellPrivate> private_service_;

  base::WeakPtrFactory<ShellImpl> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ShellImpl);
};

}  // namespace core

#endif  // CORE_SHELL_SHELL_IMPL_H_
