// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(dbeam): test for loading upacked extensions?

GEN('#include "chrome/browser/ui/webui/extensions/' +
    'extension_settings_browsertest.h"');

// chrome/test/data/extensions/good.crx's extension ID. good.crx is loaded by
// ExtensionSettingsUIBrowserTest::InstallGoodExtension() in some of the tests.
var GOOD_CRX_ID = 'ldnnhddmnhbkjipkidpdiheffobcpfmf';

/**
 * Test C++ fixture for settings WebUI testing.
 * @constructor
 * @extends {testing.Test}
 */
function ExtensionSettingsUIBrowserTest() {}

/**
 * TestFixture for extension settings WebUI testing.
 * @extends {testing.Test}
 * @constructor
 */
function ExtensionSettingsWebUITest() {}

ExtensionSettingsWebUITest.prototype = {
  __proto__: testing.Test.prototype,

  /** @override */
  runAccessibilityChecks: true,

  /** @override */
  accessibilityIssuesAreErrors: true,

  /**
   * A URL to load before starting each test.
   * @type {string}
   * @const
   */
  browsePreload: 'chrome://extensions-frame/',

  /** @override */
  typedefCppFixture: 'ExtensionSettingsUIBrowserTest',
};

TEST_F('ExtensionSettingsWebUITest', 'testChromeSendHandled', function() {
  assertEquals(this.browsePreload, document.location.href);

  // This dialog should be hidden at first.
  assertFalse($('pack-extension-overlay').classList.contains('showing'));

  // Show the dialog, which triggers a chrome.send() for metrics purposes.
  cr.dispatchSimpleEvent($('pack-extension'), 'click');
  assertTrue($('pack-extension-overlay').classList.contains('showing'));
});

function BasicExtensionSettingsWebUITest() {}

BasicExtensionSettingsWebUITest.prototype = {
  __proto__: ExtensionSettingsWebUITest.prototype,

  /** @override */
  isAsync: true,

  /** @override */
  testGenPreamble: function() {
    // Install multiple types of extensions to ensure we handle each type.
    // TODO(devlin): There are more types to add here.
    GEN('  InstallGoodExtension();');
    GEN('  InstallErrorsExtension();');
    GEN('  InstallSharedModule();');
    GEN('  InstallPackagedApp();');

    GEN('  SetAutoConfirmUninstall();');
  },

  /** @protected {Array<!Function>} */
  steps: [],

  /** @protected */
  nextStep: function() {
    assertTrue(this.steps.length > 0);
    this.steps.shift().call(this);
  },

  /** @protected */
  waitForPageLoad: function() {
    var extensionList = getRequiredElement('extension-settings-list');
    extensionList.extensionsUpdated_.then(this.nextStep.bind(this));
  },

  /** @protected */
  verifyDisabledWorks: function() {
    chrome.management.setEnabled(GOOD_CRX_ID, false, function() {
      var node = getRequiredElement(GOOD_CRX_ID);
      assertTrue(node.classList.contains('inactive-extension'));
      this.nextStep();
    }.bind(this));
  },

  /** @protected */
  verifyEnabledWorks: function() {
    chrome.management.setEnabled(GOOD_CRX_ID, true, function() {
      var node = getRequiredElement(GOOD_CRX_ID);
      assertFalse(node.classList.contains('inactive-extension'));
      this.nextStep();
    }.bind(this));
  },

  /** @protected */
  verifyUninstallWorks: function() {
    var next = this.nextStep.bind(this);
    chrome.test.runWithUserGesture(function() {
      chrome.management.uninstall(GOOD_CRX_ID, function() {
        assertEquals(null, $(GOOD_CRX_ID));
        next();
      });
    });
  },

  /** @protected */
  verifyDeveloperModeWorks: function() {
    var extensionSettings = getRequiredElement('extension-settings');
    assertFalse(extensionSettings.classList.contains('dev-mode'));
    $('toggle-dev-on').click();
    assertTrue(extensionSettings.classList.contains('dev-mode'));
    chrome.developerPrivate.getProfileConfiguration(function(profileInfo) {
      assertTrue(profileInfo.inDeveloperMode);
      this.nextStep();
    }.bind(this));
  },
};

TEST_F('BasicExtensionSettingsWebUITest', 'testDisable', function() {
  this.steps = [this.waitForPageLoad,
                this.verifyDisabledWorks,
                testDone];
  this.nextStep();
});

TEST_F('BasicExtensionSettingsWebUITest', 'testEnable', function() {
  this.steps = [this.waitForPageLoad,
                this.verifyDisabledWorks,
                this.verifyEnabledWorks,
                testDone];
  this.nextStep();
});

TEST_F('BasicExtensionSettingsWebUITest', 'testUninstall', function() {
  this.steps = [this.waitForPageLoad,
                this.verifyUninstallWorks,
                testDone];
  this.nextStep();
});

TEST_F('BasicExtensionSettingsWebUITest', 'testDeveloperMode', function() {
  var next = this.nextStep.bind(this);
  var checkDevModeIsOff = function() {
    chrome.developerPrivate.getProfileConfiguration(function(profileInfo) {
      assertFalse(profileInfo.inDeveloperMode);
      next();
    });
  };
  this.steps = [checkDevModeIsOff,
                this.waitForPageLoad,
                this.verifyDeveloperModeWorks,
                testDone];
  this.nextStep();
});

function AsyncExtensionSettingsWebUITest() {}

