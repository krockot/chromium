// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_COMMON_APPLICATION_HOST_IMPL_H_
#define CORE_COMMON_APPLICATION_HOST_IMPL_H_

#include <stdint.h>

#include "base/containers/hash_tables.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "core/public/interfaces/application_host.mojom.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"

class GURL;

namespace core {

class ApplicationRegistry;
class ApplicationRunner;

class ApplicationHostImpl : public ApplicationHost {
 public:
  ApplicationHostImpl();
  ~ApplicationHostImpl() override;

  // ApplicationHost:
  void LaunchApplication(const mojo::String& application_url,
                         mojo::InterfaceRequest<mojo::Application> application,
                         const LaunchApplicationCallback& callback) override;

 private:
  class ApplicationContainer;

  void OnApplicationLoadSuccess(
      const GURL& url,
      const LaunchApplicationCallback& response_callback,
      scoped_ptr<ApplicationRunner> runner);
  void OnApplicationLoadFailure(
      const GURL& url,
      const LaunchApplicationCallback& response_callback,
      mojo::InterfaceRequest<mojo::Application> application);

  void PurgeApplicationContainer(int64_t id);

  ApplicationRegistry* registry_;

  int64_t next_container_id_;
  base::hash_map<int64_t, ApplicationContainer*> running_applications_;

  base::WeakPtrFactory<ApplicationHostImpl> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ApplicationHostImpl);
};

}  // namespace core

#endif  // CORE_COMMON_APPLICATION_HOST_IMPL_H_
