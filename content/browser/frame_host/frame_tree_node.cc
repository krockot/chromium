// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/frame_host/frame_tree_node.h"

#include <queue>

#include "base/command_line.h"
#include "base/profiler/scoped_tracker.h"
#include "base/stl_util.h"
#include "content/browser/frame_host/frame_tree.h"
#include "content/browser/frame_host/navigation_request.h"
#include "content/browser/frame_host/navigator.h"
#include "content/browser/frame_host/render_frame_host_impl.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/content_switches.h"

namespace content {

namespace {

// This is a global map between frame_tree_node_ids and pointers to
// FrameTreeNodes.
typedef base::hash_map<int64, FrameTreeNode*> FrameTreeNodeIDMap;

base::LazyInstance<FrameTreeNodeIDMap> g_frame_tree_node_id_map =
    LAZY_INSTANCE_INITIALIZER;

// These values indicate the loading progress status. The minimum progress
// value matches what Blink's ProgressTracker has traditionally used for a
// minimum progress value.
const double kLoadingProgressNotStarted = 0.0;
const double kLoadingProgressMinimum = 0.1;
const double kLoadingProgressDone = 1.0;

}  // namespace

int64 FrameTreeNode::next_frame_tree_node_id_ = 1;

// static
FrameTreeNode* FrameTreeNode::GloballyFindByID(int64 frame_tree_node_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  FrameTreeNodeIDMap* nodes = g_frame_tree_node_id_map.Pointer();
  FrameTreeNodeIDMap::iterator it = nodes->find(frame_tree_node_id);
  return it == nodes->end() ? nullptr : it->second;
}

FrameTreeNode::FrameTreeNode(FrameTree* frame_tree,
                             Navigator* navigator,
                             RenderFrameHostDelegate* render_frame_delegate,
                             RenderViewHostDelegate* render_view_delegate,
                             RenderWidgetHostDelegate* render_widget_delegate,
                             RenderFrameHostManager::Delegate* manager_delegate,
                             const std::string& name)
    : frame_tree_(frame_tree),
      navigator_(navigator),
      render_manager_(this,
                      render_frame_delegate,
                      render_view_delegate,
                      render_widget_delegate,
                      manager_delegate),
      frame_tree_node_id_(next_frame_tree_node_id_++),
      parent_(NULL),
      replication_state_(name),
      effective_sandbox_flags_(SandboxFlags::NONE),
      loading_progress_(kLoadingProgressNotStarted) {
  std::pair<FrameTreeNodeIDMap::iterator, bool> result =
      g_frame_tree_node_id_map.Get().insert(
          std::make_pair(frame_tree_node_id_, this));
  CHECK(result.second);
}

FrameTreeNode::~FrameTreeNode() {
  frame_tree_->FrameRemoved(this);

  g_frame_tree_node_id_map.Get().erase(frame_tree_node_id_);
}

bool FrameTreeNode::IsMainFrame() const {
  return frame_tree_->root() == this;
}

void FrameTreeNode::AddChild(scoped_ptr<FrameTreeNode> child,
                             int process_id,
                             int frame_routing_id) {
  // Child frame must always be created in the same process as the parent.
  CHECK_EQ(process_id, render_manager_.current_host()->GetProcess()->GetID());

  // Initialize the RenderFrameHost for the new node.  We always create child
  // frames in the same SiteInstance as the current frame, and they can swap to
  // a different one if they navigate away.
  child->render_manager()->Init(
      render_manager_.current_host()->GetSiteInstance()->GetBrowserContext(),
      render_manager_.current_host()->GetSiteInstance(),
      render_manager_.current_host()->GetRoutingID(),
      frame_routing_id);
  child->set_parent(this);

  // Other renderer processes in this BrowsingInstance may need to find out
  // about the new frame.  Create a proxy for the child frame in all
  // SiteInstances that have a proxy for the frame's parent, since all frames
  // in a frame tree should have the same set of proxies.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kSitePerProcess))
    render_manager_.CreateProxiesForChildFrame(child.get());

  children_.push_back(child.release());
}

void FrameTreeNode::RemoveChild(FrameTreeNode* child) {
  std::vector<FrameTreeNode*>::iterator iter;
  for (iter = children_.begin(); iter != children_.end(); ++iter) {
    if ((*iter) == child)
      break;
  }

  if (iter != children_.end()) {
    // Subtle: we need to make sure the node is gone from the tree before
    // observers are notified of its deletion.
    scoped_ptr<FrameTreeNode> node_to_delete(*iter);
    children_.weak_erase(iter);
    node_to_delete.reset();
  }
}

void FrameTreeNode::ResetForNewProcess() {
  current_url_ = GURL();

  // The children may not have been cleared if a cross-process navigation
  // commits before the old process cleans everything up.  Make sure the child
  // nodes get deleted before swapping to a new process.
  ScopedVector<FrameTreeNode> old_children = children_.Pass();
  old_children.clear();  // May notify observers.
}

void FrameTreeNode::SetFrameName(const std::string& name) {
  replication_state_.name = name;

  // Notify this frame's proxies about the updated name.
  render_manager_.OnDidUpdateName(name);
}

bool FrameTreeNode::IsDescendantOf(FrameTreeNode* other) const {
  if (!other || !other->child_count())
    return false;

  for (FrameTreeNode* node = parent(); node; node = node->parent()) {
    if (node == other)
      return true;
  }

  return false;
}

bool FrameTreeNode::IsLoading() const {
  RenderFrameHostImpl* current_frame_host =
      render_manager_.current_frame_host();
  RenderFrameHostImpl* pending_frame_host =
      render_manager_.pending_frame_host();

  DCHECK(current_frame_host);
  // TODO(fdegans): Change the implementation logic for PlzNavigate once
  // DidStartLoading and DidStopLoading are properly called.
  if (pending_frame_host && pending_frame_host->is_loading())
    return true;
  return current_frame_host->is_loading();
}

bool FrameTreeNode::CommitPendingSandboxFlags() {
  bool did_change_flags =
      effective_sandbox_flags_ != replication_state_.sandbox_flags;
  effective_sandbox_flags_ = replication_state_.sandbox_flags;
  return did_change_flags;
}

void FrameTreeNode::SetNavigationRequest(
    scoped_ptr<NavigationRequest> navigation_request) {
  CHECK(base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableBrowserSideNavigation));
  ResetNavigationRequest(false);
  // TODO(clamy): perform the StartLoading logic here.
  navigation_request_ = navigation_request.Pass();
}

