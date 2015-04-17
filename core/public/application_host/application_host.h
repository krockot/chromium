// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_APPLICATION_HOST_APPLICATION_HOST_H_
#define CORE_PUBLIC_APPLICATION_HOST_APPLICATION_HOST_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "core/public/interfaces/application_host.mojom.h"

namespace core {

class ApplicationRegistry;

class ApplicationHost : public interfaces::ApplicationHost {
 public:
  ApplicationHost();
  ~ApplicationHost() override;

  static scoped_ptr<ApplicationHost> Create(
      scoped_ptr<ApplicationRegistry> registry);

 private:
  DISALLOW_COPY_AND_ASSIGN(ApplicationHost);
};

}  // namespace core

#endif  // CORE_PUBLIC_APPLICATION_HOST_APPLICATION_HOST_H_
