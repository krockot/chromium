// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_RENDERER_SCRIPT_CONTEXT_H_
#define EXTENSIONS_RENDERER_SCRIPT_CONTEXT_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/compiler_specific.h"
#include "extensions/common/features/feature.h"
#include "extensions/common/permissions/api_permission_set.h"
#include "extensions/renderer/module_system.h"
#include "extensions/renderer/request_sender.h"
#include "extensions/renderer/safe_builtins.h"
#include "gin/runner.h"
#include "url/gurl.h"
#include "v8/include/v8.h"

namespace blink {
class WebFrame;
class WebLocalFrame;
}

namespace content {
class RenderFrame;
class RenderView;
}

namespace extensions {
class Extension;
class ExtensionSet;

// Extensions wrapper for a v8 context.
class ScriptContext : public RequestSender::Source {
 public:
  ScriptContext(const v8::Handle<v8::Context>& context,
                blink::WebLocalFrame* frame,
                const Extension* extension,
                Feature::Context context_type,
                const Extension* effective_extension,
                Feature::Context effective_context_type);
  ~ScriptContext() override;

  // Returns whether |url| is sandboxed (as declared in any Extension in
  // |extension_set| as sandboxed).
  //
  // Declared in ScriptContext for lack of a better place, but this should
  // become unnecessary at some point as crbug.com/466373 is worked on.
  static bool IsSandboxedPage(const ExtensionSet& extension_set,
                              const GURL& url);

  // Clears the WebFrame for this contexts and invalidates the associated
  // ModuleSystem.
  void Invalidate();

  // Registers |observer| to be run when this context is invalidated. Closures
  // are run immediately when Invalidate() is called, not in a message loop.
  void AddInvalidationObserver(const base::Closure& observer);

  // Returns true if this context is still valid, false if it isn't.
  // A context becomes invalid via Invalidate().
  bool is_valid() const { return is_valid_; }

  v8::Handle<v8::Context> v8_context() const {
    return v8::Local<v8::Context>::New(isolate_, v8_context_);
  }

  const Extension* extension() const { return extension_.get(); }

  const Extension* effective_extension() const {
    return effective_extension_.get();
  }

  blink::WebLocalFrame* web_frame() const { return web_frame_; }

  Feature::Context context_type() const { return context_type_; }

  Feature::Context effective_context_type() const {
    return effective_context_type_;
  }

  void set_module_system(scoped_ptr<ModuleSystem> module_system) {
    module_system_ = module_system.Pass();
  }

  ModuleSystem* module_system() { return module_system_.get(); }

  SafeBuiltins* safe_builtins() { return &safe_builtins_; }

  const SafeBuiltins* safe_builtins() const { return &safe_builtins_; }

  // Returns the ID of the extension associated with this context, or empty
  // string if there is no such extension.
  const std::string& GetExtensionID() const;

  // Returns the RenderView associated with this context. Can return NULL if the
  // context is in the process of being destroyed.
  content::RenderView* GetRenderView() const;

  // Returns the RenderFrame associated with this context. Can return NULL if
  // the context is in the process of being destroyed.
  content::RenderFrame* GetRenderFrame() const;

  // Runs |function| with appropriate scopes. Doesn't catch exceptions, callers
  // must do that if they want.
  //
  // USE THIS METHOD RATHER THAN v8::Function::Call WHEREVER POSSIBLE.
  v8::Local<v8::Value> CallFunction(v8::Handle<v8::Function> function,
                                    int argc,
                                    v8::Handle<v8::Value> argv[]) const;

  void DispatchEvent(const char* event_name, v8::Handle<v8::Array> args) const;

  // Fires the onunload event on the unload_event module.
  void DispatchOnUnloadEvent();

  // Returns the availability of the API |api_name|.
  Feature::Availability GetAvailability(const std::string& api_name);

  // Returns a string description of the type of context this is.
  std::string GetContextTypeDescription();

  // Returns a string description of the effective type of context this is.
  std::string GetEffectiveContextTypeDescription();

  v8::Isolate* isolate() const { return isolate_; }

  // Get the URL of this context's web frame.
  //
  // TODO(kalman): Remove this and replace with a GetOrigin() call which reads
  // of WebDocument::securityOrigin():
  //  - The URL can change (e.g. pushState) but the origin cannot. Luckily it
  //    appears as though callers don't make security decisions based on the
  //    result of GetURL() so it's not a problem... yet.
  //  - Origin is the correct check to be making.
  //  - It might let us remove the about:blank resolving?
  GURL GetURL() const;

  // Returns whether the API |api| or any part of the API could be
  // available in this context without taking into account the context's
  // extension.
  bool IsAnyFeatureAvailableToContext(const extensions::Feature& api);

  // Utility to get the URL we will match against for a frame. If the frame has
  // committed, this is the commited URL. Otherwise it is the provisional URL.
  // The returned URL may be invalid.
  static GURL GetDataSourceURLForFrame(const blink::WebFrame* frame);

  // Returns the first non-about:-URL in the document hierarchy above and
  // including |frame|. The document hierarchy is only traversed if
  // |document_url| is an about:-URL and if |match_about_blank| is true.
  static GURL GetEffectiveDocumentURL(const blink::WebFrame* frame,
                                      const GURL& document_url,
                                      bool match_about_blank);

  // RequestSender::Source implementation.
  ScriptContext* GetContext() override;
  void OnResponseReceived(const std::string& name,
                          int request_id,
                          bool success,
                          const base::ListValue& response,
                          const std::string& error) override;

  // Grants a set of content capabilities to this context.
  void SetContentCapabilities(const APIPermissionSet& permissions);

  // Indicates if this context has an effective API permission either by being
  // a context for an extension which has that permission, or by being a web
  // context which has been granted the corresponding capability by an
  // extension.
  bool HasAPIPermission(APIPermission::ID permission) const;

 private:
  class Runner;

  // Whether this context is valid.
  bool is_valid_;

  // The v8 context the bindings are accessible to.
  v8::Global<v8::Context> v8_context_;

  // The WebLocalFrame associated with this context. This can be NULL because
  // this object can outlive is destroyed asynchronously.
  blink::WebLocalFrame* web_frame_;

  // The extension associated with this context, or NULL if there is none. This
  // might be a hosted app in the case that this context is hosting a web URL.
  scoped_refptr<const Extension> extension_;

  // The type of context.
  Feature::Context context_type_;

  // The effective extension associated with this context, or NULL if there is
  // none. This is different from the above extension if this context is in an
  // about:blank iframe for example.
  scoped_refptr<const Extension> effective_extension_;

  // The type of context.
  Feature::Context effective_context_type_;

  // Owns and structures the JS that is injected to set up extension bindings.
  scoped_ptr<ModuleSystem> module_system_;

  // Contains safe copies of builtin objects like Function.prototype.
  SafeBuiltins safe_builtins_;

  // The set of capabilities granted to this context by extensions.
  APIPermissionSet content_capabilities_;

  // A list of base::Closure instances as an observer interface for
  // invalidation.
  std::vector<base::Closure> invalidate_observers_;

  v8::Isolate* isolate_;

  GURL url_;

  scoped_ptr<Runner> runner_;

  DISALLOW_COPY_AND_ASSIGN(ScriptContext);
};

}  // namespace extensions

#endif  // EXTENSIONS_RENDERER_SCRIPT_CONTEXT_H_