void FrameTreeNode::ResetNavigationRequest(bool is_commit) {
  CHECK(base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableBrowserSideNavigation));
  // Upon commit the current NavigationRequest will be reset. There should be no
  // cleanup performed since the navigation is still ongoing. If the reset
  // corresponds to a cancelation, the RenderFrameHostManager should clean up
  // any speculative RenderFrameHost it created for the navigation.
  if (navigation_request_ && !is_commit) {
    // TODO(clamy): perform the StopLoading logic.
    render_manager_.CleanUpNavigation();
  }
  navigation_request_.reset();
}

bool FrameTreeNode::has_started_loading() const {
  return loading_progress_ != kLoadingProgressNotStarted;
}

void FrameTreeNode::reset_loading_progress() {
  loading_progress_ = kLoadingProgressNotStarted;
}

void FrameTreeNode::DidStartLoading(bool to_different_document) {
  // Any main frame load to a new document should reset the load progress since
  // it will replace the current page and any frames. The WebContents will
  // be notified when DidChangeLoadProgress is called.
  if (to_different_document && IsMainFrame())
    frame_tree_->ResetLoadProgress();

  // Notify the WebContents.
  if (!frame_tree_->IsLoading())
    navigator()->GetDelegate()->DidStartLoading(this, to_different_document);

  // Set initial load progress and update overall progress. This will notify
  // the WebContents of the load progress change.
  DidChangeLoadProgress(kLoadingProgressMinimum);

  // Notify the RenderFrameHostManager of the event.
  render_manager()->OnDidStartLoading();
}

void FrameTreeNode::DidStopLoading() {
  // TODO(erikchen): Remove ScopedTracker below once crbug.com/465796 is fixed.
  tracked_objects::ScopedTracker tracking_profile1(
      FROM_HERE_WITH_EXPLICIT_FUNCTION(
          "465796 FrameTreeNode::DidStopLoading::Start"));

  // Set final load progress and update overall progress. This will notify
  // the WebContents of the load progress change.
  DidChangeLoadProgress(kLoadingProgressDone);

  // TODO(erikchen): Remove ScopedTracker below once crbug.com/465796 is fixed.
  tracked_objects::ScopedTracker tracking_profile2(
      FROM_HERE_WITH_EXPLICIT_FUNCTION(
          "465796 FrameTreeNode::DidStopLoading::WCIDidStopLoading"));

  // Notify the WebContents.
  if (!frame_tree_->IsLoading())
    navigator()->GetDelegate()->DidStopLoading();

  // TODO(erikchen): Remove ScopedTracker below once crbug.com/465796 is fixed.
  tracked_objects::ScopedTracker tracking_profile3(
      FROM_HERE_WITH_EXPLICIT_FUNCTION(
          "465796 FrameTreeNode::DidStopLoading::RFHMDidStopLoading"));

  // Notify the RenderFrameHostManager of the event.
  render_manager()->OnDidStopLoading();

  // TODO(erikchen): Remove ScopedTracker below once crbug.com/465796 is fixed.
  tracked_objects::ScopedTracker tracking_profile4(
      FROM_HERE_WITH_EXPLICIT_FUNCTION(
          "465796 FrameTreeNode::DidStopLoading::End"));
}

void FrameTreeNode::DidChangeLoadProgress(double load_progress) {
  loading_progress_ = load_progress;
  frame_tree_->UpdateLoadProgress();
}

}  // namespace content
