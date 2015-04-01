// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_COMMON_APPLICATION_HOST_H_
#define CORE_PUBLIC_COMMON_APPLICATION_HOST_H_

#include <string>

#include "third_party/mojo/src/mojo/public/interfaces/application/application.mojom.h"

class GURL;

namespace core {

class ApplicationHost {
 public:
  virtual ~ApplicationHost() {}

  virtual void LaunchApplication(
      const GURL& url,
      mojo::InterfaceRequest<mojo::Application> request) = 0;
};

}  // namespace core

#endif  // CORE_PUBLIC_COMMON_APPLICATION_HOST_H_
