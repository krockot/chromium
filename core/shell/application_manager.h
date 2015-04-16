// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_SHELL_APPLICATION_MANAGER_H_
#define CORE_SHELL_APPLICATION_MANAGER_H_

#include <stdint.h>

#include <list>
#include <map>

#include "base/callback_forward.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "core/public/interfaces/application_host.mojom.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/application.mojom.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/service_provider.mojom.h"

class GURL;

namespace core {

class ShellImpl;

class ApplicationManager {
 public:
  explicit ApplicationManager(ShellImpl* shell);
  ~ApplicationManager();

  void ConnectApplications(
      const GURL& to_url,
      mojo::InterfaceRequest<mojo::ServiceProvider> services,
      mojo::ServiceProviderPtr exposed_services,
      const GURL& from_url);

 private:
  class RunningApplication;

  using RunningApplicationMap = std::map<int32_t, RunningApplication*>;
  using LaunchCallback =
      base::Callback<void(base::WeakPtr<RunningApplication>)>;

  void LaunchApplication(const GURL& url, const LaunchCallback& callback);
  void ConnectToRunningApplication(
      const GURL& from_url,
      mojo::InterfaceRequest<mojo::ServiceProvider> services,
      mojo::ServiceProviderPtr exposed_services,
      base::WeakPtr<RunningApplication> application);
  void OnApplicationLaunched(const LaunchCallback& callback,
                             const GURL& url,
                             mojo::ApplicationPtr,
                             LaunchError result);
  void OnApplicationExited(int32_t app_id, int32_t exit_code);

  ShellImpl* shell_;

  int32_t next_app_id_;
  RunningApplicationMap apps_;

  base::WeakPtrFactory<ApplicationManager> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ApplicationManager);
};

}  // namespace core

#endif  // CORE_SHELL_APPLICATION_MANAGER_H_
