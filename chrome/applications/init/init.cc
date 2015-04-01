// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/applications/init/init.h"

#include "core/public/application/macros.h"

namespace chrome_init {

Init::Init() {
}

Init::~Init() {
}

void Init::InitializeApplication(mojo::Shell* shell,
                                 const std::vector<std::string>& args) {
}

void Init::AcceptConnection(const GURL& requestor_url,
                            scoped_ptr<core::ApplicationConnection> connection,
                            const GURL& resolved_url) {
}

void Init::Quit() {
}

}  // namespace chrome_init

APPLICATION_MAIN(chrome_init::Init)
