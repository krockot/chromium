// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/guest_view/guest_view_base.h"

#include "base/lazy_instance.h"
#include "base/strings/utf_string_conversions.h"
#include "components/ui/zoom/zoom_controller.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/page_zoom.h"
#include "content/public/common/url_constants.h"
#include "extensions/browser/api/extensions_api_client.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/guest_view/app_view/app_view_guest.h"
#include "extensions/browser/guest_view/extension_options/extension_options_guest.h"
#include "extensions/browser/guest_view/guest_view_manager.h"
#include "extensions/browser/guest_view/mime_handler_view/mime_handler_view_guest.h"
#include "extensions/browser/guest_view/web_view/web_view_guest.h"
#include "extensions/browser/guest_view/worker_frame/worker_frame_guest.h"
#include "extensions/browser/process_map.h"
#include "extensions/common/extension_messages.h"
#include "extensions/common/features/feature.h"
#include "extensions/common/features/feature_provider.h"
#include "extensions/common/guest_view/guest_view_constants.h"
#include "third_party/WebKit/public/web/WebInputEvent.h"

using content::WebContents;

namespace content {
struct FrameNavigateParams;
}

namespace extensions {

namespace {

typedef std::map<std::string, GuestViewBase::GuestCreationCallback>
    GuestViewCreationMap;
static base::LazyInstance<GuestViewCreationMap> guest_view_registry =
    LAZY_INSTANCE_INITIALIZER;

typedef std::map<WebContents*, GuestViewBase*> WebContentsGuestViewMap;
static base::LazyInstance<WebContentsGuestViewMap> webcontents_guestview_map =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

GuestViewBase::Event::Event(const std::string& name,
                            scoped_ptr<base::DictionaryValue> args)
    : name_(name), args_(args.Pass()) {
}

GuestViewBase::Event::~Event() {
}

scoped_ptr<base::DictionaryValue> GuestViewBase::Event::GetArguments() {
  return args_.Pass();
}

// This observer ensures that the GuestViewBase destroys itself when its
// embedder goes away.
class GuestViewBase::OwnerLifetimeObserver : public WebContentsObserver {
 public:
  OwnerLifetimeObserver(GuestViewBase* guest,
                        content::WebContents* embedder_web_contents)
      : WebContentsObserver(embedder_web_contents),
        destroyed_(false),
        guest_(guest) {}

  ~OwnerLifetimeObserver() override {}

  // WebContentsObserver implementation.
  void WebContentsDestroyed() override {
    // If the embedder is destroyed then destroy the guest.
    Destroy();
  }

  void DidNavigateMainFrame(
      const content::LoadCommittedDetails& details,
      const content::FrameNavigateParams& params) override {
    // If the embedder navigates to a different page then destroy the guest.
    if (details.is_navigation_to_different_page())
      Destroy();
  }

  void RenderProcessGone(base::TerminationStatus status) override {
    // If the embedder crashes, then destroy the guest.
    Destroy();
  }

 private:
  bool destroyed_;
  GuestViewBase* guest_;

  void Destroy() {
    if (destroyed_)
      return;

    destroyed_ = true;
    guest_->EmbedderWillBeDestroyed();
    guest_->Destroy();
  }

  DISALLOW_COPY_AND_ASSIGN(OwnerLifetimeObserver);
};

// This observer ensures that the GuestViewBase destroys itself when its
// embedder goes away.
class GuestViewBase::OpenerLifetimeObserver : public WebContentsObserver {
 public:
  OpenerLifetimeObserver(GuestViewBase* guest)
      : WebContentsObserver(guest->GetOpener()->web_contents()),
        guest_(guest) {}

  ~OpenerLifetimeObserver() override {}

  // WebContentsObserver implementation.
  void WebContentsDestroyed() override {
    if (guest_->attached())
      return;

    // If the opener is destroyed then destroy the guest.
    guest_->Destroy();
  }

 private:
  GuestViewBase* guest_;

