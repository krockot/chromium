// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * TestFixture for kiosk certification manager dialog WebUI testing.
 * @extends {testing.Test}
 * @constructor
 */
function CertificateManagerDialogWebUITest() {}

CertificateManagerDialogWebUITest.prototype = {
  __proto__: testing.Test.prototype,

  /**
   * Browse to the certification manager dialog page.
   */
  browsePreload: 'chrome://certificate-manager/',
};

// Sanity test of the WebUI could be opened with no errors.
TEST_F('CertificateManagerDialogWebUITest', 'Basic', function() {
  assertEquals(this.browsePreload, document.location.href);
});
