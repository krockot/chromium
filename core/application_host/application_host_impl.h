// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_COMMON_APPLICATION_HOST_IMPL_H_
#define CORE_COMMON_APPLICATION_HOST_IMPL_H_

#include "base/containers/hash_tables.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "core/public/application_host/application_loader.h"
#include "core/public/interfaces/application_host.mojom.h"

namespace core {

class Application;
class ApplicationRegistry;
class EntryPoint;

class ApplicationHostImpl : public ApplicationHost {
 public:
  ApplicationHostImpl();
  ~ApplicationHostImpl() override;

  // ApplicationHost:
  void LaunchApplication(
      const mojo::String& application_url,
      mojo::InterfaceRequest<mojo::Application> application,
      const LaunchApplicationCallback& callback) override;

 private:
  class ApplicationContainer;
  friend class ApplicationContainer;

  void OnApplicationLoadSuccess(
      const GURL& url,
      const LaunchApplicationCallback& response_callback,
      scoped_ptr<EntryPoint> entry_point);
  void OnApplicationLoadFailure(
      const GURL& url,
      const LaunchApplicationCallback& response_callback,
      mojo::InterfaceRequest<mojo::Application> application);

  void DestroyApplicationContainer(ApplicationContainer* container);

  scoped_ptr<ApplicationRegistry> registry_;
  base::hash_set<ApplicationContainer*> running_applications_;
  base::WeakPtrFactory<ApplicationHostImpl> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ApplicationHostImpl);
};

}  // namespace core

#endif  // CORE_COMMON_APPLICATION_HOST_IMPL_H_
