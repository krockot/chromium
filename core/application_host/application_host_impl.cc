// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/application_host/application_host_impl.h"

#include "base/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/stl_util.h"
#include "core/public/application_host/application_loader.h"
#include "core/public/application_host/application_registry.h"
#include "core/public/application_host/application_runner.h"
#include "core/public/application_host/core_application_host_client.h"
#include "url/gurl.h"

namespace core {

class ApplicationHostImpl::ApplicationContainer {
 public:
  ApplicationContainer(const GURL& url, scoped_ptr<ApplicationRunner> runner);
  ~ApplicationContainer();

  void Start(const ApplicationRunner::ExitCallback& exit_callback);

 private:
  scoped_ptr<ApplicationRunner> runner_;
  base::WeakPtrFactory<ApplicationContainer> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ApplicationContainer);
};

ApplicationHostImpl::ApplicationContainer::ApplicationContainer(
    const GURL& url,
    scoped_ptr<ApplicationRunner> runner)
    : runner_(runner.Pass()), weak_factory_(this) {
}

ApplicationHostImpl::ApplicationContainer::~ApplicationContainer() {
}

void ApplicationHostImpl::ApplicationContainer::Start(
    const ApplicationRunner::ExitCallback& exit_callback) {
  runner_->Start(exit_callback);
}

ApplicationHostImpl::ApplicationHostImpl()
    : registry_(
          core::CoreApplicationHostClient::Get()->CreateApplicationRegistry()),
      next_container_id_(0),
      weak_factory_(this) {

}

ApplicationHostImpl::~ApplicationHostImpl() {
  STLDeleteContainerPairSecondPointers(running_applications_.begin(),
                                       running_applications_.end());
}

void ApplicationHostImpl::LaunchApplication(
    const mojo::String& application_url,
    mojo::InterfaceRequest<mojo::Application> application,
    const LaunchApplicationCallback& callback) {
  GURL url = GURL(application_url.To<std::string>());
  ApplicationLoader* loader = registry_->GetApplicationLoader(url);
  if (!loader) {
    LOG(ERROR) << "Unable to launch unregistered application: " << url.spec();
    callback.Run(LAUNCH_ERROR_NOT_FOUND);
    return;
  }
  loader->Load(application.Pass(),
               base::Bind(&ApplicationHostImpl::OnApplicationLoadSuccess,
                          weak_factory_.GetWeakPtr(), url, callback),
               base::Bind(&ApplicationHostImpl::OnApplicationLoadFailure,
                          weak_factory_.GetWeakPtr(), url, callback));
}

void ApplicationHostImpl::OnApplicationLoadSuccess(
    const GURL& url,
    const LaunchApplicationCallback& response_callback,
    scoped_ptr<ApplicationRunner> runner) {
  int64_t id = next_container_id_++;
  ApplicationContainer* container =
      new ApplicationContainer(url, runner.Pass());
  running_applications_.insert(std::make_pair(id, container));
  response_callback.Run(LAUNCH_ERROR_OK);
  container->Start(base::Bind(&ApplicationHostImpl::PurgeApplicationContainer,
                              weak_factory_.GetWeakPtr(), id));
}

void ApplicationHostImpl::OnApplicationLoadFailure(
    const GURL& url,
    const LaunchApplicationCallback& response_callback,
    mojo::InterfaceRequest<mojo::Application> application) {
  response_callback.Run(LAUNCH_ERROR_NOT_FOUND);
}

void ApplicationHostImpl::PurgeApplicationContainer(int64_t id) {
  auto iter = running_applications_.find(id);
  DCHECK(iter != running_applications_.end());
  delete iter->second;
  running_applications_.erase(iter);
}

}  // namespace core