AsyncExtensionSettingsWebUITest.prototype = {
  __proto__: ExtensionSettingsWebUITest.prototype,

  /** @override */
  isAsync: true,

  /** @override */
  testGenPreamble: function() {
    GEN('  InstallGoodExtension();');
    GEN('  InstallErrorsExtension();');
  },

  enableDeveloperMode: function(callback) {
    var devControls = $('dev-controls');

    // Make sure developer controls are hidden before checkbox is clicked.
    assertEquals(0, devControls.offsetHeight);
    $('toggle-dev-on').click();

    document.addEventListener('webkitTransitionEnd', function f(e) {
      if (e.target == devControls) {
        // Make sure developer controls are not hidden after checkbox is
        // clicked.
        assertGT(devControls.offsetHeight, 0);

        document.removeEventListener(f, 'webkitTransitionEnd');
        callback();
      }
    });
    ensureTransitionEndEvent(devControls, 4000);
  },
};

TEST_F('AsyncExtensionSettingsWebUITest', 'testDeveloperModeA11y', function() {
  this.enableDeveloperMode(testDone);
});

// Often times out on all platforms: http://crbug.com/467528
TEST_F('AsyncExtensionSettingsWebUITest',
       'DISABLED_testErrorListButtonVisibility',
    function() {
  this.enableDeveloperMode(function() {
    // 2 extensions are loaded:
    //   The 'good' extension will have 0 errors wich means no error list
    //     buttons.
    //   The 'bad' extension will have >3 manifest errors and <3 runtime errors.
    //     This means 2 buttons: 1 visible and 1 hidden.
    var visibleButtons = document.querySelectorAll(
        '.extension-error-list-show-more > a:not([hidden])');
    assertEquals(1, visibleButtons.length);
    // Visible buttons must be part of the focusRow.
    assertTrue(visibleButtons[0].hasAttribute('column-type'));

    var hiddenButtons = document.querySelectorAll(
        '.extension-error-list-show-more > a[hidden]');
    assertEquals(1, hiddenButtons.length);
    // Hidden buttons must NOT be part of the focusRow.
    assertFalse(hiddenButtons[0].hasAttribute('column-type'));

    testDone();
  });
});

/**
 * TestFixture for extension settings WebUI testing (commands config edition).
 * @extends {testing.Test}
 * @constructor
 */
function ExtensionSettingsCommandsConfigWebUITest() {}

ExtensionSettingsCommandsConfigWebUITest.prototype = {
  __proto__: testing.Test.prototype,

  /** @override */
  runAccessibilityChecks: true,

  /** @override */
  accessibilityIssuesAreErrors: true,

  /**
   * A URL to load before starting each test.
   * @type {string}
   * @const
   */
  browsePreload: 'chrome://extensions-frame/configureCommands',
};

TEST_F('ExtensionSettingsCommandsConfigWebUITest', 'testChromeSendHandler',
    function() {
  // Just navigating to the page should trigger the chrome.send().
  assertEquals(this.browsePreload, document.location.href);
  assertTrue($('extension-commands-overlay').classList.contains('showing'));
});

/**
 * @constructor
 * @extends {ExtensionSettingsWebUITest}
 */
function InstalledExtensionSettingsWebUITest() {}

InstalledExtensionSettingsWebUITest.prototype = {
  __proto__: ExtensionSettingsWebUITest.prototype,

  /** @override */
  typedefCppFixture: 'ExtensionSettingsUIBrowserTest',

  /** @override */
  testGenPreamble: function() {
    GEN('  InstallGoodExtension();');
  },
};

/** @this {InstalledExtensionSettingsWebUITest} */
function runAudit() {
  assertEquals(this.browsePreload, document.location.href);
  this.runAccessibilityAudit();
}

TEST_F('InstalledExtensionSettingsWebUITest', 'baseAccessibilityOk', runAudit);

/**
 * @constructor
 * @extends {InstalledExtensionSettingsWebUITest}
 */
function AsyncInstalledExtensionSettingsWebUITest() {}

AsyncInstalledExtensionSettingsWebUITest.prototype = {
  __proto__: InstalledExtensionSettingsWebUITest.prototype,

  /** @override */
  isAsync: true,
};

TEST_F('AsyncInstalledExtensionSettingsWebUITest', 'showOptions', function() {
  var optionsOverlay = extensions.ExtensionOptionsOverlay.getInstance();
  optionsOverlay.setExtensionAndShowOverlay(GOOD_CRX_ID, 'GOOD!', '', testDone);

  // Preferred size changes don't happen in browser tests. Just fake it.
  var size = {width: 500, height: 500};
  document.querySelector('extensionoptions').onpreferredsizechanged(size);
});

/**
 * @constructor
 * @extends {InstalledExtensionSettingsWebUITest}
 */
function ManagedExtensionSettingsWebUITest() {}

ManagedExtensionSettingsWebUITest.prototype = {
  __proto__: InstalledExtensionSettingsWebUITest.prototype,

  /** @override */
  testGenPreamble: function() {
    GEN('  AddManagedPolicyProvider();');
    InstalledExtensionSettingsWebUITest.prototype.testGenPreamble.call(this);
  },
};

TEST_F('ManagedExtensionSettingsWebUITest', 'testAccessibility', runAudit);

/**
 * @constructor
 * @extends {InstalledExtensionSettingsWebUITest}
 */
function ExtensionOptionsDialogWebUITest() {}

ExtensionOptionsDialogWebUITest.prototype = {
  __proto__: InstalledExtensionSettingsWebUITest.prototype,

  /** @override */
  browsePreload: ExtensionSettingsWebUITest.prototype.browsePreload +
      '?options=' + GOOD_CRX_ID,
};

TEST_F('ExtensionOptionsDialogWebUITest', 'testAccessibility', runAudit);