  DISALLOW_COPY_AND_ASSIGN(OpenerLifetimeObserver);
};

GuestViewBase::GuestViewBase(content::BrowserContext* browser_context,
                             content::WebContents* owner_web_contents,
                             int guest_instance_id)
    : owner_web_contents_(owner_web_contents),
      browser_context_(browser_context),
      guest_instance_id_(guest_instance_id),
      view_instance_id_(guestview::kInstanceIDNone),
      element_instance_id_(guestview::kInstanceIDNone),
      initialized_(false),
      is_being_destroyed_(false),
      auto_size_enabled_(false),
      is_full_page_plugin_(false),
      observing_owners_zoom_controller_(false),
      weak_ptr_factory_(this) {
}

void GuestViewBase::Init(const std::string& owner_extension_id,
                         const base::DictionaryValue& create_params,
                         const WebContentsCreatedCallback& callback) {
  if (initialized_)
    return;
  initialized_ = true;

  Feature* feature = FeatureProvider::GetAPIFeatures()->GetFeature(
      GetAPINamespace());
  CHECK(feature);

  ProcessMap* process_map = ProcessMap::Get(browser_context());
  CHECK(process_map);

  const Extension* embedder_extension = ExtensionRegistry::Get(browser_context_)
          ->enabled_extensions()
          .GetByID(owner_extension_id);

  // Ok for |embedder_extension| to be NULL, the embedder might be WebUI.
  Feature::Availability availability = feature->IsAvailableToContext(
      embedder_extension,
      process_map->GetMostLikelyContextType(
          embedder_extension,
          owner_web_contents()->GetRenderProcessHost()->GetID()),
      GetOwnerSiteURL());
  if (!availability.is_available()) {
    // The derived class did not create a WebContents so this class serves no
    // purpose. Let's self-destruct.
    delete this;
    callback.Run(NULL);
    return;
  }

  CreateWebContents(create_params,
                    base::Bind(&GuestViewBase::CompleteInit,
                               weak_ptr_factory_.GetWeakPtr(),
                               owner_extension_id,
                               callback));
}

void GuestViewBase::InitWithWebContents(
    const std::string& owner_extension_id,
    content::WebContents* guest_web_contents) {
  DCHECK(guest_web_contents);

  owner_extension_id_ = owner_extension_id;

  // At this point, we have just created the guest WebContents, we need to add
  // an observer to the embedder WebContents. This observer will be responsible
  // for destroying the guest WebContents if the embedder goes away.
  owner_lifetime_observer_.reset(
      new OwnerLifetimeObserver(this, owner_web_contents_));

  WebContentsObserver::Observe(guest_web_contents);
  guest_web_contents->SetDelegate(this);
  webcontents_guestview_map.Get().insert(
      std::make_pair(guest_web_contents, this));
  GuestViewManager::FromBrowserContext(browser_context_)->
      AddGuest(guest_instance_id_, guest_web_contents);

  // Create a ZoomController to allow the guest's contents to be zoomed.
  ui_zoom::ZoomController::CreateForWebContents(guest_web_contents);

  // Give the derived class an opportunity to perform additional initialization.
  DidInitialize();
}

void GuestViewBase::SetAutoSize(bool enabled,
                                const gfx::Size& min_size,
                                const gfx::Size& max_size) {
  min_auto_size_ = min_size;
  min_auto_size_.SetToMin(max_size);
  max_auto_size_ = max_size;
  max_auto_size_.SetToMax(min_size);

  enabled &= !min_auto_size_.IsEmpty() && !max_auto_size_.IsEmpty() &&
      IsAutoSizeSupported();
  if (!enabled && !auto_size_enabled_)
    return;

  auto_size_enabled_ = enabled;

  if (!attached())
    return;

  content::RenderViewHost* rvh = web_contents()->GetRenderViewHost();
  if (auto_size_enabled_) {
    rvh->EnableAutoResize(min_auto_size_, max_auto_size_);
  } else {
    rvh->DisableAutoResize(element_size_);
    guest_size_ = element_size_;
    GuestSizeChangedDueToAutoSize(guest_size_, element_size_);
  }
}

// static
void GuestViewBase::RegisterGuestViewType(
    const std::string& view_type,
    const GuestCreationCallback& callback) {
  GuestViewCreationMap::iterator it =
      guest_view_registry.Get().find(view_type);
  DCHECK(it == guest_view_registry.Get().end());
  guest_view_registry.Get()[view_type] = callback;
}

// static
GuestViewBase* GuestViewBase::Create(
    content::BrowserContext* browser_context,
    content::WebContents* owner_web_contents,
    int guest_instance_id,
    const std::string& view_type) {
  if (guest_view_registry.Get().empty())
    RegisterGuestViewTypes();

  GuestViewCreationMap::iterator it =
      guest_view_registry.Get().find(view_type);
  if (it == guest_view_registry.Get().end()) {
    NOTREACHED();
    return NULL;
  }
  return it->second.Run(browser_context, owner_web_contents, guest_instance_id);
}

// static
GuestViewBase* GuestViewBase::FromWebContents(WebContents* web_contents) {
  WebContentsGuestViewMap* guest_map = webcontents_guestview_map.Pointer();
  WebContentsGuestViewMap::iterator it = guest_map->find(web_contents);
  return it == guest_map->end() ? NULL : it->second;
}

// static
GuestViewBase* GuestViewBase::From(int embedder_process_id,
                                   int guest_instance_id) {
  content::RenderProcessHost* host =
      content::RenderProcessHost::FromID(embedder_process_id);
  if (!host)
    return NULL;

  content::WebContents* guest_web_contents =
      GuestViewManager::FromBrowserContext(host->GetBrowserContext())->
          GetGuestByInstanceIDSafely(guest_instance_id, embedder_process_id);
  if (!guest_web_contents)
    return NULL;

  return GuestViewBase::FromWebContents(guest_web_contents);
}

// static
bool GuestViewBase::IsGuest(WebContents* web_contents) {
  return !!GuestViewBase::FromWebContents(web_contents);
}

bool GuestViewBase::IsAutoSizeSupported() const {
  return false;
}

bool GuestViewBase::IsDragAndDropEnabled() const {
  return false;
}

void GuestViewBase::DidAttach(int guest_proxy_routing_id) {
  opener_lifetime_observer_.reset();

  // Any zoom events from the embedder should be relayed to the guest.
  StartObservingOwnersZoomController();

  // Give the derived class an opportunity to perform some actions.
  DidAttachToEmbedder();

  // Inform the associated GuestViewContainer that the contentWindow is ready.
  embedder_web_contents()->Send(new ExtensionMsg_GuestAttached(
      embedder_web_contents()->GetMainFrame()->GetRoutingID(),
      element_instance_id_,
      guest_proxy_routing_id));

  SendQueuedEvents();
}

void GuestViewBase::DidDetach() {
  GuestViewManager::FromBrowserContext(browser_context_)->DetachGuest(
      this, element_instance_id_);
  owner_web_contents()->Send(new ExtensionMsg_GuestDetached(
      owner_web_contents()->GetMainFrame()->GetRoutingID(),
      element_instance_id_));
  element_instance_id_ = guestview::kInstanceIDNone;
}

void GuestViewBase::ElementSizeChanged(const gfx::Size& old_size,
                                       const gfx::Size& new_size) {
  element_size_ = new_size;
}

WebContents* GuestViewBase::GetOwnerWebContents() const {
  return owner_web_contents_;
}

void GuestViewBase::GuestSizeChanged(const gfx::Size& old_size,
                                     const gfx::Size& new_size) {
  if (!auto_size_enabled_)
    return;
  guest_size_ = new_size;
  GuestSizeChangedDueToAutoSize(old_size, new_size);
}

const GURL& GuestViewBase::GetOwnerSiteURL() const {
  return owner_web_contents()->GetLastCommittedURL();
}

void GuestViewBase::Destroy() {
  if (is_being_destroyed_)
    return;

  is_being_destroyed_ = true;

  // It is important to clear owner_web_contents_ after the call to
  // StopObservingOwnersZoomControllerIfNecessary(), but before the rest of
  // the statements in this function.
  StopObservingOwnersZoomControllerIfNecessary();
  owner_web_contents_ = NULL;

  DCHECK(web_contents());

  // Give the derived class an opportunity to perform some cleanup.
  WillDestroy();

  // Invalidate weak pointers now so that bound callbacks cannot be called late
  // into destruction. We must call this after WillDestroy because derived types
  // may wish to access their openers.
  weak_ptr_factory_.InvalidateWeakPtrs();

  // Give the content module an opportunity to perform some cleanup.
  if (!destruction_callback_.is_null())
    destruction_callback_.Run();

  webcontents_guestview_map.Get().erase(web_contents());
  GuestViewManager::FromBrowserContext(browser_context_)->
      RemoveGuest(guest_instance_id_);
  pending_events_.clear();

  delete web_contents();
}

void GuestViewBase::SetAttachParams(const base::DictionaryValue& params) {
  attach_params_.reset(params.DeepCopy());
  attach_params_->GetInteger(guestview::kParameterInstanceId,
                             &view_instance_id_);
}

void GuestViewBase::SetOpener(GuestViewBase* guest) {
  if (guest && guest->IsViewType(GetViewType())) {
    opener_ = guest->weak_ptr_factory_.GetWeakPtr();
    if (!attached())
      opener_lifetime_observer_.reset(new OpenerLifetimeObserver(this));
    return;
  }
  opener_ = base::WeakPtr<GuestViewBase>();
  opener_lifetime_observer_.reset();
}

void GuestViewBase::RegisterDestructionCallback(
    const DestructionCallback& callback) {
  destruction_callback_ = callback;
}

void GuestViewBase::WillAttach(content::WebContents* embedder_web_contents,
                               int element_instance_id,
                               bool is_full_page_plugin) {
  owner_web_contents_ = embedder_web_contents;

  // If we are attaching to a different WebContents than the one that created
  // the guest, we need to create a new LifetimeObserver.
  if (embedder_web_contents != owner_lifetime_observer_->web_contents()) {
    owner_lifetime_observer_.reset(
        new OwnerLifetimeObserver(this, embedder_web_contents));
  }

  element_instance_id_ = element_instance_id;
  is_full_page_plugin_ = is_full_page_plugin;

  WillAttachToEmbedder();
}

void GuestViewBase::DidStopLoading(content::RenderViewHost* render_view_host) {
  if (!IsDragAndDropEnabled()) {
    const char script[] = "window.addEventListener('dragstart', function() { "
                          "  window.event.preventDefault(); "
                          "});";
    render_view_host->GetMainFrame()->ExecuteJavaScript(
        base::ASCIIToUTF16(script));
  }
  DidStopLoading();
}

void GuestViewBase::RenderViewReady() {
  GuestReady();
}

void GuestViewBase::WebContentsDestroyed() {
  // Let the derived class know that its WebContents is in the process of
  // being destroyed. web_contents() is still valid at this point.
  // TODO(fsamuel): This allows for reentrant code into WebContents during
  // destruction. This could potentially lead to bugs. Perhaps we should get rid
  // of this?
  GuestDestroyed();

  // Self-destruct.
  delete this;
}

void GuestViewBase::ActivateContents(WebContents* web_contents) {
  if (!attached() || !embedder_web_contents()->GetDelegate())
    return;

  embedder_web_contents()->GetDelegate()->ActivateContents(
      embedder_web_contents());
}

void GuestViewBase::DeactivateContents(WebContents* web_contents) {
  if (!attached() || !embedder_web_contents()->GetDelegate())
    return;

  embedder_web_contents()->GetDelegate()->DeactivateContents(
      embedder_web_contents());
}

void GuestViewBase::RunFileChooser(WebContents* web_contents,
                                   const content::FileChooserParams& params) {
  if (!attached() || !embedder_web_contents()->GetDelegate())
    return;

  embedder_web_contents()->GetDelegate()->RunFileChooser(web_contents, params);
}

bool GuestViewBase::ShouldFocusPageAfterCrash() {
  // Focus is managed elsewhere.
  return false;
}

bool GuestViewBase::PreHandleGestureEvent(content::WebContents* source,
                                         const blink::WebGestureEvent& event) {
  return event.type == blink::WebGestureEvent::GesturePinchBegin ||
      event.type == blink::WebGestureEvent::GesturePinchUpdate ||
      event.type == blink::WebGestureEvent::GesturePinchEnd;
}

GuestViewBase::~GuestViewBase() {
}

void GuestViewBase::OnZoomChanged(
    const ui_zoom::ZoomController::ZoomChangedEventData& data) {
  if (content::ZoomValuesEqual(data.old_zoom_level, data.new_zoom_level))
    return;

  // When the embedder's zoom level is changed, then we also update the
  // guest's zoom level to match.
  ui_zoom::ZoomController::FromWebContents(web_contents())
      ->SetZoomLevel(data.new_zoom_level);
}

void GuestViewBase::DispatchEventToEmbedder(Event* event) {
  scoped_ptr<Event> event_ptr(event);

  if (!attached()) {
    pending_events_.push_back(linked_ptr<Event>(event_ptr.release()));
    return;
  }

  EventFilteringInfo info;
  info.SetInstanceID(view_instance_id_);
  scoped_ptr<base::ListValue> args(new base::ListValue());
  args->Append(event->GetArguments().release());

  EventRouter::DispatchEvent(
      owner_web_contents_,
      browser_context_,
      owner_extension_id_,
      event->name(),
      args.Pass(),
      EventRouter::USER_GESTURE_UNKNOWN,
      info);
}

void GuestViewBase::SendQueuedEvents() {
  if (!attached())
    return;
  while (!pending_events_.empty()) {
    linked_ptr<Event> event_ptr = pending_events_.front();
    pending_events_.pop_front();
    DispatchEventToEmbedder(event_ptr.release());
  }
}

void GuestViewBase::CompleteInit(const std::string& owner_extension_id,
                                 const WebContentsCreatedCallback& callback,
                                 content::WebContents* guest_web_contents) {
  if (!guest_web_contents) {
    // The derived class did not create a WebContents so this class serves no
    // purpose. Let's self-destruct.
    delete this;
    callback.Run(NULL);
    return;
  }
  InitWithWebContents(owner_extension_id, guest_web_contents);
  callback.Run(guest_web_contents);
}

void GuestViewBase::StartObservingOwnersZoomController() {
  ui_zoom::ZoomController* zoom_controller =
      ui_zoom::ZoomController::FromWebContents(embedder_web_contents());
  if (!zoom_controller)
    return;

  // Listen to the embedder's zoom changes.
  zoom_controller->AddObserver(this);
  observing_owners_zoom_controller_ = true;
  // Set the guest's initial zoom level to be equal to the embedder's.
  ui_zoom::ZoomController::FromWebContents(web_contents())
      ->SetZoomLevel(zoom_controller->GetZoomLevel());
}

void GuestViewBase::StopObservingOwnersZoomControllerIfNecessary() {
  // We use owner_web_contents_ below and not embedder_web_contents() since
  // we may have been detached by this point.
  DCHECK(!observing_owners_zoom_controller_ || owner_web_contents_);
  if (!owner_web_contents_ || !observing_owners_zoom_controller_)
    return;

  ui_zoom::ZoomController* zoom_controller =
      ui_zoom::ZoomController::FromWebContents(owner_web_contents_);
  zoom_controller->RemoveObserver(this);
  observing_owners_zoom_controller_ = false;
}

// static
void GuestViewBase::RegisterGuestViewTypes() {
  AppViewGuest::Register();
  ExtensionOptionsGuest::Register();
  MimeHandlerViewGuest::Register();
  WebViewGuest::Register();
  WorkerFrameGuest::Register();
}

}  // namespace extensions
