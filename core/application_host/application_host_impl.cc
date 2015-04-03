// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/application_host/application_host_impl.h"

#include "base/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/stl_util.h"
#include "base/threading/thread.h"
#include "core/application_host/application_registry_impl.h"
#include "core/public/application/application.h"
#include "core/public/application_host/core_application_host_client.h"
#include "core/public/application_host/entry_point.h"
#include "mojo/common/message_pump_mojo.h"
#include "url/gurl.h"

namespace core {

namespace {

void RunEntryPoint(scoped_ptr<EntryPoint> entry_point) {
  entry_point->Run();
}

}  // namespace

class ApplicationHostImpl::ApplicationContainer {
 public:
  ApplicationContainer(ApplicationHostImpl* host, const GURL& url);
  ~ApplicationContainer();

  void Run(scoped_ptr<EntryPoint> entry_point);

 private:
  void OnApplicationQuit();

  ApplicationHostImpl* host_;
  base::Thread application_thread_;
  base::WeakPtrFactory<ApplicationContainer> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ApplicationContainer);
};

ApplicationHostImpl::ApplicationContainer::ApplicationContainer(
    ApplicationHostImpl* host,
    const GURL& url)
    : host_(host), application_thread_(url.spec()), weak_factory_(this) {
}

ApplicationHostImpl::ApplicationContainer::~ApplicationContainer() {
}

void ApplicationHostImpl::ApplicationContainer::Run(
    scoped_ptr<EntryPoint> entry_point) {
  base::Thread::Options options;
  options.message_pump_factory =
      base::Bind(&mojo::common::MessagePumpMojo::Create);
  application_thread_.StartWithOptions(options);
  application_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&RunEntryPoint, base::Passed(&entry_point)));
}

void ApplicationHostImpl::ApplicationContainer::OnApplicationQuit() {
  host_->DestroyApplicationContainer(this);
}

ApplicationHostImpl::ApplicationHostImpl()
    : registry_(new ApplicationRegistryImpl), weak_factory_(this) {
  core::CoreApplicationHostClient::Get()->RegisterApplications(registry_.get());
}

ApplicationHostImpl::~ApplicationHostImpl() {
  STLDeleteContainerPointers(running_applications_.begin(),
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
    scoped_ptr<EntryPoint> entry_point) {
  ApplicationContainer* container = new ApplicationContainer(this, url);
  running_applications_.insert(container);
  container->Run(entry_point.Pass());
  response_callback.Run(LAUNCH_ERROR_OK);
}

void ApplicationHostImpl::OnApplicationLoadFailure(
    const GURL& url,
    const LaunchApplicationCallback& response_callback,
    mojo::InterfaceRequest<mojo::Application> application) {
  // TODO(core): Real error propagation from loader.
  // TODO(core): Fallback behavior.
  LOG(ERROR) << "Failed to load application: " << url.spec();
  response_callback.Run(LAUNCH_ERROR_NOT_FOUND);
}

void ApplicationHostImpl::DestroyApplicationContainer(
    ApplicationContainer* container) {
  auto iter = running_applications_.find(container);
  DCHECK(iter != running_applications_.end());
  running_applications_.erase(iter);
  delete *iter;
}

}  // namespace core
