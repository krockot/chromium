// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_COMMON_APPLICATION_CONNECTION_H_
#define CORE_PUBLIC_COMMON_APPLICATION_CONNECTION_H_

#include <string>

#include "base/bind.h"
#include "base/callback_forward.h"
#include "base/memory/scoped_ptr.h"
#include "core/public/common/service_provider.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_ptr.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"
#include "third_party/mojo/src/mojo/public/cpp/system/message_pipe.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/shell.mojom.h"

class GURL;

namespace core {

class ApplicationConnection {
 public:
  template <typename Interface>
  using ServiceFactory =
      base::Callback<void(mojo::InterfaceRequest<Interface>)>;

  static scoped_ptr<ApplicationConnection> Create(mojo::Shell* shell,
                                                  const GURL& url);

  ApplicationConnection() {}
  virtual ~ApplicationConnection() {}

  virtual void AddService(const std::string& name,
                          const ServiceProvider::RequestHandler& factory) = 0;
  virtual void ExposeServices() = 0;
  virtual void ConnectToService(const std::string& name,
                                mojo::ScopedMessagePipeHandle handle) = 0;

  template <typename Interface>
  void AddService(const ServiceFactory<Interface>& factory) {
    AddService(Interface::Name_,
               base::Bind(&BindHandleToFactoryResult<Interface>, factory));
  }

  template <typename Interface>
  void ConnectToService(mojo::InterfacePtr<Interface>* proxy) {
    ConnectToService(Interface::Name_,
                     mojo::GetProxy(proxy).PassMessagePipe());
  }

 private:
  template <typename Interface>
  static void BindHandleToFactoryResult(
      const ServiceFactory<Interface>& factory,
      mojo::ScopedMessagePipeHandle handle) {
    factory.Run(mojo::MakeRequest<Interface>(handle.Pass()));
  }

  DISALLOW_COPY_AND_ASSIGN(ApplicationConnection);
};

}  // namespace core

#endif  // CORE_PUBLIC_COMMON_APPLICATION_CONNECTION_H_
