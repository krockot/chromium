// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_SHELL_SHELL_IMPL_H_
#define CORE_SHELL_SHELL_IMPL_H_

#include "base/containers/hash_tables.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "core/public/shell/shell.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/shell.mojom.h"

namespace core {

class ApplicationHostImpl;

class ShellImpl : public Shell {
 public:
  ShellImpl();
  ~ShellImpl() override;

  // Shell:
  void Launch(const GURL& url) override;

 private:
  class ApplicationInstance;
  friend class ApplicationInstance;

  void Connect(const GURL& to_url,
               mojo::InterfaceRequest<mojo::ServiceProvider> services,
               mojo::ServiceProviderPtr exposed_services,
               const GURL& from_url);
  void DestroyApplicationInstance(const std::string& url_spec);

  scoped_ptr<ApplicationHostImpl> in_process_application_host_;

  base::hash_map<std::string, ApplicationInstance*> running_applications_;

  DISALLOW_COPY_AND_ASSIGN(ShellImpl);
};

}  // namespace core

#endif  // CORE_SHELL_SHELL_IMPL_H_
