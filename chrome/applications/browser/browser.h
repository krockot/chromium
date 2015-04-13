// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_APPLICATIONS_BROWSER_BROWSER_H_
#define CHROME_APPLICATIONS_BROWSER_BROWSER_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "core/public/application/application_delegate.h"
#include "core/public/interfaces/application_broker.mojom.h"
#include "core/public/interfaces/shell_private.mojom.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"

namespace chrome_browser {

class Browser : public core::ApplicationDelegate {
 public:
  Browser();
  ~Browser() override;

 private:
  class ApplicationManager;

  // core::ApplicationDelegate:
  void InitializeApplication(mojo::Shell* shell,
                             const std::vector<std::string>& args) override;
  void AcceptConnection(const GURL& requestor_url,
                        core::ApplicationConnection* connection,
                        const GURL& resolved_url) override;
  void Quit() override;

  void BindBrokerRequest(
      mojo::InterfaceRequest<core::ApplicationBroker> request);

  scoped_ptr<ApplicationManager> manager_;
  core::ShellPrivatePtr shell_private_;

  base::WeakPtrFactory<Browser> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(Browser);
};

}  // namespace chrome_browser

#endif  // CHROME_APPLICATIONS_BROWSER_BROWSER_H_
