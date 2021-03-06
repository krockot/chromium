// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

    Polymer('more-switch', {
      publish: {
        /**
         * The index of the selected element (0-indexed), or -1 if nothing is
         * selected.
         *
         * @attribute selectedIndex
         * @type number
         * @default -1
         */
        selectedIndex: -1,
      },

      attached: function() {
        // TODO(nevir): Handle childen that are added/removed after this point.
        this.observeChildren();
        this.evaluate();
      },

      selectedIndexChanged: function() {
        // TODO(nevir): Why isn't type hinting working here?
        this._setActiveChild(Number(this.selectedIndex), true);
      },

      /**
       * Re-evaluates the conditions of each child, to determine which should be
       * selected.
       *
       * Subclasses should call this whenever they think the truthiness of a
       * child has changed.
       *
       * @method evaluate
       * @protected
       */
      evaluate: function() {
        var index = -1;
        for (var i = 0, child; child = this.children[i]; i++) {
          if (this.childIsTruthy(child, i)) {
            index = i;
            break;
          }
        }
        this._setActiveChild(index, false);
      },

      // Subclass Hooks

      /**
       * Registers mutation observers for the conditions expressed by children.
       *
       * Subclasses should override this method if they need to express
       * conditions in some other way.
       *
       * @method observeChildren
       * @protected
       */
      observeChildren: function() {
        // We observe attribute changes, as the assumption is that you will be
        // using a `template-switch` within a template context. Thus, the `when`
        // attributes will be already bound. We just have to infer their
        // truthiness whenever they change.
        this._observer = new MutationObserver(this._onAttributesChanged.bind(this));
        for (var i = 0, child; child = this.children[i]; i++) {
          this._observer.observe(child, {attributes: true});
        }
      },

      /**
       * @method childIsTruthy
       * @protected
       * @param {!HTMLElement} child The child element.
       * @param {number}       index The index of the child.
       * @return {boolean} Whether the child's conditions are truthy.
       */
      childIsTruthy: function(child, index) {
        return child.hasAttribute('else') || this._stringIsTruthy(child.getAttribute('when'));
      },

      /**
       * @method activateChild
       * @protected
       * @param {!HTMLElement} child    The child element.
       * @param {number}       index    The index of the child.
       * @param {boolean}      external Whether this child is being activated by
       *     an external event (for example, `selectedIndex` was assigned to).
       */
      activateChild: function(child, index, external) {
        for (var i = 0, child; child = this.children[i]; i++) {
          if (i === index) {
            child.removeAttribute('hidden');
          } else {
            child.setAttribute('hidden', '');
          }
        }
      },

      // Private Implementation

      _onAttributesChanged: function(mutations) {
        for (var i = 0, mutation; mutation = mutations[i]; i++) {
          if (mutation.attributeName === 'when') {
            this.evaluate();
            return;
          }
        }
      },

      _setActiveChild: function(index, external) {
        if (this._activeChild === index) return;
        this._activeChild  = index;
        this.selectedIndex = index;

        this.activateChild(this.children[index], index, external);
      },

      // TODO(nevir): This is dirty. Can we evaluate the binding expressions
      // ourselves?
      _stringIsTruthy: function(string) {
        if (!string) return false;
        if (string === 'false') return false;
        if (string === '0') return false;
        if (string === 'NaN') return false;
        return true;
      },
    });
  
