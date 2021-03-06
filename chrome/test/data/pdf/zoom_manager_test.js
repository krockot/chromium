// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

chrome.test.runTests(function() {
  'use strict';

  class MockViewport {
    constructor() {
      this.zooms = [];
      this.zoom = 1;
    }

    setZoom(zoom) {
      this.zooms.push(zoom);
      this.zoom = zoom;
    }
  }

  /**
   * A mock implementation of the function used by ZoomManager to set the
   * browser zoom level.
   */
  class MockBrowserZoomSetter {
    constructor() {
      this.zoom = 1;
      this.started = false;
    }

    /**
     * The function implementing setBrowserZoomFunction.
     */
    setBrowserZoom(zoom) {
      chrome.test.assertFalse(this.started);

      this.zoom = zoom;
      this.started = true;
      return new Promise(function(resolve, reject) {
        this.resolve_ = resolve;
      }.bind(this));
    }

    /**
     * Resolves the promise returned by a call to setBrowserZoom.
     */
    complete() {
      this.resolve_();
      this.started = false;
    }
  };

  return [
    function testZoomChange() {
      let viewport = new MockViewport();
      let browserZoomSetter = new MockBrowserZoomSetter();
      let zoomManager = new ZoomManager(
          viewport, browserZoomSetter.setBrowserZoom.bind(browserZoomSetter));
      viewport.zoom = 2;
      zoomManager.onPdfZoomChange();
      chrome.test.assertEq(2, browserZoomSetter.zoom);
      chrome.test.assertTrue(browserZoomSetter.started);
      chrome.test.succeed();
    },

    function testZoomChangedBeforeConstruction() {
      let viewport = new MockViewport();
      viewport.zoom = 2;
      let browserZoomSetter = new MockBrowserZoomSetter();
      let zoomManager = new ZoomManager(
          viewport, browserZoomSetter.setBrowserZoom.bind(browserZoomSetter));
      chrome.test.assertEq(2, browserZoomSetter.zoom);
      chrome.test.assertTrue(browserZoomSetter.started);
      chrome.test.succeed();
    },

    function testBrowserZoomChange() {
      let viewport = new MockViewport();
      let zoomManager = new ZoomManager(viewport, Promise.resolve.bind(Promise),
                                        chrome.test.fail);
      zoomManager.onBrowserZoomChange(3);
      chrome.test.assertEq(1, viewport.zooms.length);
      chrome.test.assertEq(3, viewport.zooms[0]);
      chrome.test.assertEq(3, viewport.zoom);
      chrome.test.succeed();
    },

    function testSmallZoomChange() {
      let viewport = new MockViewport();
      let browserZoomSetter = new MockBrowserZoomSetter();
      let zoomManager = new ZoomManager(
          viewport, browserZoomSetter.setBrowserZoom.bind(browserZoomSetter));
      viewport.zoom = 1.0001;
      zoomManager.onPdfZoomChange();
      chrome.test.assertEq(1, browserZoomSetter.zoom);
      chrome.test.assertFalse(browserZoomSetter.started);
      chrome.test.succeed();
    },

    function testSmallBrowserZoomChange() {
      let viewport = new MockViewport();
      let zoomManager = new ZoomManager(viewport, Promise.resolve.bind(Promise),
                                        chrome.test.fail);
      zoomManager.onBrowserZoomChange(0.999);
      chrome.test.assertEq(0, viewport.zooms.length);
      chrome.test.assertEq(1, viewport.zoom);
      chrome.test.succeed();
    },

    function testMultiplePdfZoomChanges() {
      let viewport = new MockViewport();
      let browserZoomSetter = new MockBrowserZoomSetter();
      let zoomManager = new ZoomManager(
          viewport, browserZoomSetter.setBrowserZoom.bind(browserZoomSetter));
      viewport.zoom = 2;
      zoomManager.onPdfZoomChange();
      viewport.zoom = 3;
      zoomManager.onPdfZoomChange();
      chrome.test.assertTrue(browserZoomSetter.started);
      chrome.test.assertEq(2, browserZoomSetter.zoom);
      browserZoomSetter.complete();
      Promise.resolve().then(function() {
        chrome.test.assertTrue(browserZoomSetter.started);
        chrome.test.assertEq(3, browserZoomSetter.zoom);
        chrome.test.succeed();
      });
    },

    function testMultipleBrowserZoomChanges() {
      let viewport = new MockViewport();
      let zoomManager = new ZoomManager(viewport, Promise.resolve.bind(Promise),
                                        chrome.test.fail);
      zoomManager.onBrowserZoomChange(2);
      zoomManager.onBrowserZoomChange(3);
      chrome.test.assertEq(2, viewport.zooms.length);
      chrome.test.assertEq(2, viewport.zooms[0]);
      chrome.test.assertEq(3, viewport.zooms[1]);
      chrome.test.assertEq(3, viewport.zoom);
      chrome.test.succeed();
    },
  ];
}());
