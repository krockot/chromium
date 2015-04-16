// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_SHELL_SHELL_IMPL_H_
#define CORE_SHELL_SHELL_IMPL_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "core/public/interfaces/application_host.mojom.h"
#include "core/public/shell/shell.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/service_provider.mojom.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/shell.mojom.h"

class GURL;

namespace mojo {
class ErrorHandler;
}

namespace core {

class ApplicationManager;

class ShellImpl : public Shell {
 public:
  ShellImpl();
  ~ShellImpl() override;

  void ConnectApplications(
      const GURL& to_url,
      mojo::InterfaceRequest<mojo::ServiceProvider> services,
      mojo::ServiceProviderPtr exposed_services,
      const GURL& from_url);

  mojo::Shell* browser_shell_proxy() const {
    return browser_shell_proxy_.get();
  }

  ApplicationHost* in_process_application_host() const {
    return in_process_application_host_.get();
  }

 private:
  scoped_ptr<ApplicationHost> in_process_application_host_;

  mojo::ShellPtr browser_shell_proxy_;
  scoped_ptr<mojo::Shell> browser_shell_;

  scoped_ptr<ApplicationManager> application_manager_;

  base::WeakPtrFactory<ShellImpl> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ShellImpl);
};

}  // namespace core

#endif  // CORE_SHELL_SHELL_IMPL_H_
