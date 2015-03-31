// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_APPLICATIONS_INIT_INIT_H_
#define CHROME_APPLICATIONS_INIT_INIT_H_

#include "core/public/application/application_delegate.h"

namespace chrome_init {

class Init : public core::ApplicationDelegate {
 public:
  Init();
  ~Init() override;

 private:
  // core::ApplicationDelegate:
  void InitializeApplication(mojo::Shell* shell,
                             const std::vector<std::string>& args) override;
  void AcceptConnection(const GURL& requestor_url,
                        scoped_ptr<core::ApplicationConnection> connection,
                        const GURL& resolved_url) override;
  void Quit() override;
};

}  // namespace chrome_init

#endif  // CHROME_APPLICATIONS_INIT_INIT_H_
