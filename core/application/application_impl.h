// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_APPLICATION_APPLICATION_IMPL_H_
#define CORE_APPLICATION_APPLICATION_IMPL_H_

#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "core/public/application/application.h"
#include "core/public/application/application_delegate.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/binding.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/error_handler.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/application.mojom.h"

namespace core {

class ApplicationImpl : public Application,
                        public mojo::Application,
                        public mojo::ErrorHandler {
 public:
  ApplicationImpl(scoped_ptr<ApplicationDelegate> delegate,
                  mojo::InterfaceRequest<mojo::Application> request);
  ~ApplicationImpl() override;

  // Application:
  void Init(const std::vector<std::string>& args) override;
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;

 private:
  // mojo::Application:
  void Initialize(mojo::ShellPtr shell,
                  mojo::Array<mojo::String> args,
                  const mojo::String& url) override;
  void AcceptConnection(const mojo::String& requestor_url,
                        mojo::InterfaceRequest<mojo::ServiceProvider> services,
                        mojo::ServiceProviderPtr exposed_services,
                        const mojo::String& resolved_url) override;
  void RequestQuit() override;

  // mojo::ErrorHandler
  void OnConnectionError() override;

  void Terminate();

  ObserverList<Observer> observers_;
  scoped_ptr<ApplicationDelegate> delegate_;
  mojo::Binding<mojo::Application> binding_;
  mojo::ShellPtr shell_proxy_;

  DISALLOW_COPY_AND_ASSIGN(ApplicationImpl);
};

}  // namespace core

#endif  // CORE_APPLICATION_APPLICATION_IMPL_H_
