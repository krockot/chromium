// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SHELL_ANDROID_CONTENT_HANDLER_H_
#define MOJO_SHELL_ANDROID_CONTENT_HANDLER_H_

#include <jni.h>

#include "mojo/application/content_handler_factory.h"
#include "mojo/public/cpp/application/application_delegate.h"
#include "mojo/public/cpp/application/interface_factory_impl.h"
#include "third_party/mojo_services/src/content_handler/public/interfaces/content_handler.mojom.h"

namespace base {
class FilePath;
}

namespace mojo {
namespace shell {

class AndroidHandler : public ApplicationDelegate,
                       public ContentHandlerFactory::Delegate {
 public:
  AndroidHandler();
  ~AndroidHandler();

 private:
  // ApplicationDelegate:
  void Initialize(ApplicationImpl* app) override;
  bool ConfigureIncomingConnection(ApplicationConnection* connection) override;

  // ContentHandlerFactory::Delegate:
  void RunApplication(InterfaceRequest<Application> application_request,
                      URLResponsePtr response) override;

  ContentHandlerFactory content_handler_factory_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(AndroidHandler);
};

bool RegisterAndroidHandlerJni(JNIEnv* env);

}  // namespace shell
}  // namespace mojo

#endif  // MOJO_SHELL_ANDROID_CONTENT_HANDLER_H_
