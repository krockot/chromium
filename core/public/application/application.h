// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_COMMON_APPLICATION_H_
#define CORE_PUBLIC_COMMON_APPLICATION_H_

#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/application.mojom.h"

namespace core {

class ApplicationDelegate;

class Application {
 public:
  class Observer {
   public:
    virtual ~Observer() {}

    virtual void OnQuit() {}
  };

  Application() {}
  virtual ~Application() {}

  static scoped_ptr<Application> Create(
      scoped_ptr<ApplicationDelegate> delegate,
      mojo::InterfaceRequest<mojo::Application> request);

  virtual void Init(const std::vector<std::string>& args) = 0;
  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(Application);
};

}  // namespace core

#endif  // CORE_PUBLIC_COMMON_APPLICATION_H_
