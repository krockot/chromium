// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_COMMON_SERVICE_PROVIDER_H_
#define CORE_PUBLIC_COMMON_SERVICE_PROVIDER_H_

#include <string>

#include "base/callback_forward.h"
#include "base/macros.h"
#include "third_party/mojo/src/mojo/public/cpp/system/message_pipe.h"

namespace core {

class ServiceProvider {
 public:
  using RequestHandler = base::Callback<void(mojo::ScopedMessagePipeHandle)>;

  ServiceProvider() {}
  virtual ~ServiceProvider() {}

  virtual void RegisterService(const std::string&,
                               const RequestHandler& handler) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(ServiceProvider);
};

}  // namespace core

#endif  // CORE_PUBLIC_COMMON_SERVICE_PROVIDER_H_
