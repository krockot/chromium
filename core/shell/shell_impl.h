// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_SHELL_SHELL_IMPL_H_
#define CORE_SHELL_SHELL_IMPL_H_

#include "base/containers/hash_tables.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "core/public/interfaces/application_host.mojom.h"
#include "core/public/interfaces/process.mojom.h"
#include "core/public/shell/shell.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/binding.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/application.mojom.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/service_provider.mojom.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/shell.mojom.h"

namespace core {

class ShellProcess;

class ShellImpl : public Shell, public mojo::ServiceProvider {
 public:
  ShellImpl();
  ~ShellImpl() override;

 private:
  class ShellPrivateImpl;
  friend class ShellPrivateImpl;

  class ApplicationInstance;
  friend class ApplicationInstance;

  // mojo::ServiceProvider:
  void ConnectToService(const mojo::String& interface_name,
                        mojo::ScopedMessagePipeHandle handle) override;

  ApplicationInstance* Launch(const GURL& url);
  void Connect(const GURL& to_url,
               mojo::InterfaceRequest<mojo::ServiceProvider> services,
               mojo::ServiceProviderPtr exposed_services,
               const GURL& from_url);
  void DestroyApplicationInstance(const std::string& url_spec);

  scoped_ptr<ShellProcess> process_;
  ProcessPtr process_proxy_;
  ApplicationHostPtr in_process_application_host_;

  mojo::ServiceProviderPtr init_services_;
  scoped_ptr<ShellPrivateImpl> private_;
  mojo::Binding<mojo::ServiceProvider> private_services_binding_;

  base::hash_map<std::string, ApplicationInstance*> running_applications_;

  base::WeakPtrFactory<ShellImpl> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ShellImpl);
};

}  // namespace core

#endif  // CORE_SHELL_SHELL_IMPL_H_
