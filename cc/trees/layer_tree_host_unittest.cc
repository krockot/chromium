// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/trees/layer_tree_host.h"

#include <algorithm>

#include "base/auto_reset.h"
#include "base/synchronization/lock.h"
#include "cc/animation/timing_function.h"
#include "cc/debug/frame_rate_counter.h"
#include "cc/layers/content_layer.h"
#include "cc/layers/content_layer_client.h"
#include "cc/layers/io_surface_layer.h"
#include "cc/layers/layer_impl.h"
#include "cc/layers/painted_scrollbar_layer.h"
#include "cc/layers/picture_layer.h"
#include "cc/layers/solid_color_layer.h"
#include "cc/layers/video_layer.h"
#include "cc/output/begin_frame_args.h"
#include "cc/output/compositor_frame_ack.h"
#include "cc/output/copy_output_request.h"
#include "cc/output/copy_output_result.h"
#include "cc/output/output_surface.h"
#include "cc/output/swap_promise.h"
#include "cc/quads/draw_quad.h"
#include "cc/quads/io_surface_draw_quad.h"
#include "cc/quads/tile_draw_quad.h"
#include "cc/resources/prioritized_resource.h"
#include "cc/resources/prioritized_resource_manager.h"
#include "cc/resources/resource_update_queue.h"
#include "cc/test/fake_content_layer.h"
#include "cc/test/fake_content_layer_client.h"
#include "cc/test/fake_content_layer_impl.h"
#include "cc/test/fake_layer_tree_host_client.h"
#include "cc/test/fake_output_surface.h"
#include "cc/test/fake_painted_scrollbar_layer.h"
#include "cc/test/fake_picture_layer.h"
#include "cc/test/fake_picture_layer_impl.h"
#include "cc/test/fake_picture_pile.h"
#include "cc/test/fake_proxy.h"
#include "cc/test/fake_scoped_ui_resource.h"
#include "cc/test/fake_video_frame_provider.h"
#include "cc/test/geometry_test_utils.h"
#include "cc/test/impl_side_painting_settings.h"
#include "cc/test/layer_tree_test.h"
#include "cc/test/test_shared_bitmap_manager.h"
#include "cc/test/test_web_graphics_context_3d.h"
#include "cc/trees/layer_tree_host_impl.h"
#include "cc/trees/layer_tree_impl.h"
#include "cc/trees/single_thread_proxy.h"
#include "cc/trees/thread_proxy.h"
#include "gpu/GLES2/gl2extchromium.h"
#include "skia/ext/refptr.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "third_party/khronos/GLES2/gl2ext.h"
#include "third_party/skia/include/core/SkPicture.h"
#include "ui/gfx/frame_time.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/gfx/geometry/vector2d_conversions.h"

using testing::_;
using testing::AnyNumber;
using testing::AtLeast;
using testing::Mock;

namespace cc {
namespace {

class LayerTreeHostTest : public LayerTreeTest {
 public:
  LayerTreeHostTest() : contents_texture_manager_(nullptr) {}

  void DidInitializeOutputSurface() override {
    contents_texture_manager_ = layer_tree_host()->contents_texture_manager();
  }

 protected:
  PrioritizedResourceManager* contents_texture_manager_;
};

// Test if the LTHI receives ReadyToActivate notifications from the TileManager
// when no raster tasks get scheduled.
class LayerTreeHostTestReadyToActivateEmpty : public LayerTreeHostTest {
 public:
  LayerTreeHostTestReadyToActivateEmpty()
      : did_notify_ready_to_activate_(false),
        all_tiles_required_for_activation_are_ready_to_draw_(false),
        required_for_activation_count_(0) {}

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void CommitCompleteOnThread(LayerTreeHostImpl* impl) override {
    const std::vector<PictureLayerImpl*>& layers =
        impl->sync_tree()->picture_layers();
    required_for_activation_count_ = 0;
    for (const auto& layer : layers) {
      FakePictureLayerImpl* fake_layer =
          static_cast<FakePictureLayerImpl*>(layer);
      required_for_activation_count_ +=
          fake_layer->CountTilesRequiredForActivation();
    }
  }

  void NotifyReadyToActivateOnThread(LayerTreeHostImpl* impl) override {
    did_notify_ready_to_activate_ = true;
    all_tiles_required_for_activation_are_ready_to_draw_ =
        impl->tile_manager()->IsReadyToActivate();
    EndTest();
  }

  void AfterTest() override {
    EXPECT_TRUE(did_notify_ready_to_activate_);
    EXPECT_TRUE(all_tiles_required_for_activation_are_ready_to_draw_);
    EXPECT_EQ(size_t(0), required_for_activation_count_);
  }

 protected:
  bool did_notify_ready_to_activate_;
  bool all_tiles_required_for_activation_are_ready_to_draw_;
  size_t required_for_activation_count_;
};

SINGLE_AND_MULTI_THREAD_IMPL_TEST_F(LayerTreeHostTestReadyToActivateEmpty);

// Test if the LTHI receives ReadyToActivate notifications from the TileManager
// when some raster tasks flagged as REQUIRED_FOR_ACTIVATION got scheduled.
class LayerTreeHostTestReadyToActivateNonEmpty
    : public LayerTreeHostTestReadyToActivateEmpty {
 public:
  void SetupTree() override {
    client_.set_fill_with_nonsolid_color(true);
    scoped_refptr<FakePictureLayer> root_layer =
        FakePictureLayer::Create(&client_);
    root_layer->SetBounds(gfx::Size(1024, 1024));
    root_layer->SetIsDrawable(true);

    layer_tree_host()->SetRootLayer(root_layer);
    LayerTreeHostTest::SetupTree();
  }

  void AfterTest() override {
    EXPECT_TRUE(did_notify_ready_to_activate_);
    EXPECT_TRUE(all_tiles_required_for_activation_are_ready_to_draw_);
    EXPECT_LE(size_t(1), required_for_activation_count_);
  }

 private:
  FakeContentLayerClient client_;
};

// Multi-thread only because in single thread the commit goes directly to the
// active tree, so notify ready to activate is skipped.
MULTI_THREAD_IMPL_TEST_F(LayerTreeHostTestReadyToActivateNonEmpty);

// Test if the LTHI receives ReadyToDraw notifications from the TileManager when
// no raster tasks get scheduled.
class LayerTreeHostTestReadyToDrawEmpty : public LayerTreeHostTest {
 public:
  LayerTreeHostTestReadyToDrawEmpty()
      : did_notify_ready_to_draw_(false),
        all_tiles_required_for_draw_are_ready_to_draw_(false),
        required_for_draw_count_(0) {}

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void NotifyReadyToDrawOnThread(LayerTreeHostImpl* impl) override {
    did_notify_ready_to_draw_ = true;
    const std::vector<PictureLayerImpl*>& layers =
        impl->active_tree()->picture_layers();
    all_tiles_required_for_draw_are_ready_to_draw_ =
        impl->tile_manager()->IsReadyToDraw();
    for (const auto& layer : layers) {
      FakePictureLayerImpl* fake_layer =
          static_cast<FakePictureLayerImpl*>(layer);
      required_for_draw_count_ += fake_layer->CountTilesRequiredForDraw();
    }

    EndTest();
  }

  void AfterTest() override {
    EXPECT_TRUE(did_notify_ready_to_draw_);
    EXPECT_TRUE(all_tiles_required_for_draw_are_ready_to_draw_);
    EXPECT_EQ(size_t(0), required_for_draw_count_);
  }

 protected:
  bool did_notify_ready_to_draw_;
  bool all_tiles_required_for_draw_are_ready_to_draw_;
  size_t required_for_draw_count_;
};

SINGLE_AND_MULTI_THREAD_IMPL_TEST_F(LayerTreeHostTestReadyToDrawEmpty);

// Test if the LTHI receives ReadyToDraw notifications from the TileManager when
// some raster tasks flagged as REQUIRED_FOR_DRAW got scheduled.
class LayerTreeHostTestReadyToDrawNonEmpty
    : public LayerTreeHostTestReadyToDrawEmpty {
 public:
  void SetupTree() override {
    client_.set_fill_with_nonsolid_color(true);
    scoped_refptr<FakePictureLayer> root_layer =
        FakePictureLayer::Create(&client_);
    root_layer->SetBounds(gfx::Size(1024, 1024));
    root_layer->SetIsDrawable(true);

    layer_tree_host()->SetRootLayer(root_layer);
    LayerTreeHostTest::SetupTree();
  }

  void AfterTest() override {
    EXPECT_TRUE(did_notify_ready_to_draw_);
    EXPECT_TRUE(all_tiles_required_for_draw_are_ready_to_draw_);
    EXPECT_LE(size_t(1), required_for_draw_count_);
  }

 private:
  FakeContentLayerClient client_;
};

// Note: With this test setup, we only get tiles flagged as REQUIRED_FOR_DRAW in
// single threaded mode.
SINGLE_THREAD_IMPL_TEST_F(LayerTreeHostTestReadyToDrawNonEmpty);

// Two setNeedsCommits in a row should lead to at least 1 commit and at least 1
// draw with frame 0.
class LayerTreeHostTestSetNeedsCommit1 : public LayerTreeHostTest {
 public:
  LayerTreeHostTestSetNeedsCommit1() : num_commits_(0), num_draws_(0) {}

  void BeginTest() override {
    PostSetNeedsCommitToMainThread();
    PostSetNeedsCommitToMainThread();
  }

  void DrawLayersOnThread(LayerTreeHostImpl* impl) override {
    num_draws_++;
    if (!impl->active_tree()->source_frame_number())
      EndTest();
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* impl) override {
    num_commits_++;
  }

  void AfterTest() override {
    EXPECT_LE(1, num_commits_);
    EXPECT_LE(1, num_draws_);
  }

 private:
  int num_commits_;
  int num_draws_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(LayerTreeHostTestSetNeedsCommit1);

// A SetNeedsCommit should lead to 1 commit. Issuing a second commit after that
// first committed frame draws should lead to another commit.
class LayerTreeHostTestSetNeedsCommit2 : public LayerTreeHostTest {
 public:
  LayerTreeHostTestSetNeedsCommit2() : num_commits_(0), num_draws_(0) {}

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DrawLayersOnThread(LayerTreeHostImpl* impl) override { ++num_draws_; }

  void CommitCompleteOnThread(LayerTreeHostImpl* impl) override {
    ++num_commits_;
    switch (num_commits_) {
      case 1:
        PostSetNeedsCommitToMainThread();
        break;
      case 2:
        EndTest();
        break;
      default:
        NOTREACHED();
    }
  }

  void AfterTest() override {
    EXPECT_EQ(2, num_commits_);
    EXPECT_LE(1, num_draws_);
  }

 private:
  int num_commits_;
  int num_draws_;
};

MULTI_THREAD_TEST_F(LayerTreeHostTestSetNeedsCommit2);

// Verify that we pass property values in PushPropertiesTo.
class LayerTreeHostTestPushPropertiesTo : public LayerTreeHostTest {
 protected:
  void SetupTree() override {
    scoped_refptr<Layer> root = Layer::Create();
    root->CreateRenderSurface();
    root->SetBounds(gfx::Size(10, 10));
    layer_tree_host()->SetRootLayer(root);
    LayerTreeHostTest::SetupTree();
  }

  enum Properties {
    STARTUP,
    BOUNDS,
    HIDE_LAYER_AND_SUBTREE,
    DRAWS_CONTENT,
    DONE,
  };

  void BeginTest() override {
    index_ = STARTUP;
    PostSetNeedsCommitToMainThread();
  }

  void DrawLayersOnThread(LayerTreeHostImpl* impl) override {
    VerifyAfterValues(impl->active_tree()->root_layer());
  }

  void DidCommitAndDrawFrame() override {
    SetBeforeValues(layer_tree_host()->root_layer());
    VerifyBeforeValues(layer_tree_host()->root_layer());

    ++index_;
    if (index_ == DONE) {
      EndTest();
      return;
    }

    SetAfterValues(layer_tree_host()->root_layer());
  }

  void AfterTest() override {}

  void VerifyBeforeValues(Layer* layer) {
    EXPECT_EQ(gfx::Size(10, 10).ToString(), layer->bounds().ToString());
    EXPECT_FALSE(layer->hide_layer_and_subtree());
    EXPECT_FALSE(layer->DrawsContent());
  }

  void SetBeforeValues(Layer* layer) {
    layer->SetBounds(gfx::Size(10, 10));
    layer->SetHideLayerAndSubtree(false);
    layer->SetIsDrawable(false);
  }

  void VerifyAfterValues(LayerImpl* layer) {
    switch (static_cast<Properties>(index_)) {
      case STARTUP:
      case DONE:
        break;
      case BOUNDS:
        EXPECT_EQ(gfx::Size(20, 20).ToString(), layer->bounds().ToString());
        break;
      case HIDE_LAYER_AND_SUBTREE:
        EXPECT_TRUE(layer->hide_layer_and_subtree());
        break;
      case DRAWS_CONTENT:
        EXPECT_TRUE(layer->DrawsContent());
        break;
    }
  }

  void SetAfterValues(Layer* layer) {
    switch (static_cast<Properties>(index_)) {
      case STARTUP:
      case DONE:
        break;
      case BOUNDS:
        layer->SetBounds(gfx::Size(20, 20));
        break;
      case HIDE_LAYER_AND_SUBTREE:
        layer->SetHideLayerAndSubtree(true);
        break;
      case DRAWS_CONTENT:
        layer->SetIsDrawable(true);
        break;
    }
  }

  int index_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(LayerTreeHostTestPushPropertiesTo);

// 1 setNeedsRedraw after the first commit has completed should lead to 1
// additional draw.
class LayerTreeHostTestSetNeedsRedraw : public LayerTreeHostTest {
 public:
  LayerTreeHostTestSetNeedsRedraw() : num_commits_(0), num_draws_(0) {}

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DrawLayersOnThread(LayerTreeHostImpl* impl) override {
    EXPECT_EQ(0, impl->active_tree()->source_frame_number());
    if (!num_draws_) {
      // Redraw again to verify that the second redraw doesn't commit.
      PostSetNeedsRedrawToMainThread();
    } else {
      EndTest();
    }
    num_draws_++;
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* impl) override {
    EXPECT_EQ(0, num_draws_);
    num_commits_++;
  }

  void AfterTest() override {
    EXPECT_GE(2, num_draws_);
    EXPECT_EQ(1, num_commits_);
  }

 private:
  int num_commits_;
  int num_draws_;
};

MULTI_THREAD_TEST_F(LayerTreeHostTestSetNeedsRedraw);

// After setNeedsRedrawRect(invalid_rect) the final damage_rect
// must contain invalid_rect.
class LayerTreeHostTestSetNeedsRedrawRect : public LayerTreeHostTest {
 public:
  LayerTreeHostTestSetNeedsRedrawRect()
      : num_draws_(0), bounds_(50, 50), invalid_rect_(10, 10, 20, 20) {}

  void BeginTest() override {
    if (layer_tree_host()->settings().impl_side_painting)
      root_layer_ = FakePictureLayer::Create(&client_);
    else
      root_layer_ = ContentLayer::Create(&client_);
    root_layer_->SetIsDrawable(true);
    root_layer_->SetBounds(bounds_);
    layer_tree_host()->SetRootLayer(root_layer_);
    layer_tree_host()->SetViewportSize(bounds_);
    PostSetNeedsCommitToMainThread();
  }

  DrawResult PrepareToDrawOnThread(LayerTreeHostImpl* host_impl,
                                   LayerTreeHostImpl::FrameData* frame_data,
                                   DrawResult draw_result) override {
    EXPECT_EQ(DRAW_SUCCESS, draw_result);

    gfx::RectF root_damage_rect;
    if (!frame_data->render_passes.empty())
      root_damage_rect = frame_data->render_passes.back()->damage_rect;

    if (!num_draws_) {
      // If this is the first frame, expect full frame damage.
      EXPECT_EQ(root_damage_rect, gfx::Rect(bounds_));
    } else {
      // Check that invalid_rect_ is indeed repainted.
      EXPECT_TRUE(root_damage_rect.Contains(invalid_rect_));
    }

    return draw_result;
  }

  void DrawLayersOnThread(LayerTreeHostImpl* impl) override {
    if (!num_draws_) {
      PostSetNeedsRedrawRectToMainThread(invalid_rect_);
    } else {
      EndTest();
    }
    num_draws_++;
  }

  void AfterTest() override { EXPECT_EQ(2, num_draws_); }

 private:
  int num_draws_;
  const gfx::Size bounds_;
  const gfx::Rect invalid_rect_;
  FakeContentLayerClient client_;
  scoped_refptr<Layer> root_layer_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(LayerTreeHostTestSetNeedsRedrawRect);

// Ensure the texture size of the pending and active trees are identical when a
// layer is not in the viewport and a resize happens on the viewport
class LayerTreeHostTestGpuRasterDeviceSizeChanged : public LayerTreeHostTest {
 public:
  LayerTreeHostTestGpuRasterDeviceSizeChanged()
      : num_draws_(0), bounds_(500, 64), invalid_rect_(10, 10, 20, 20) {}

  void BeginTest() override {
    client_.set_fill_with_nonsolid_color(true);
    root_layer_ = FakePictureLayer::Create(&client_);
    root_layer_->SetIsDrawable(true);
    gfx::Transform transform;
    // Translate the layer out of the viewport to force it to not update its
    // tile size via PushProperties.
    transform.Translate(10000.0, 10000.0);
    root_layer_->SetTransform(transform);
    root_layer_->SetBounds(bounds_);
    layer_tree_host()->SetRootLayer(root_layer_);
    layer_tree_host()->SetViewportSize(bounds_);

    PostSetNeedsCommitToMainThread();
  }

  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->gpu_rasterization_enabled = true;
    settings->gpu_rasterization_forced = true;
  }

  void DrawLayersOnThread(LayerTreeHostImpl* impl) override {
    // Perform 2 commits.
    if (!num_draws_) {
      PostSetNeedsRedrawRectToMainThread(invalid_rect_);
    } else {
      EndTest();
    }
    num_draws_++;
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* host_impl) override {
    if (num_draws_ == 2) {
      auto pending_tree = host_impl->pending_tree();
      auto pending_layer_impl =
          static_cast<FakePictureLayerImpl*>(pending_tree->root_layer());
      EXPECT_NE(pending_layer_impl, nullptr);

      auto active_tree = host_impl->pending_tree();
      auto active_layer_impl =
          static_cast<FakePictureLayerImpl*>(active_tree->root_layer());
      EXPECT_NE(pending_layer_impl, nullptr);

      auto active_tiling_set = active_layer_impl->picture_layer_tiling_set();
      auto active_tiling = active_tiling_set->tiling_at(0);
      auto pending_tiling_set = pending_layer_impl->picture_layer_tiling_set();
      auto pending_tiling = pending_tiling_set->tiling_at(0);
      EXPECT_EQ(
          pending_tiling->TilingDataForTesting().max_texture_size().width(),
          active_tiling->TilingDataForTesting().max_texture_size().width());
    }
  }

  void DidCommitAndDrawFrame() override {
    // On the second commit, resize the viewport.
    if (num_draws_ == 1) {
      layer_tree_host()->SetViewportSize(gfx::Size(400, 64));
    }
  }

  void AfterTest() override {}

 private:
  int num_draws_;
  const gfx::Size bounds_;
  const gfx::Rect invalid_rect_;
  FakeContentLayerClient client_;
  scoped_refptr<FakePictureLayer> root_layer_;
};

SINGLE_AND_MULTI_THREAD_IMPL_TEST_F(
    LayerTreeHostTestGpuRasterDeviceSizeChanged);

class LayerTreeHostTestNoExtraCommitFromInvalidate : public LayerTreeHostTest {
 public:
  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->layer_transforms_should_scale_layer_contents = true;
  }

  void SetupTree() override {
    root_layer_ = Layer::Create();
    root_layer_->SetBounds(gfx::Size(10, 20));
    root_layer_->CreateRenderSurface();

    if (layer_tree_host()->settings().impl_side_painting)
      scaled_layer_ = FakePictureLayer::Create(&client_);
    else
      scaled_layer_ = FakeContentLayer::Create(&client_);
    scaled_layer_->SetBounds(gfx::Size(1, 1));
    root_layer_->AddChild(scaled_layer_);

    layer_tree_host()->SetRootLayer(root_layer_);
    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DrawLayersOnThread(LayerTreeHostImpl* host_impl) override {
    if (host_impl->active_tree()->source_frame_number() == 1)
      EndTest();
  }

  void DidCommit() override {
    switch (layer_tree_host()->source_frame_number()) {
      case 1:
        // SetBounds grows the layer and exposes new content.
        if (layer_tree_host()->settings().impl_side_painting) {
          scaled_layer_->SetBounds(gfx::Size(4, 4));
        } else {
          // Changing the device scale factor causes a commit. It also changes
          // the content bounds of |scaled_layer_|, which should not generate
          // a second commit as a result.
          layer_tree_host()->SetDeviceScaleFactor(4.f);
        }
        break;
      default:
        // No extra commits.
        EXPECT_EQ(2, layer_tree_host()->source_frame_number());
    }
  }

  void AfterTest() override {
    EXPECT_EQ(gfx::Size(4, 4).ToString(),
              scaled_layer_->content_bounds().ToString());
  }

 private:
  FakeContentLayerClient client_;
  scoped_refptr<Layer> root_layer_;
  scoped_refptr<Layer> scaled_layer_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(LayerTreeHostTestNoExtraCommitFromInvalidate);

class LayerTreeHostTestNoExtraCommitFromScrollbarInvalidate
    : public LayerTreeHostTest {
 public:
  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->layer_transforms_should_scale_layer_contents = true;
  }

  void SetupTree() override {
    root_layer_ = Layer::Create();
    root_layer_->SetBounds(gfx::Size(10, 20));
    root_layer_->CreateRenderSurface();

    bool paint_scrollbar = true;
    bool has_thumb = false;
    scrollbar_ = FakePaintedScrollbarLayer::Create(
        paint_scrollbar, has_thumb, root_layer_->id());
    scrollbar_->SetPosition(gfx::Point(0, 10));
    scrollbar_->SetBounds(gfx::Size(10, 10));

    root_layer_->AddChild(scrollbar_);

    layer_tree_host()->SetRootLayer(root_layer_);
    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DrawLayersOnThread(LayerTreeHostImpl* host_impl) override {
    if (host_impl->active_tree()->source_frame_number() == 1)
      EndTest();
  }

  void DidCommit() override {
    switch (layer_tree_host()->source_frame_number()) {
      case 1:
        // Changing the device scale factor causes a commit. It also changes
        // the content bounds of |scrollbar_|, which should not generate
        // a second commit as a result.
        layer_tree_host()->SetDeviceScaleFactor(4.f);
        break;
      default:
        // No extra commits.
        EXPECT_EQ(2, layer_tree_host()->source_frame_number());
    }
  }

  void AfterTest() override {
    EXPECT_EQ(gfx::Size(40, 40).ToString(),
              scrollbar_->internal_content_bounds().ToString());
  }

 private:
  FakeContentLayerClient client_;
  scoped_refptr<Layer> root_layer_;
  scoped_refptr<FakePaintedScrollbarLayer> scrollbar_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(
    LayerTreeHostTestNoExtraCommitFromScrollbarInvalidate);

class LayerTreeHostTestSetNextCommitForcesRedraw : public LayerTreeHostTest {
 public:
  LayerTreeHostTestSetNextCommitForcesRedraw()
      : num_draws_(0), bounds_(50, 50), invalid_rect_(10, 10, 20, 20) {}

  void BeginTest() override {
    if (layer_tree_host()->settings().impl_side_painting)
      root_layer_ = FakePictureLayer::Create(&client_);
    else
      root_layer_ = ContentLayer::Create(&client_);
    root_layer_->SetIsDrawable(true);
    root_layer_->SetBounds(bounds_);
    layer_tree_host()->SetRootLayer(root_layer_);
    layer_tree_host()->SetViewportSize(bounds_);
    PostSetNeedsCommitToMainThread();
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* host_impl) override {
    if (num_draws_ == 3 && host_impl->settings().impl_side_painting)
      host_impl->SetNeedsRedrawRect(invalid_rect_);
  }

  DrawResult PrepareToDrawOnThread(LayerTreeHostImpl* host_impl,
                                   LayerTreeHostImpl::FrameData* frame_data,
                                   DrawResult draw_result) override {
    EXPECT_EQ(DRAW_SUCCESS, draw_result);

    gfx::RectF root_damage_rect;
    if (!frame_data->render_passes.empty())
      root_damage_rect = frame_data->render_passes.back()->damage_rect;

    switch (num_draws_) {
      case 0:
        EXPECT_EQ(gfx::Rect(bounds_), root_damage_rect);
        break;
      case 1:
      case 2:
        EXPECT_EQ(gfx::Rect(0, 0, 0, 0), root_damage_rect);
        break;
      case 3:
        EXPECT_EQ(invalid_rect_, root_damage_rect);
        break;
      case 4:
        EXPECT_EQ(gfx::Rect(bounds_), root_damage_rect);
        break;
      default:
        NOTREACHED();
    }

    return draw_result;
  }

  void DrawLayersOnThread(LayerTreeHostImpl* host_impl) override {
    switch (num_draws_) {
      case 0:
      case 1:
        // Cycle through a couple of empty commits to ensure we're observing the
        // right behavior
        PostSetNeedsCommitToMainThread();
        break;
      case 2:
        // Should force full frame damage on the next commit
        PostSetNextCommitForcesRedrawToMainThread();
        PostSetNeedsCommitToMainThread();
        if (host_impl->settings().impl_side_painting)
          host_impl->BlockNotifyReadyToActivateForTesting(true);
        else
          num_draws_++;
        break;
      case 3:
        host_impl->BlockNotifyReadyToActivateForTesting(false);
        break;
      default:
        EndTest();
        break;
    }
    num_draws_++;
  }

  void AfterTest() override { EXPECT_EQ(5, num_draws_); }

 private:
  int num_draws_;
  const gfx::Size bounds_;
  const gfx::Rect invalid_rect_;
  FakeContentLayerClient client_;
  scoped_refptr<Layer> root_layer_;
};

SINGLE_AND_MULTI_THREAD_BLOCKNOTIFY_TEST_F(
    LayerTreeHostTestSetNextCommitForcesRedraw);

// Tests that if a layer is not drawn because of some reason in the parent then
// its damage is preserved until the next time it is drawn.
class LayerTreeHostTestUndrawnLayersDamageLater : public LayerTreeHostTest {
 public:
  LayerTreeHostTestUndrawnLayersDamageLater() {}

  void InitializeSettings(LayerTreeSettings* settings) override {
    // If we don't set the minimum contents scale, it's harder to verify whether
    // the damage we get is correct. For other scale amounts, please see
    // LayerTreeHostTestDamageWithScale.
    settings->minimum_contents_scale = 1.f;
  }

  void SetupTree() override {
    if (layer_tree_host()->settings().impl_side_painting)
      root_layer_ = FakePictureLayer::Create(&client_);
    else
      root_layer_ = ContentLayer::Create(&client_);
    root_layer_->SetIsDrawable(true);
    root_layer_->SetBounds(gfx::Size(50, 50));
    layer_tree_host()->SetRootLayer(root_layer_);

    // The initially transparent layer has a larger child layer, which is
    // not initially drawn because of the this (parent) layer.
    if (layer_tree_host()->settings().impl_side_painting)
      parent_layer_ = FakePictureLayer::Create(&client_);
    else
      parent_layer_ = FakeContentLayer::Create(&client_);
    parent_layer_->SetBounds(gfx::Size(15, 15));
    parent_layer_->SetOpacity(0.0f);
    root_layer_->AddChild(parent_layer_);

    if (layer_tree_host()->settings().impl_side_painting)
      child_layer_ = FakePictureLayer::Create(&client_);
    else
      child_layer_ = FakeContentLayer::Create(&client_);
    child_layer_->SetBounds(gfx::Size(25, 25));
    parent_layer_->AddChild(child_layer_);

    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  DrawResult PrepareToDrawOnThread(LayerTreeHostImpl* host_impl,
                                   LayerTreeHostImpl::FrameData* frame_data,
                                   DrawResult draw_result) override {
    EXPECT_EQ(DRAW_SUCCESS, draw_result);

    gfx::RectF root_damage_rect;
    if (!frame_data->render_passes.empty())
      root_damage_rect = frame_data->render_passes.back()->damage_rect;

    // The first time, the whole view needs be drawn.
    // Afterwards, just the opacity of surface_layer1 is changed a few times,
    // and each damage should be the bounding box of it and its child. If this
    // was working improperly, the damage might not include its childs bounding
    // box.
    switch (host_impl->active_tree()->source_frame_number()) {
      case 0:
        EXPECT_EQ(gfx::Rect(root_layer_->bounds()), root_damage_rect);
        break;
      case 1:
      case 2:
      case 3:
        EXPECT_EQ(gfx::Rect(child_layer_->bounds()), root_damage_rect);
        break;
      default:
        NOTREACHED();
    }

    return draw_result;
  }

  void DidCommitAndDrawFrame() override {
    switch (layer_tree_host()->source_frame_number()) {
      case 1:
        // Test not owning the surface.
        parent_layer_->SetOpacity(1.0f);
        break;
      case 2:
        parent_layer_->SetOpacity(0.0f);
        break;
      case 3:
        // Test owning the surface.
        parent_layer_->SetOpacity(0.5f);
        parent_layer_->SetForceRenderSurface(true);
        break;
      case 4:
        EndTest();
        break;
      default:
        NOTREACHED();
    }
  }

  void AfterTest() override {}

 private:
  FakeContentLayerClient client_;
  scoped_refptr<Layer> root_layer_;
  scoped_refptr<Layer> parent_layer_;
  scoped_refptr<Layer> child_layer_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(LayerTreeHostTestUndrawnLayersDamageLater);

// Tests that if a layer is not drawn because of some reason in the parent then
// its damage is preserved until the next time it is drawn.
class LayerTreeHostTestDamageWithScale : public LayerTreeHostTest {
 public:
  LayerTreeHostTestDamageWithScale() {}

  void SetupTree() override {
    client_.set_fill_with_nonsolid_color(true);

    scoped_ptr<FakePicturePile> pile(
        new FakePicturePile(ImplSidePaintingSettings().minimum_contents_scale,
                            ImplSidePaintingSettings().default_tile_grid_size));
    root_layer_ =
        FakePictureLayer::CreateWithRecordingSource(&client_, pile.Pass());
    root_layer_->SetBounds(gfx::Size(50, 50));

    pile.reset(
        new FakePicturePile(ImplSidePaintingSettings().minimum_contents_scale,
                            ImplSidePaintingSettings().default_tile_grid_size));
    child_layer_ =
        FakePictureLayer::CreateWithRecordingSource(&client_, pile.Pass());
    child_layer_->SetBounds(gfx::Size(25, 25));
    child_layer_->SetIsDrawable(true);
    child_layer_->SetContentsOpaque(true);
    root_layer_->AddChild(child_layer_);

    layer_tree_host()->SetRootLayer(root_layer_);
    LayerTreeHostTest::SetupTree();
  }

  void DidActivateTreeOnThread(LayerTreeHostImpl* host_impl) override {
    // Force the layer to have a tiling at 1.3f scale. Note that if we simply
    // add tiling, it will be gone by the time we draw because of aggressive
    // cleanup. AddTilingUntilNextDraw ensures that it remains there during
    // damage calculation.
    FakePictureLayerImpl* child_layer_impl = static_cast<FakePictureLayerImpl*>(
        host_impl->active_tree()->LayerById(child_layer_->id()));
    child_layer_impl->AddTilingUntilNextDraw(1.3f);
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  DrawResult PrepareToDrawOnThread(LayerTreeHostImpl* host_impl,
                                   LayerTreeHostImpl::FrameData* frame_data,
                                   DrawResult draw_result) override {
    EXPECT_EQ(DRAW_SUCCESS, draw_result);

    gfx::RectF root_damage_rect;
    if (!frame_data->render_passes.empty())
      root_damage_rect = frame_data->render_passes.back()->damage_rect;

    // The first time, the whole view needs be drawn.
    // Afterwards, just the opacity of surface_layer1 is changed a few times,
    // and each damage should be the bounding box of it and its child. If this
    // was working improperly, the damage might not include its childs bounding
    // box.
    switch (host_impl->active_tree()->source_frame_number()) {
      case 0:
        EXPECT_EQ(gfx::Rect(root_layer_->bounds()), root_damage_rect);
        break;
      case 1: {
        FakePictureLayerImpl* child_layer_impl =
            static_cast<FakePictureLayerImpl*>(
                host_impl->active_tree()->LayerById(child_layer_->id()));
        // We remove tilings pretty aggressively if they are not ideal. Add this
        // back in so that we can compare
        // child_layer_impl->GetEnclosingRectInTargetSpace to the damage.
        child_layer_impl->AddTilingUntilNextDraw(1.3f);

        EXPECT_EQ(gfx::Rect(26, 26), root_damage_rect);
        EXPECT_EQ(child_layer_impl->GetEnclosingRectInTargetSpace(),
                  root_damage_rect);
        EXPECT_TRUE(child_layer_impl->GetEnclosingRectInTargetSpace().Contains(
            gfx::Rect(child_layer_->bounds())));
        break;
      }
      default:
        NOTREACHED();
    }

    return draw_result;
  }

  void DidCommitAndDrawFrame() override {
    switch (layer_tree_host()->source_frame_number()) {
      case 1: {
        // Test not owning the surface.
        child_layer_->SetOpacity(0.5f);
        break;
      }
      case 2:
        EndTest();
        break;
      default:
        NOTREACHED();
    }
  }

  void AfterTest() override {}

 private:
  FakeContentLayerClient client_;
  scoped_refptr<Layer> root_layer_;
  scoped_refptr<Layer> child_layer_;
};

MULTI_THREAD_IMPL_TEST_F(LayerTreeHostTestDamageWithScale);

// Tests that if a layer is not drawn because of some reason in the parent,
// causing its content bounds to not be computed, then when it is later drawn,
// its content bounds get pushed.
class LayerTreeHostTestUndrawnLayersPushContentBoundsLater
    : public LayerTreeHostTest {
 public:
  LayerTreeHostTestUndrawnLayersPushContentBoundsLater()
      : root_layer_(Layer::Create()) {}

  void SetupTree() override {
    root_layer_->CreateRenderSurface();
    root_layer_->SetIsDrawable(true);
    root_layer_->SetBounds(gfx::Size(20, 20));
    layer_tree_host()->SetRootLayer(root_layer_);

    parent_layer_ = Layer::Create();
    parent_layer_->SetBounds(gfx::Size(20, 20));
    parent_layer_->SetOpacity(0.0f);
    root_layer_->AddChild(parent_layer_);

    child_layer_ = Layer::Create();
    child_layer_->SetBounds(gfx::Size(15, 15));
    parent_layer_->AddChild(child_layer_);

    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DidActivateTreeOnThread(LayerTreeHostImpl* host_impl) override {
    LayerImpl* root = host_impl->active_tree()->root_layer();
    LayerImpl* parent = root->children()[0];
    LayerImpl* child = parent->children()[0];

    switch (host_impl->active_tree()->source_frame_number()) {
      case 0:
        EXPECT_EQ(0.f, parent->opacity());
        EXPECT_EQ(gfx::SizeF(), child->content_bounds());
        break;
      case 1:
        EXPECT_EQ(1.f, parent->opacity());
        EXPECT_EQ(gfx::SizeF(15.f, 15.f), child->content_bounds());
        EndTest();
        break;
      default:
        NOTREACHED();
    }
  }

  void DidCommit() override {
    switch (layer_tree_host()->source_frame_number()) {
      case 1:
        parent_layer_->SetOpacity(1.0f);
        break;
      case 2:
        break;
      default:
        NOTREACHED();
    }
  }

  void AfterTest() override {}

 private:
  scoped_refptr<Layer> root_layer_;
  scoped_refptr<Layer> parent_layer_;
  scoped_refptr<Layer> child_layer_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(
    LayerTreeHostTestUndrawnLayersPushContentBoundsLater);

// This test verifies that properties on the layer tree host are commited
// to the impl side.
class LayerTreeHostTestCommit : public LayerTreeHostTest {
 public:
  LayerTreeHostTestCommit() {}

  void BeginTest() override {
    layer_tree_host()->SetViewportSize(gfx::Size(20, 20));
    layer_tree_host()->set_background_color(SK_ColorGRAY);

    PostSetNeedsCommitToMainThread();
  }

  void DidActivateTreeOnThread(LayerTreeHostImpl* impl) override {
    EXPECT_EQ(gfx::Size(20, 20), impl->DrawViewportSize());
    EXPECT_EQ(SK_ColorGRAY, impl->active_tree()->background_color());

    EndTest();
  }

  void AfterTest() override {}
};

MULTI_THREAD_TEST_F(LayerTreeHostTestCommit);

// This test verifies that LayerTreeHostImpl's current frame time gets
// updated in consecutive frames when it doesn't draw due to tree
// activation failure.
class LayerTreeHostTestFrameTimeUpdatesAfterActivationFails
    : public LayerTreeHostTest {
 public:
  LayerTreeHostTestFrameTimeUpdatesAfterActivationFails()
      : frame_count_with_pending_tree_(0) {}

  void BeginTest() override {
    layer_tree_host()->SetViewportSize(gfx::Size(20, 20));
    layer_tree_host()->set_background_color(SK_ColorGRAY);

    PostSetNeedsCommitToMainThread();
  }

  void BeginCommitOnThread(LayerTreeHostImpl* impl) override {
    EXPECT_EQ(frame_count_with_pending_tree_, 0);
    if (impl->settings().impl_side_painting)
      impl->BlockNotifyReadyToActivateForTesting(true);
  }

  void WillBeginImplFrameOnThread(LayerTreeHostImpl* impl,
                                  const BeginFrameArgs& args) override {
    if (impl->pending_tree())
      frame_count_with_pending_tree_++;

    if (frame_count_with_pending_tree_ == 1) {
      EXPECT_EQ(first_frame_time_.ToInternalValue(), 0);
      first_frame_time_ = impl->CurrentBeginFrameArgs().frame_time;
    } else if (frame_count_with_pending_tree_ == 2 &&
               impl->settings().impl_side_painting) {
      impl->BlockNotifyReadyToActivateForTesting(false);
    }
  }

  void DrawLayersOnThread(LayerTreeHostImpl* impl) override {
    if (frame_count_with_pending_tree_ > 1) {
      EXPECT_NE(first_frame_time_.ToInternalValue(), 0);
      EXPECT_NE(first_frame_time_.ToInternalValue(),
                impl->CurrentBeginFrameArgs().frame_time.ToInternalValue());
      EndTest();
      return;
    }

    EXPECT_FALSE(impl->settings().impl_side_painting);
    EndTest();
  }
  void DidActivateTreeOnThread(LayerTreeHostImpl* impl) override {
    if (impl->settings().impl_side_painting)
      EXPECT_NE(frame_count_with_pending_tree_, 1);
  }

  void AfterTest() override {}

 private:
  int frame_count_with_pending_tree_;
  base::TimeTicks first_frame_time_;
};

SINGLE_AND_MULTI_THREAD_BLOCKNOTIFY_TEST_F(
    LayerTreeHostTestFrameTimeUpdatesAfterActivationFails);

// This test verifies that LayerTreeHostImpl's current frame time gets
// updated in consecutive frames when it draws in each frame.
class LayerTreeHostTestFrameTimeUpdatesAfterDraw : public LayerTreeHostTest {
 public:
  LayerTreeHostTestFrameTimeUpdatesAfterDraw() : frame_(0) {}

  void BeginTest() override {
    layer_tree_host()->SetViewportSize(gfx::Size(20, 20));
    layer_tree_host()->set_background_color(SK_ColorGRAY);

    PostSetNeedsCommitToMainThread();
  }

  void DrawLayersOnThread(LayerTreeHostImpl* impl) override {
    frame_++;
    if (frame_ == 1) {
      first_frame_time_ = impl->CurrentBeginFrameArgs().frame_time;
      impl->SetNeedsRedraw();

      // Since we might use a low-resolution clock on Windows, we need to
      // make sure that the clock has incremented past first_frame_time_.
      while (first_frame_time_ == gfx::FrameTime::Now()) {
      }

      return;
    }

    EXPECT_NE(first_frame_time_, impl->CurrentBeginFrameArgs().frame_time);
    EndTest();
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* host_impl) override {
    // Ensure there isn't a commit between the two draws, to ensure that a
    // commit isn't required for updating the current frame time. We can
    // only check for this in the multi-threaded case, since in the single-
    // threaded case there will always be a commit between consecutive draws.
    if (HasImplThread())
      EXPECT_EQ(0, frame_);
  }

  void AfterTest() override {}

 private:
  int frame_;
  base::TimeTicks first_frame_time_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(LayerTreeHostTestFrameTimeUpdatesAfterDraw);

// Verifies that StartPageScaleAnimation events propagate correctly
// from LayerTreeHost to LayerTreeHostImpl in the MT compositor.
class LayerTreeHostTestStartPageScaleAnimation : public LayerTreeHostTest {
 public:
  LayerTreeHostTestStartPageScaleAnimation() {}

  void SetupTree() override {
    LayerTreeHostTest::SetupTree();

    if (layer_tree_host()->settings().impl_side_painting) {
      scoped_refptr<FakePictureLayer> layer =
          FakePictureLayer::Create(&client_);
      layer->set_always_update_resources(true);
      scroll_layer_ = layer;
    } else {
      scroll_layer_ = FakeContentLayer::Create(&client_);
    }

    Layer* root_layer = layer_tree_host()->root_layer();
    scroll_layer_->SetScrollClipLayerId(root_layer->id());
    scroll_layer_->SetIsContainerForFixedPositionLayers(true);
    scroll_layer_->SetBounds(gfx::Size(2 * root_layer->bounds().width(),
                                       2 * root_layer->bounds().height()));
    scroll_layer_->SetScrollOffset(gfx::ScrollOffset());
    layer_tree_host()->root_layer()->AddChild(scroll_layer_);
    // This test requires the page_scale and inner viewport layers to be
    // identified.
    layer_tree_host()->RegisterViewportLayers(NULL, root_layer,
                                              scroll_layer_.get(), NULL);
    layer_tree_host()->SetPageScaleFactorAndLimits(1.f, 0.5f, 2.f);
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void ApplyViewportDeltas(const gfx::Vector2d& scroll_delta,
                           float scale,
                           float) override {
    gfx::ScrollOffset offset = scroll_layer_->scroll_offset();
    scroll_layer_->SetScrollOffset(ScrollOffsetWithDelta(offset,
                                                         scroll_delta));
    layer_tree_host()->SetPageScaleFactorAndLimits(scale, 0.5f, 2.f);
  }

  void DidActivateTreeOnThread(LayerTreeHostImpl* impl) override {
    // We get one commit before the first draw, and the animation doesn't happen
    // until the second draw.
    switch (impl->active_tree()->source_frame_number()) {
      case 0:
        EXPECT_EQ(1.f, impl->active_tree()->current_page_scale_factor());
        // We'll start an animation when we get back to the main thread.
        break;
      case 1:
        EXPECT_EQ(1.f, impl->active_tree()->current_page_scale_factor());
        break;
      case 2:
        EXPECT_EQ(1.25f, impl->active_tree()->current_page_scale_factor());
        EndTest();
        break;
      default:
        NOTREACHED();
    }
  }

  void DidCommitAndDrawFrame() override {
    switch (layer_tree_host()->source_frame_number()) {
      case 1:
        layer_tree_host()->StartPageScaleAnimation(
            gfx::Vector2d(), false, 1.25f, base::TimeDelta());
        break;
    }
  }

  void AfterTest() override {}

  FakeContentLayerClient client_;
  scoped_refptr<Layer> scroll_layer_;
};

MULTI_THREAD_TEST_F(LayerTreeHostTestStartPageScaleAnimation);

class LayerTreeHostTestSetVisible : public LayerTreeHostTest {
 public:
  LayerTreeHostTestSetVisible() : num_draws_(0) {}

  void BeginTest() override {
    PostSetNeedsCommitToMainThread();
    PostSetVisibleToMainThread(false);
    // This is suppressed while we're invisible.
    PostSetNeedsRedrawToMainThread();
    // Triggers the redraw.
    PostSetVisibleToMainThread(true);
  }

  void DrawLayersOnThread(LayerTreeHostImpl* impl) override {
    EXPECT_TRUE(impl->visible());
    ++num_draws_;
    EndTest();
  }

  void AfterTest() override { EXPECT_EQ(1, num_draws_); }

 private:
  int num_draws_;
};

MULTI_THREAD_TEST_F(LayerTreeHostTestSetVisible);

class TestOpacityChangeLayerDelegate : public ContentLayerClient {
 public:
  TestOpacityChangeLayerDelegate() : test_layer_(0) {}

  void SetTestLayer(Layer* test_layer) { test_layer_ = test_layer; }

  void PaintContents(SkCanvas* canvas,
                     const gfx::Rect& clip,
                     PaintingControlSetting picture_control) override {
    // Set layer opacity to 0.
    if (test_layer_)
      test_layer_->SetOpacity(0.f);
  }
  scoped_refptr<DisplayItemList> PaintContentsToDisplayList(
      const gfx::Rect& clip,
      PaintingControlSetting picture_control) override {
    NOTIMPLEMENTED();
    return DisplayItemList::Create();
  }
  bool FillsBoundsCompletely() const override { return false; }

 private:
  Layer* test_layer_;
};

class ContentLayerWithUpdateTracking : public ContentLayer {
 public:
  static scoped_refptr<ContentLayerWithUpdateTracking> Create(
      ContentLayerClient* client) {
    return make_scoped_refptr(new ContentLayerWithUpdateTracking(client));
  }

  int PaintContentsCount() { return paint_contents_count_; }
  void ResetPaintContentsCount() { paint_contents_count_ = 0; }

  bool Update(ResourceUpdateQueue* queue,
              const OcclusionTracker<Layer>* occlusion) override {
    bool updated = ContentLayer::Update(queue, occlusion);
    paint_contents_count_++;
    return updated;
  }

 private:
  explicit ContentLayerWithUpdateTracking(ContentLayerClient* client)
      : ContentLayer(client), paint_contents_count_(0) {
    SetBounds(gfx::Size(10, 10));
    SetIsDrawable(true);
  }
  ~ContentLayerWithUpdateTracking() override {}

  int paint_contents_count_;
};

// Layer opacity change during paint should not prevent compositor resources
// from being updated during commit.
class LayerTreeHostTestOpacityChange : public LayerTreeHostTest {
 public:
  LayerTreeHostTestOpacityChange() : test_opacity_change_delegate_() {}

  void BeginTest() override {
    if (layer_tree_host()->settings().impl_side_painting) {
      update_check_picture_layer_ =
          FakePictureLayer::Create(&test_opacity_change_delegate_);
      test_opacity_change_delegate_.SetTestLayer(
          update_check_picture_layer_.get());
      is_impl_paint_ = true;
    } else {
      update_check_content_layer_ = ContentLayerWithUpdateTracking::Create(
          &test_opacity_change_delegate_);
      test_opacity_change_delegate_.SetTestLayer(
          update_check_content_layer_.get());
      is_impl_paint_ = false;
    }
    layer_tree_host()->SetViewportSize(gfx::Size(10, 10));
    if (layer_tree_host()->settings().impl_side_painting)
      layer_tree_host()->root_layer()->AddChild(update_check_picture_layer_);
    else
      layer_tree_host()->root_layer()->AddChild(update_check_content_layer_);

    PostSetNeedsCommitToMainThread();
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* impl) override { EndTest(); }

  void AfterTest() override {
    // Update() should have been called once.
    if (is_impl_paint_)
      EXPECT_EQ(1u, update_check_picture_layer_->update_count());
    else
      EXPECT_EQ(1, update_check_content_layer_->PaintContentsCount());
  }

 private:
  TestOpacityChangeLayerDelegate test_opacity_change_delegate_;
  scoped_refptr<ContentLayerWithUpdateTracking> update_check_content_layer_;
  scoped_refptr<FakePictureLayer> update_check_picture_layer_;
  bool is_impl_paint_;
};

MULTI_THREAD_TEST_F(LayerTreeHostTestOpacityChange);

class LayerTreeHostTestDeviceScaleFactorScalesViewportAndLayers
    : public LayerTreeHostTest {
 public:
  LayerTreeHostTestDeviceScaleFactorScalesViewportAndLayers() {}

  void InitializeSettings(LayerTreeSettings* settings) override {
    // PictureLayer can only be used with impl side painting enabled.
    settings->impl_side_painting = true;
  }

  void BeginTest() override {
    client_.set_fill_with_nonsolid_color(true);
    root_layer_ = FakePictureLayer::Create(&client_);
    child_layer_ = FakePictureLayer::Create(&client_);

    layer_tree_host()->SetViewportSize(gfx::Size(60, 60));
    layer_tree_host()->SetDeviceScaleFactor(1.5);
    EXPECT_EQ(gfx::Size(60, 60), layer_tree_host()->device_viewport_size());

    root_layer_->AddChild(child_layer_);

    root_layer_->SetIsDrawable(true);
    root_layer_->SetBounds(gfx::Size(30, 30));

    child_layer_->SetIsDrawable(true);
    child_layer_->SetPosition(gfx::Point(2, 2));
    child_layer_->SetBounds(gfx::Size(10, 10));

    layer_tree_host()->SetRootLayer(root_layer_);

    PostSetNeedsCommitToMainThread();
  }

  void DidActivateTreeOnThread(LayerTreeHostImpl* impl) override {
    // Should only do one commit.
    EXPECT_EQ(0, impl->active_tree()->source_frame_number());
    // Device scale factor should come over to impl.
    EXPECT_NEAR(impl->device_scale_factor(), 1.5f, 0.00001f);

    // Both layers are on impl.
    ASSERT_EQ(1u, impl->active_tree()->root_layer()->children().size());

    // Device viewport is scaled.
    EXPECT_EQ(gfx::Size(60, 60), impl->DrawViewportSize());

    FakePictureLayerImpl* root =
        static_cast<FakePictureLayerImpl*>(impl->active_tree()->root_layer());
    FakePictureLayerImpl* child = static_cast<FakePictureLayerImpl*>(
        impl->active_tree()->root_layer()->children()[0]);

    // Positions remain in layout pixels.
    EXPECT_EQ(gfx::Point(0, 0), root->position());
    EXPECT_EQ(gfx::Point(2, 2), child->position());

    // Compute all the layer transforms for the frame.
    LayerTreeHostImpl::FrameData frame_data;
    impl->PrepareToDraw(&frame_data);
    impl->DidDrawAllLayers(frame_data);

    const LayerImplList& render_surface_layer_list =
        *frame_data.render_surface_layer_list;

    // Both layers should be drawing into the root render surface.
    ASSERT_EQ(1u, render_surface_layer_list.size());
    ASSERT_EQ(root->render_surface(),
              render_surface_layer_list[0]->render_surface());
    ASSERT_EQ(2u, root->render_surface()->layer_list().size());

    // The root render surface is the size of the viewport.
    EXPECT_EQ(gfx::Rect(0, 0, 60, 60), root->render_surface()->content_rect());

    // The max tiling scale of the child should be scaled.
    EXPECT_FLOAT_EQ(1.5f, child->MaximumTilingContentsScale());

    gfx::Transform scale_transform;
    scale_transform.Scale(impl->device_scale_factor(),
                          impl->device_scale_factor());

    // The root layer is scaled by 2x.
    gfx::Transform root_screen_space_transform = scale_transform;
    gfx::Transform root_draw_transform = scale_transform;

    EXPECT_EQ(root_draw_transform, root->draw_transform());
    EXPECT_EQ(root_screen_space_transform, root->screen_space_transform());

    // The child is at position 2,2, which is transformed to 3,3 after the scale
    gfx::Transform child_transform;
    child_transform.Translate(3.f, 3.f);
    child_transform.Scale(child->MaximumTilingContentsScale(),
                          child->MaximumTilingContentsScale());

    EXPECT_TRANSFORMATION_MATRIX_EQ(child_transform, child->draw_transform());
    EXPECT_TRANSFORMATION_MATRIX_EQ(child_transform,
                                    child->screen_space_transform());

    EndTest();
  }

  void AfterTest() override {}

 private:
  FakeContentLayerClient client_;
  scoped_refptr<FakePictureLayer> root_layer_;
  scoped_refptr<FakePictureLayer> child_layer_;
};

MULTI_THREAD_TEST_F(LayerTreeHostTestDeviceScaleFactorScalesViewportAndLayers);

// TODO(sohanjg) : Remove it once impl-side painting ships everywhere.
// Verify atomicity of commits and reuse of textures.
class LayerTreeHostTestDirectRendererAtomicCommit : public LayerTreeHostTest {
 public:
  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->renderer_settings.texture_id_allocation_chunk_size = 1;
    // Make sure partial texture updates are turned off.
    settings->max_partial_texture_updates = 0;
    // Linear fade animator prevents scrollbars from drawing immediately.
    settings->scrollbar_animator = LayerTreeSettings::NO_ANIMATOR;
  }

  void SetupTree() override {
    layer_ = FakeContentLayer::Create(&client_);
    layer_->SetBounds(gfx::Size(10, 20));

    bool paint_scrollbar = true;
    bool has_thumb = false;
    scrollbar_ = FakePaintedScrollbarLayer::Create(
        paint_scrollbar, has_thumb, layer_->id());
    scrollbar_->SetPosition(gfx::Point(0, 10));
    scrollbar_->SetBounds(gfx::Size(10, 10));

    layer_->AddChild(scrollbar_);

    layer_tree_host()->SetRootLayer(layer_);
    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override {
    drew_frame_ = -1;
    PostSetNeedsCommitToMainThread();
  }

  void DidActivateTreeOnThread(LayerTreeHostImpl* impl) override {
    ASSERT_EQ(0u, impl->settings().max_partial_texture_updates);

    TestWebGraphicsContext3D* context = TestContext();

    switch (impl->active_tree()->source_frame_number()) {
      case 0:
        // Number of textures should be one for each layer
        ASSERT_EQ(2u, context->NumTextures());
        // Number of textures used for commit should be one for each layer.
        EXPECT_EQ(2u, context->NumUsedTextures());
        // Verify that used texture is correct.
        EXPECT_TRUE(context->UsedTexture(context->TextureAt(0)));
        EXPECT_TRUE(context->UsedTexture(context->TextureAt(1)));

        context->ResetUsedTextures();
        break;
      case 1:
        // Number of textures should be one for scrollbar layer since it was
        // requested and deleted on the impl-thread, and double for the content
        // layer since its first texture is used by impl thread and cannot by
        // used for update.
        ASSERT_EQ(3u, context->NumTextures());
        // Number of textures used for commit should be one for each layer.
        EXPECT_EQ(2u, context->NumUsedTextures());
        // First textures should not have been used.
        EXPECT_FALSE(context->UsedTexture(context->TextureAt(0)));
        EXPECT_TRUE(context->UsedTexture(context->TextureAt(1)));
        // New textures should have been used.
        EXPECT_TRUE(context->UsedTexture(context->TextureAt(2)));
        context->ResetUsedTextures();
        break;
      case 2:
        EndTest();
        break;
      default:
        NOTREACHED();
        break;
    }
  }

  void DrawLayersOnThread(LayerTreeHostImpl* impl) override {
    TestWebGraphicsContext3D* context = TestContext();

    if (drew_frame_ == impl->active_tree()->source_frame_number()) {
      EXPECT_EQ(0u, context->NumUsedTextures()) << "For frame " << drew_frame_;
      return;
    }
    drew_frame_ = impl->active_tree()->source_frame_number();

    // We draw/ship one texture each frame for each layer.
    EXPECT_EQ(2u, context->NumUsedTextures());
    context->ResetUsedTextures();

    if (!TestEnded())
      PostSetNeedsCommitToMainThread();
  }

  void Layout() override {
    layer_->SetNeedsDisplay();
    scrollbar_->SetNeedsDisplay();
  }

  void AfterTest() override {}

 protected:
  FakeContentLayerClient client_;
  scoped_refptr<FakeContentLayer> layer_;
  scoped_refptr<FakePaintedScrollbarLayer> scrollbar_;
  int drew_frame_;
};

MULTI_THREAD_DIRECT_RENDERER_NOIMPL_TEST_F(
    LayerTreeHostTestDirectRendererAtomicCommit);

// TODO(sohanjg) : Remove it once impl-side painting ships everywhere.
class LayerTreeHostTestDelegatingRendererAtomicCommit
    : public LayerTreeHostTestDirectRendererAtomicCommit {
 public:
  void DidActivateTreeOnThread(LayerTreeHostImpl* impl) override {
    ASSERT_EQ(0u, impl->settings().max_partial_texture_updates);

    TestWebGraphicsContext3D* context = TestContext();

    switch (impl->active_tree()->source_frame_number()) {
      case 0:
        // Number of textures should be one for each layer
        ASSERT_EQ(2u, context->NumTextures());
        // Number of textures used for commit should be one for each layer.
        EXPECT_EQ(2u, context->NumUsedTextures());
        // Verify that used texture is correct.
        EXPECT_TRUE(context->UsedTexture(context->TextureAt(0)));
        EXPECT_TRUE(context->UsedTexture(context->TextureAt(1)));
        break;
      case 1:
        // Number of textures should be doubled as the first context layer
        // texture is being used by the impl-thread and cannot be used for
        // update.  The scrollbar behavior is different direct renderer because
        // UI resource deletion with delegating renderer occurs after tree
        // activation.
        ASSERT_EQ(4u, context->NumTextures());
        // Number of textures used for commit should still be
        // one for each layer.
        EXPECT_EQ(2u, context->NumUsedTextures());
        // First textures should not have been used.
        EXPECT_FALSE(context->UsedTexture(context->TextureAt(0)));
        EXPECT_FALSE(context->UsedTexture(context->TextureAt(1)));
        // New textures should have been used.
        EXPECT_TRUE(context->UsedTexture(context->TextureAt(2)));
        EXPECT_TRUE(context->UsedTexture(context->TextureAt(3)));
        break;
      case 2:
        EndTest();
        break;
      default:
        NOTREACHED();
        break;
    }
  }
};

MULTI_THREAD_DELEGATING_RENDERER_NOIMPL_TEST_F(
    LayerTreeHostTestDelegatingRendererAtomicCommit);

static void SetLayerPropertiesForTesting(Layer* layer,
                                         Layer* parent,
                                         const gfx::Transform& transform,
                                         const gfx::Point3F& transform_origin,
                                         const gfx::PointF& position,
                                         const gfx::Size& bounds,
                                         bool opaque) {
  layer->RemoveAllChildren();
  if (parent)
    parent->AddChild(layer);
  layer->SetTransform(transform);
  layer->SetTransformOrigin(transform_origin);
  layer->SetPosition(position);
  layer->SetBounds(bounds);
  layer->SetContentsOpaque(opaque);
}

// TODO(sohanjg) : Remove it once impl-side painting ships everywhere.
class LayerTreeHostTestAtomicCommitWithPartialUpdate
    : public LayerTreeHostTest {
 public:
  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->renderer_settings.texture_id_allocation_chunk_size = 1;
    // Allow one partial texture update.
    settings->max_partial_texture_updates = 1;
    // No partial updates when impl side painting is enabled.
    settings->impl_side_painting = false;
  }

  void SetupTree() override {
    parent_ = FakeContentLayer::Create(&client_);
    parent_->SetBounds(gfx::Size(10, 20));

    child_ = FakeContentLayer::Create(&client_);
    child_->SetPosition(gfx::Point(0, 10));
    child_->SetBounds(gfx::Size(3, 10));

    parent_->AddChild(child_);

    layer_tree_host()->SetRootLayer(parent_);
    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DidCommitAndDrawFrame() override {
    switch (layer_tree_host()->source_frame_number()) {
      case 1:
        parent_->SetNeedsDisplay();
        child_->SetNeedsDisplay();
        break;
      case 2:
        // Damage part of layers.
        parent_->SetNeedsDisplayRect(gfx::Rect(5, 5));
        child_->SetNeedsDisplayRect(gfx::Rect(5, 5));
        break;
      case 3:
        child_->SetNeedsDisplay();
        layer_tree_host()->SetViewportSize(gfx::Size(10, 10));
        break;
      case 4:
        layer_tree_host()->SetViewportSize(gfx::Size(10, 20));
        break;
      case 5:
        EndTest();
        break;
      default:
        NOTREACHED() << layer_tree_host()->source_frame_number();
        break;
    }
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* impl) override {
    ASSERT_EQ(1u, impl->settings().max_partial_texture_updates);

    TestWebGraphicsContext3D* context = TestContext();

    switch (impl->active_tree()->source_frame_number()) {
      case 0:
        // Number of textures should be one for each layer.
        ASSERT_EQ(2u, context->NumTextures());
        // Number of textures used for commit should be one for each layer.
        EXPECT_EQ(2u, context->NumUsedTextures());
        // Verify that used textures are correct.
        EXPECT_TRUE(context->UsedTexture(context->TextureAt(0)));
        EXPECT_TRUE(context->UsedTexture(context->TextureAt(1)));
        context->ResetUsedTextures();
        break;
      case 1:
        if (HasImplThread()) {
          // Number of textures should be two for each content layer.
          ASSERT_EQ(4u, context->NumTextures());
        } else {
          // In single thread we can always do partial updates, so the limit has
          // no effect.
          ASSERT_EQ(2u, context->NumTextures());
        }
        // Number of textures used for commit should be one for each content
        // layer.
        EXPECT_EQ(2u, context->NumUsedTextures());

        if (HasImplThread()) {
          // First content textures should not have been used.
          EXPECT_FALSE(context->UsedTexture(context->TextureAt(0)));
          EXPECT_FALSE(context->UsedTexture(context->TextureAt(1)));
          // New textures should have been used.
          EXPECT_TRUE(context->UsedTexture(context->TextureAt(2)));
          EXPECT_TRUE(context->UsedTexture(context->TextureAt(3)));
        } else {
          // In single thread we can always do partial updates, so the limit has
          // no effect.
          EXPECT_TRUE(context->UsedTexture(context->TextureAt(0)));
          EXPECT_TRUE(context->UsedTexture(context->TextureAt(1)));
        }

        context->ResetUsedTextures();
        break;
      case 2:
        if (HasImplThread()) {
          // Number of textures should be two for each content layer.
          ASSERT_EQ(4u, context->NumTextures());
        } else {
          // In single thread we can always do partial updates, so the limit has
          // no effect.
          ASSERT_EQ(2u, context->NumTextures());
        }
        // Number of textures used for commit should be one for each content
        // layer.
        EXPECT_EQ(2u, context->NumUsedTextures());

        if (HasImplThread()) {
          // One content layer does a partial update also.
          EXPECT_TRUE(context->UsedTexture(context->TextureAt(2)));
          EXPECT_FALSE(context->UsedTexture(context->TextureAt(3)));
        } else {
          // In single thread we can always do partial updates, so the limit has
          // no effect.
          EXPECT_TRUE(context->UsedTexture(context->TextureAt(0)));
          EXPECT_TRUE(context->UsedTexture(context->TextureAt(1)));
        }

        context->ResetUsedTextures();
        break;
      case 3:
        // No textures should be used for commit.
        EXPECT_EQ(0u, context->NumUsedTextures());

        context->ResetUsedTextures();
        break;
      case 4:
        // Number of textures used for commit should be one, for the
        // content layer.
        EXPECT_EQ(1u, context->NumUsedTextures());

        context->ResetUsedTextures();
        break;
      default:
        NOTREACHED();
        break;
    }
  }

  void DrawLayersOnThread(LayerTreeHostImpl* impl) override {
    EXPECT_LT(impl->active_tree()->source_frame_number(), 5);

    TestWebGraphicsContext3D* context = TestContext();

    // Number of textures used for drawing should one per layer except for
    // frame 3 where the viewport only contains one layer.
    if (impl->active_tree()->source_frame_number() == 3) {
      EXPECT_EQ(1u, context->NumUsedTextures());
    } else {
      EXPECT_EQ(2u, context->NumUsedTextures())
          << "For frame " << impl->active_tree()->source_frame_number();
    }

    context->ResetUsedTextures();
  }

  void AfterTest() override {}

 private:
  FakeContentLayerClient client_;
  scoped_refptr<FakeContentLayer> parent_;
  scoped_refptr<FakeContentLayer> child_;
};

// Partial updates are not possible with a delegating renderer.
SINGLE_AND_MULTI_THREAD_DIRECT_RENDERER_TEST_F(
    LayerTreeHostTestAtomicCommitWithPartialUpdate);

// TODO(sohanjg) : Make it work with impl-side painting.
class LayerTreeHostTestSurfaceNotAllocatedForLayersOutsideMemoryLimit
    : public LayerTreeHostTest {
 protected:
  void SetupTree() override {
    root_layer_ = FakeContentLayer::Create(&client_);
    root_layer_->SetBounds(gfx::Size(100, 100));

    surface_layer1_ = FakeContentLayer::Create(&client_);
    surface_layer1_->SetBounds(gfx::Size(100, 100));
    surface_layer1_->SetForceRenderSurface(true);
    surface_layer1_->SetOpacity(0.5f);
    root_layer_->AddChild(surface_layer1_);

    surface_layer2_ = FakeContentLayer::Create(&client_);
    surface_layer2_->SetBounds(gfx::Size(100, 100));
    surface_layer2_->SetForceRenderSurface(true);
    surface_layer2_->SetOpacity(0.5f);
    surface_layer1_->AddChild(surface_layer2_);

    replica_layer1_ = FakeContentLayer::Create(&client_);
    surface_layer1_->SetReplicaLayer(replica_layer1_.get());

    replica_layer2_ = FakeContentLayer::Create(&client_);
    surface_layer2_->SetReplicaLayer(replica_layer2_.get());

    layer_tree_host()->SetRootLayer(root_layer_);
    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DrawLayersOnThread(LayerTreeHostImpl* host_impl) override {
    Renderer* renderer = host_impl->renderer();
    RenderPassId surface1_render_pass_id = host_impl->active_tree()
                                               ->root_layer()
                                               ->children()[0]
                                               ->render_surface()
                                               ->GetRenderPassId();
    RenderPassId surface2_render_pass_id = host_impl->active_tree()
                                               ->root_layer()
                                               ->children()[0]
                                               ->children()[0]
                                               ->render_surface()
                                               ->GetRenderPassId();

    switch (host_impl->active_tree()->source_frame_number()) {
      case 0:
        EXPECT_TRUE(
            renderer->HasAllocatedResourcesForTesting(surface1_render_pass_id));
        EXPECT_TRUE(
            renderer->HasAllocatedResourcesForTesting(surface2_render_pass_id));

        // Reduce the memory limit to only fit the root layer and one render
        // surface. This prevents any contents drawing into surfaces
        // from being allocated.
        host_impl->SetMemoryPolicy(ManagedMemoryPolicy(100 * 100 * 4 * 2));
        break;
      case 1:
        EXPECT_FALSE(
            renderer->HasAllocatedResourcesForTesting(surface1_render_pass_id));
        EXPECT_FALSE(
            renderer->HasAllocatedResourcesForTesting(surface2_render_pass_id));

        EndTest();
        break;
    }
  }

  void DidCommitAndDrawFrame() override {
    if (layer_tree_host()->source_frame_number() < 2)
      root_layer_->SetNeedsDisplay();
  }

  void AfterTest() override {
    EXPECT_LE(2u, root_layer_->update_count());
    EXPECT_LE(2u, surface_layer1_->update_count());
    EXPECT_LE(2u, surface_layer2_->update_count());
  }

  FakeContentLayerClient client_;
  scoped_refptr<FakeContentLayer> root_layer_;
  scoped_refptr<FakeContentLayer> surface_layer1_;
  scoped_refptr<FakeContentLayer> replica_layer1_;
  scoped_refptr<FakeContentLayer> surface_layer2_;
  scoped_refptr<FakeContentLayer> replica_layer2_;
};

// Surfaces don't exist with a delegated renderer.
SINGLE_AND_MULTI_THREAD_DIRECT_RENDERER_NOIMPL_TEST_F(
    LayerTreeHostTestSurfaceNotAllocatedForLayersOutsideMemoryLimit);

class EvictionTestLayer : public Layer {
 public:
  static scoped_refptr<EvictionTestLayer> Create() {
    return make_scoped_refptr(new EvictionTestLayer());
  }

  bool Update(ResourceUpdateQueue*, const OcclusionTracker<Layer>*) override;
  bool DrawsContent() const override { return true; }

  scoped_ptr<LayerImpl> CreateLayerImpl(LayerTreeImpl* tree_impl) override;
  void PushPropertiesTo(LayerImpl* impl) override;
  void SetTexturePriorities(const PriorityCalculator&) override;

  bool HaveBackingTexture() const {
    return texture_.get() ? texture_->have_backing_texture() : false;
  }

 private:
  EvictionTestLayer() : Layer() {}
  ~EvictionTestLayer() override {}

  void CreateTextureIfNeeded() {
    if (texture_)
      return;
    texture_ = PrioritizedResource::Create(
        layer_tree_host()->contents_texture_manager());
    texture_->SetDimensions(gfx::Size(10, 10), RGBA_8888);
    bitmap_.allocN32Pixels(10, 10);
  }

  scoped_ptr<PrioritizedResource> texture_;
  SkBitmap bitmap_;
};

class EvictionTestLayerImpl : public LayerImpl {
 public:
  static scoped_ptr<EvictionTestLayerImpl> Create(LayerTreeImpl* tree_impl,
                                                  int id) {
    return make_scoped_ptr(new EvictionTestLayerImpl(tree_impl, id));
  }
  ~EvictionTestLayerImpl() override {}

  void AppendQuads(RenderPass* render_pass,
                   AppendQuadsData* append_quads_data) override {
    ASSERT_TRUE(has_texture_);
    ASSERT_NE(0u, layer_tree_impl()->resource_provider()->num_resources());
  }

  void SetHasTexture(bool has_texture) { has_texture_ = has_texture; }

 private:
  EvictionTestLayerImpl(LayerTreeImpl* tree_impl, int id)
      : LayerImpl(tree_impl, id), has_texture_(false) {}

  bool has_texture_;
};

void EvictionTestLayer::SetTexturePriorities(const PriorityCalculator&) {
  CreateTextureIfNeeded();
  if (!texture_)
    return;
  texture_->set_request_priority(PriorityCalculator::UIPriority(true));
}

bool EvictionTestLayer::Update(ResourceUpdateQueue* queue,
                               const OcclusionTracker<Layer>* occlusion) {
  CreateTextureIfNeeded();
  if (!texture_)
    return false;

  gfx::Rect full_rect(0, 0, 10, 10);
  ResourceUpdate upload = ResourceUpdate::Create(
      texture_.get(), &bitmap_, full_rect, full_rect, gfx::Vector2d());
  queue->AppendFullUpload(upload);
  return true;
}

scoped_ptr<LayerImpl> EvictionTestLayer::CreateLayerImpl(
    LayerTreeImpl* tree_impl) {
  return EvictionTestLayerImpl::Create(tree_impl, layer_id_);
}

void EvictionTestLayer::PushPropertiesTo(LayerImpl* layer_impl) {
  Layer::PushPropertiesTo(layer_impl);

  EvictionTestLayerImpl* test_layer_impl =
      static_cast<EvictionTestLayerImpl*>(layer_impl);
  test_layer_impl->SetHasTexture(texture_->have_backing_texture());
}

class LayerTreeHostTestEvictTextures : public LayerTreeHostTest {
 public:
  LayerTreeHostTestEvictTextures()
      : layer_(EvictionTestLayer::Create()),
        impl_for_evict_textures_(0),
        num_commits_(0) {}

  void BeginTest() override {
    layer_tree_host()->SetRootLayer(layer_);
    layer_tree_host()->SetViewportSize(gfx::Size(10, 20));

    gfx::Transform identity_matrix;
    SetLayerPropertiesForTesting(layer_.get(),
                                 0,
                                 identity_matrix,
                                 gfx::Point3F(0.f, 0.f, 0.f),
                                 gfx::PointF(0.f, 0.f),
                                 gfx::Size(10, 20),
                                 true);

    PostSetNeedsCommitToMainThread();
  }

  void PostEvictTextures() {
    ImplThreadTaskRunner()->PostTask(
        FROM_HERE,
        base::Bind(&LayerTreeHostTestEvictTextures::EvictTexturesOnImplThread,
                   base::Unretained(this)));
  }

  void EvictTexturesOnImplThread() {
    DCHECK(impl_for_evict_textures_);
    impl_for_evict_textures_->EvictTexturesForTesting();
  }

  // Commit 1: Just commit and draw normally, then post an eviction at the end
  // that will trigger a commit.
  // Commit 2: Triggered by the eviction, let it go through and then set
  // needsCommit.
  // Commit 3: Triggered by the setNeedsCommit. In Layout(), post an eviction
  // task, which will be handled before the commit. Don't set needsCommit, it
  // should have been posted. A frame should not be drawn (note,
  // didCommitAndDrawFrame may be called anyway).
  // Commit 4: Triggered by the eviction, let it go through and then set
  // needsCommit.
  // Commit 5: Triggered by the setNeedsCommit, post an eviction task in
  // Layout(), a frame should not be drawn but a commit will be posted.
  // Commit 6: Triggered by the eviction, post an eviction task in
  // Layout(), which will be a noop, letting the commit (which recreates the
  // textures) go through and draw a frame, then end the test.
  //
  // Commits 1+2 test the eviction recovery path where eviction happens outside
  // of the beginFrame/commit pair.
  // Commits 3+4 test the eviction recovery path where eviction happens inside
  // the beginFrame/commit pair.
  // Commits 5+6 test the path where an eviction happens during the eviction
  // recovery path.
  void DidCommit() override {
    switch (num_commits_) {
      case 1:
        EXPECT_TRUE(layer_->HaveBackingTexture());
        PostEvictTextures();
        break;
      case 2:
        EXPECT_TRUE(layer_->HaveBackingTexture());
        layer_tree_host()->SetNeedsCommit();
        break;
      case 3:
        break;
      case 4:
        EXPECT_TRUE(layer_->HaveBackingTexture());
        layer_tree_host()->SetNeedsCommit();
        break;
      case 5:
        break;
      case 6:
        EXPECT_TRUE(layer_->HaveBackingTexture());
        EndTest();
        break;
      default:
        NOTREACHED();
        break;
    }
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* impl) override {
    impl_for_evict_textures_ = impl;
  }

  void Layout() override {
    ++num_commits_;
    switch (num_commits_) {
      case 1:
      case 2:
        break;
      case 3:
        PostEvictTextures();
        break;
      case 4:
        // We couldn't check in didCommitAndDrawFrame on commit 3,
        // so check here.
        EXPECT_FALSE(layer_->HaveBackingTexture());
        break;
      case 5:
        PostEvictTextures();
        break;
      case 6:
        // We couldn't check in didCommitAndDrawFrame on commit 5,
        // so check here.
        EXPECT_FALSE(layer_->HaveBackingTexture());
        PostEvictTextures();
        break;
      default:
        NOTREACHED();
        break;
    }
  }

  void AfterTest() override {}

 private:
  FakeContentLayerClient client_;
  scoped_refptr<EvictionTestLayer> layer_;
  LayerTreeHostImpl* impl_for_evict_textures_;
  int num_commits_;
};

MULTI_THREAD_NOIMPL_TEST_F(LayerTreeHostTestEvictTextures);

class LayerTreeHostTestContinuousInvalidate : public LayerTreeHostTest {
 public:
  LayerTreeHostTestContinuousInvalidate()
      : num_commit_complete_(0), num_draw_layers_(0) {}

  void BeginTest() override {
    layer_tree_host()->SetViewportSize(gfx::Size(10, 10));
    layer_tree_host()->root_layer()->SetBounds(gfx::Size(10, 10));

    if (layer_tree_host()->settings().impl_side_painting)
      layer_ = FakePictureLayer::Create(&client_);
    else
      layer_ = FakeContentLayer::Create(&client_);

    layer_->SetBounds(gfx::Size(10, 10));
    layer_->SetPosition(gfx::PointF(0.f, 0.f));
    layer_->SetIsDrawable(true);
    layer_tree_host()->root_layer()->AddChild(layer_);

    PostSetNeedsCommitToMainThread();
  }

  void DidCommitAndDrawFrame() override {
    if (num_draw_layers_ == 2)
      return;
    layer_->SetNeedsDisplay();
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* impl) override {
    if (num_draw_layers_ == 1)
      num_commit_complete_++;
  }

  void DrawLayersOnThread(LayerTreeHostImpl* impl) override {
    num_draw_layers_++;
    if (num_draw_layers_ == 2)
      EndTest();
  }

  void AfterTest() override {
    // Check that we didn't commit twice between first and second draw.
    EXPECT_EQ(1, num_commit_complete_);
  }

 private:
  FakeContentLayerClient client_;
  scoped_refptr<Layer> layer_;
  int num_commit_complete_;
  int num_draw_layers_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(LayerTreeHostTestContinuousInvalidate);

class LayerTreeHostTestDeferCommits : public LayerTreeHostTest {
 public:
  LayerTreeHostTestDeferCommits()
      : num_will_begin_impl_frame_(0),
        num_send_begin_main_frame_(0) {}

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void WillBeginImplFrame(const BeginFrameArgs& args) override {
    num_will_begin_impl_frame_++;
    switch (num_will_begin_impl_frame_) {
      case 1:
        break;
      case 2:
        PostSetNeedsCommitToMainThread();
        break;
      case 3:
        PostSetDeferCommitsToMainThread(false);
        break;
      default:
        // Sometimes |num_will_begin_impl_frame_| will be greater than 3 if the
        // main thread is slow to respond.
        break;
    }
  }

  void ScheduledActionSendBeginMainFrame() override {
    num_send_begin_main_frame_++;
    switch (num_send_begin_main_frame_) {
      case 1:
        PostSetDeferCommitsToMainThread(true);
        break;
      case 2:
        EndTest();
        break;
      default:
        NOTREACHED();
        break;
    }
  }

  void AfterTest() override {
    EXPECT_GE(num_will_begin_impl_frame_, 3);
    EXPECT_EQ(2, num_send_begin_main_frame_);
  }

 private:
  int num_will_begin_impl_frame_;
  int num_send_begin_main_frame_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(LayerTreeHostTestDeferCommits);

class LayerTreeHostWithProxy : public LayerTreeHost {
 public:
  LayerTreeHostWithProxy(FakeLayerTreeHostClient* client,
                         const LayerTreeSettings& settings,
                         scoped_ptr<FakeProxy> proxy)
      : LayerTreeHost(client, NULL, NULL, NULL, settings) {
    proxy->SetLayerTreeHost(this);
    client->SetLayerTreeHost(this);
    InitializeForTesting(proxy.Pass());
  }
};

TEST(LayerTreeHostTest, LimitPartialUpdates) {
  // When partial updates are not allowed, max updates should be 0.
  {
    FakeLayerTreeHostClient client(FakeLayerTreeHostClient::DIRECT_3D);

    scoped_ptr<FakeProxy> proxy(new FakeProxy);
    proxy->GetRendererCapabilities().allow_partial_texture_updates = false;
    proxy->SetMaxPartialTextureUpdates(5);

    LayerTreeSettings settings;
    settings.impl_side_painting = false;
    settings.max_partial_texture_updates = 10;

    LayerTreeHostWithProxy host(&client, settings, proxy.Pass());

    EXPECT_EQ(0u, host.MaxPartialTextureUpdates());
  }

  // When partial updates are allowed,
  // max updates should be limited by the proxy.
  {
    FakeLayerTreeHostClient client(FakeLayerTreeHostClient::DIRECT_3D);

    scoped_ptr<FakeProxy> proxy(new FakeProxy);
    proxy->GetRendererCapabilities().allow_partial_texture_updates = true;
    proxy->SetMaxPartialTextureUpdates(5);

    LayerTreeSettings settings;
    settings.impl_side_painting = false;
    settings.max_partial_texture_updates = 10;

    LayerTreeHostWithProxy host(&client, settings, proxy.Pass());

    EXPECT_EQ(5u, host.MaxPartialTextureUpdates());
  }

  // When partial updates are allowed,
  // max updates should also be limited by the settings.
  {
    FakeLayerTreeHostClient client(FakeLayerTreeHostClient::DIRECT_3D);

    scoped_ptr<FakeProxy> proxy(new FakeProxy);
    proxy->GetRendererCapabilities().allow_partial_texture_updates = true;
    proxy->SetMaxPartialTextureUpdates(20);

    LayerTreeSettings settings;
    settings.impl_side_painting = false;
    settings.max_partial_texture_updates = 10;

    LayerTreeHostWithProxy host(&client, settings, proxy.Pass());

    EXPECT_EQ(10u, host.MaxPartialTextureUpdates());
  }
}

TEST(LayerTreeHostTest, PartialUpdatesWithGLRenderer) {
  FakeLayerTreeHostClient client(FakeLayerTreeHostClient::DIRECT_3D);

  LayerTreeSettings settings;
  settings.max_partial_texture_updates = 4;
  settings.single_thread_proxy_scheduler = false;
  settings.impl_side_painting = false;

  scoped_ptr<SharedBitmapManager> shared_bitmap_manager(
      new TestSharedBitmapManager());
  scoped_ptr<LayerTreeHost> host = LayerTreeHost::CreateSingleThreaded(
      &client, &client, shared_bitmap_manager.get(), NULL, NULL, settings,
      base::MessageLoopProxy::current(), nullptr);
  client.SetLayerTreeHost(host.get());
  host->Composite(base::TimeTicks::Now());

  EXPECT_EQ(4u, host->settings().max_partial_texture_updates);
}

TEST(LayerTreeHostTest, PartialUpdatesWithSoftwareRenderer) {
  FakeLayerTreeHostClient client(FakeLayerTreeHostClient::DIRECT_SOFTWARE);

  LayerTreeSettings settings;
  settings.max_partial_texture_updates = 4;
  settings.single_thread_proxy_scheduler = false;
  settings.impl_side_painting = false;

  scoped_ptr<SharedBitmapManager> shared_bitmap_manager(
      new TestSharedBitmapManager());
  scoped_ptr<LayerTreeHost> host = LayerTreeHost::CreateSingleThreaded(
      &client, &client, shared_bitmap_manager.get(), NULL, NULL, settings,
      base::MessageLoopProxy::current(), nullptr);
  client.SetLayerTreeHost(host.get());
  host->Composite(base::TimeTicks::Now());

  EXPECT_EQ(4u, host->settings().max_partial_texture_updates);
}

TEST(LayerTreeHostTest, PartialUpdatesWithDelegatingRendererAndGLContent) {
  FakeLayerTreeHostClient client(FakeLayerTreeHostClient::DELEGATED_3D);

  LayerTreeSettings settings;
  settings.max_partial_texture_updates = 4;
  settings.single_thread_proxy_scheduler = false;
  settings.impl_side_painting = false;

  scoped_ptr<SharedBitmapManager> shared_bitmap_manager(
      new TestSharedBitmapManager());
  scoped_ptr<LayerTreeHost> host = LayerTreeHost::CreateSingleThreaded(
      &client, &client, shared_bitmap_manager.get(), NULL, NULL, settings,
      base::MessageLoopProxy::current(), nullptr);
  client.SetLayerTreeHost(host.get());
  host->Composite(base::TimeTicks::Now());

  EXPECT_EQ(0u, host->MaxPartialTextureUpdates());
}

TEST(LayerTreeHostTest,
     PartialUpdatesWithDelegatingRendererAndSoftwareContent) {
  FakeLayerTreeHostClient client(FakeLayerTreeHostClient::DELEGATED_SOFTWARE);

  LayerTreeSettings settings;
  settings.max_partial_texture_updates = 4;
  settings.single_thread_proxy_scheduler = false;
  settings.impl_side_painting = false;

  scoped_ptr<SharedBitmapManager> shared_bitmap_manager(
      new TestSharedBitmapManager());
  scoped_ptr<LayerTreeHost> host = LayerTreeHost::CreateSingleThreaded(
      &client, &client, shared_bitmap_manager.get(), NULL, NULL, settings,
      base::MessageLoopProxy::current(), nullptr);
  client.SetLayerTreeHost(host.get());
  host->Composite(base::TimeTicks::Now());

  EXPECT_EQ(0u, host->MaxPartialTextureUpdates());
}

// TODO(sohanjg) : Remove it once impl-side painting ships everywhere.
class LayerTreeHostTestShutdownWithOnlySomeResourcesEvicted
    : public LayerTreeHostTest {
 public:
  LayerTreeHostTestShutdownWithOnlySomeResourcesEvicted()
      : root_layer_(FakeContentLayer::Create(&client_)),
        child_layer1_(FakeContentLayer::Create(&client_)),
        child_layer2_(FakeContentLayer::Create(&client_)),
        num_commits_(0) {}

  void BeginTest() override {
    layer_tree_host()->SetViewportSize(gfx::Size(100, 100));
    root_layer_->SetBounds(gfx::Size(100, 100));
    child_layer1_->SetBounds(gfx::Size(100, 100));
    child_layer2_->SetBounds(gfx::Size(100, 100));
    root_layer_->AddChild(child_layer1_);
    root_layer_->AddChild(child_layer2_);
    layer_tree_host()->SetRootLayer(root_layer_);
    PostSetNeedsCommitToMainThread();
  }

  void DidSetVisibleOnImplTree(LayerTreeHostImpl* host_impl,
                               bool visible) override {
    if (visible) {
      // One backing should remain unevicted.
      EXPECT_EQ(100u * 100u * 4u * 1u,
                contents_texture_manager_->MemoryUseBytes());
    } else {
      EXPECT_EQ(0u, contents_texture_manager_->MemoryUseBytes());
    }

    // Make sure that contents textures are marked as having been
    // purged.
    EXPECT_TRUE(host_impl->active_tree()->ContentsTexturesPurged());
    // End the test in this state.
    EndTest();
  }

  void DidActivateTreeOnThread(LayerTreeHostImpl* host_impl) override {
    ++num_commits_;
    switch (num_commits_) {
      case 1:
        // All three backings should have memory.
        EXPECT_EQ(100u * 100u * 4u * 3u,
                  contents_texture_manager_->MemoryUseBytes());

        // Set a new policy that will kick out 1 of the 3 resources.
        // Because a resource was evicted, a commit will be kicked off.
        host_impl->SetMemoryPolicy(
            ManagedMemoryPolicy(100 * 100 * 4 * 2,
                                gpu::MemoryAllocation::CUTOFF_ALLOW_EVERYTHING,
                                1000));
        break;
      case 2:
        // Only two backings should have memory.
        EXPECT_EQ(100u * 100u * 4u * 2u,
                  contents_texture_manager_->MemoryUseBytes());
        // Become backgrounded, which will cause 1 more resource to be
        // evicted.
        PostSetVisibleToMainThread(false);
        break;
      default:
        // No further commits should happen because this is not visible
        // anymore.
        NOTREACHED();
        break;
    }
  }

  void AfterTest() override {}

 private:
  FakeContentLayerClient client_;
  scoped_refptr<FakeContentLayer> root_layer_;
  scoped_refptr<FakeContentLayer> child_layer1_;
  scoped_refptr<FakeContentLayer> child_layer2_;
  int num_commits_;
};

SINGLE_AND_MULTI_THREAD_NOIMPL_TEST_F(
    LayerTreeHostTestShutdownWithOnlySomeResourcesEvicted);

class LayerTreeHostTestLCDChange : public LayerTreeHostTest {
 public:
  void SetupTree() override {
    num_tiles_rastered_ = 0;

    scoped_refptr<Layer> root_layer = PictureLayer::Create(&client_);
    client_.set_fill_with_nonsolid_color(true);
    root_layer->SetIsDrawable(true);
    root_layer->SetBounds(gfx::Size(10, 10));
    root_layer->SetContentsOpaque(true);

    layer_tree_host()->SetRootLayer(root_layer);

    // The expectations are based on the assumption that the default
    // LCD settings are:
    EXPECT_TRUE(layer_tree_host()->settings().can_use_lcd_text);

    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DidCommitAndDrawFrame() override {
    switch (layer_tree_host()->source_frame_number()) {
      case 1:
        PostSetNeedsCommitToMainThread();
        break;
      case 2:
        // Change layer opacity that should trigger lcd change.
        layer_tree_host()->root_layer()->SetOpacity(.5f);
        break;
      case 3:
        // Change layer opacity that should not trigger lcd change.
        layer_tree_host()->root_layer()->SetOpacity(1.f);
        break;
      case 4:
        EndTest();
        break;
    }
  }

  void NotifyTileStateChangedOnThread(LayerTreeHostImpl* host_impl,
                                      const Tile* tile) override {
    ++num_tiles_rastered_;
  }

  void DrawLayersOnThread(LayerTreeHostImpl* host_impl) override {
    PictureLayerImpl* root_layer =
        static_cast<PictureLayerImpl*>(host_impl->active_tree()->root_layer());
    bool can_use_lcd_text =
        host_impl->active_tree()->root_layer()->can_use_lcd_text();
    switch (host_impl->active_tree()->source_frame_number()) {
      case 0:
        // The first draw.
        EXPECT_EQ(1, num_tiles_rastered_);
        EXPECT_TRUE(can_use_lcd_text);
        EXPECT_TRUE(root_layer->RasterSourceUsesLCDText());
        break;
      case 1:
        // Nothing changed on the layer.
        EXPECT_EQ(1, num_tiles_rastered_);
        EXPECT_TRUE(can_use_lcd_text);
        EXPECT_TRUE(root_layer->RasterSourceUsesLCDText());
        break;
      case 2:
        // LCD text was disabled; it should be re-rastered with LCD text off.
        EXPECT_EQ(2, num_tiles_rastered_);
        EXPECT_FALSE(can_use_lcd_text);
        EXPECT_FALSE(root_layer->RasterSourceUsesLCDText());
        break;
      case 3:
        // LCD text was enabled, but it's sticky and stays off.
        EXPECT_EQ(2, num_tiles_rastered_);
        EXPECT_TRUE(can_use_lcd_text);
        EXPECT_FALSE(root_layer->RasterSourceUsesLCDText());
        break;
    }
  }

  void AfterTest() override {}

 private:
  FakeContentLayerClient client_;
  int num_tiles_rastered_;
};

SINGLE_AND_MULTI_THREAD_IMPL_TEST_F(LayerTreeHostTestLCDChange);

// Verify that the BeginFrame notification is used to initiate rendering.
class LayerTreeHostTestBeginFrameNotification : public LayerTreeHostTest {
 public:
  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->use_external_begin_frame_source = true;
  }

  void BeginTest() override {
    // This will trigger a SetNeedsBeginFrame which will trigger a
    // BeginFrame.
    PostSetNeedsCommitToMainThread();
  }

  DrawResult PrepareToDrawOnThread(LayerTreeHostImpl* host_impl,
                                   LayerTreeHostImpl::FrameData* frame,
                                   DrawResult draw_result) override {
    EndTest();
    return DRAW_SUCCESS;
  }

  void AfterTest() override {}

 private:
  base::TimeTicks frame_time_;
};

MULTI_THREAD_TEST_F(LayerTreeHostTestBeginFrameNotification);

class LayerTreeHostTestBeginFrameNotificationShutdownWhileEnabled
    : public LayerTreeHostTest {
 public:
  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->use_external_begin_frame_source = true;
    settings->using_synchronous_renderer_compositor = true;
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void CommitCompleteOnThread(LayerTreeHostImpl* host_impl) override {
    // The BeginFrame notification is turned off now but will get enabled
    // once we return. End test while it's enabled.
    ImplThreadTaskRunner()->PostTask(
        FROM_HERE,
        base::Bind(&LayerTreeHostTestBeginFrameNotification::EndTest,
                   base::Unretained(this)));
  }

  void AfterTest() override {}
};

MULTI_THREAD_TEST_F(
    LayerTreeHostTestBeginFrameNotificationShutdownWhileEnabled);

class LayerTreeHostTestAbortedCommitDoesntStall : public LayerTreeHostTest {
 protected:
  LayerTreeHostTestAbortedCommitDoesntStall()
      : commit_count_(0), commit_abort_count_(0), commit_complete_count_(0) {}

  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->use_external_begin_frame_source = true;
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DidCommit() override {
    commit_count_++;
    if (commit_count_ == 4) {
      // After two aborted commits, request a real commit now to make sure a
      // real commit following an aborted commit will still complete and
      // end the test even when the Impl thread is idle.
      layer_tree_host()->SetNeedsCommit();
    }
  }

  void BeginMainFrameAbortedOnThread(LayerTreeHostImpl* host_impl,
                                     CommitEarlyOutReason reason) override {
    commit_abort_count_++;
    // Initiate another abortable commit.
    host_impl->SetNeedsCommit();
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* host_impl) override {
    commit_complete_count_++;
    if (commit_complete_count_ == 1) {
      // Initiate an abortable commit after the first commit.
      host_impl->SetNeedsCommit();
    } else {
      EndTest();
    }
  }

  void AfterTest() override {
    EXPECT_EQ(commit_count_, 5);
    EXPECT_EQ(commit_abort_count_, 3);
    EXPECT_EQ(commit_complete_count_, 2);
  }

  int commit_count_;
  int commit_abort_count_;
  int commit_complete_count_;
};

class LayerTreeHostTestAbortedCommitDoesntStallSynchronousCompositor
    : public LayerTreeHostTestAbortedCommitDoesntStall {
  void InitializeSettings(LayerTreeSettings* settings) override {
    LayerTreeHostTestAbortedCommitDoesntStall::InitializeSettings(settings);
    settings->using_synchronous_renderer_compositor = true;
  }
};

MULTI_THREAD_TEST_F(
    LayerTreeHostTestAbortedCommitDoesntStallSynchronousCompositor);

class LayerTreeHostTestAbortedCommitDoesntStallDisabledVsync
    : public LayerTreeHostTestAbortedCommitDoesntStall {
  void InitializeSettings(LayerTreeSettings* settings) override {
    LayerTreeHostTestAbortedCommitDoesntStall::InitializeSettings(settings);
    settings->throttle_frame_production = false;
  }
};

MULTI_THREAD_TEST_F(LayerTreeHostTestAbortedCommitDoesntStallDisabledVsync);

class LayerTreeHostTestUninvertibleTransformDoesNotBlockActivation
    : public LayerTreeHostTest {
 protected:
  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->impl_side_painting = true;
  }

  void SetupTree() override {
    LayerTreeHostTest::SetupTree();

    scoped_refptr<Layer> layer = PictureLayer::Create(&client_);
    layer->SetTransform(gfx::Transform(0.0, 0.0, 0.0, 0.0, 0.0, 0.0));
    layer->SetBounds(gfx::Size(10, 10));
    layer_tree_host()->root_layer()->AddChild(layer);
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DidActivateTreeOnThread(LayerTreeHostImpl* host_impl) override {
    EndTest();
  }

  void AfterTest() override {}

  FakeContentLayerClient client_;
};

MULTI_THREAD_TEST_F(
    LayerTreeHostTestUninvertibleTransformDoesNotBlockActivation);

class LayerTreeHostTestChangeLayerPropertiesInPaintContents
    : public LayerTreeHostTest {
 public:
  class SetBoundsClient : public ContentLayerClient {
   public:
    SetBoundsClient() : layer_(0) {}

    void set_layer(Layer* layer) { layer_ = layer; }

    void PaintContents(SkCanvas* canvas,
                       const gfx::Rect& clip,
                       PaintingControlSetting picture_control) override {
      layer_->SetBounds(gfx::Size(2, 2));
    }

    scoped_refptr<DisplayItemList> PaintContentsToDisplayList(
        const gfx::Rect& clip,
        PaintingControlSetting picture_control) override {
      NOTIMPLEMENTED();
      return DisplayItemList::Create();
    }

    bool FillsBoundsCompletely() const override { return false; }

   private:
    Layer* layer_;
  };

  LayerTreeHostTestChangeLayerPropertiesInPaintContents() : num_commits_(0) {}

  void SetupTree() override {
    if (layer_tree_host()->settings().impl_side_painting) {
      scoped_refptr<PictureLayer> root_layer = PictureLayer::Create(&client_);
      layer_tree_host()->SetRootLayer(root_layer);
    } else {
      scoped_refptr<ContentLayer> root_layer = ContentLayer::Create(&client_);
      layer_tree_host()->SetRootLayer(root_layer);
    }
    Layer* root_layer = layer_tree_host()->root_layer();
    root_layer->SetIsDrawable(true);
    root_layer->SetBounds(gfx::Size(1, 1));

    client_.set_layer(root_layer);

    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }
  void AfterTest() override {}

  void DidActivateTreeOnThread(LayerTreeHostImpl* host_impl) override {
    num_commits_++;
    if (num_commits_ == 1) {
      LayerImpl* root_layer = host_impl->active_tree()->root_layer();
      EXPECT_EQ(gfx::Size(1, 1), root_layer->bounds());
    } else {
      LayerImpl* root_layer = host_impl->active_tree()->root_layer();
      EXPECT_EQ(gfx::Size(2, 2), root_layer->bounds());
      EndTest();
    }
  }

 private:
  SetBoundsClient client_;
  int num_commits_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(
    LayerTreeHostTestChangeLayerPropertiesInPaintContents);

class MockIOSurfaceWebGraphicsContext3D : public TestWebGraphicsContext3D {
 public:
  MockIOSurfaceWebGraphicsContext3D() {
    test_capabilities_.gpu.iosurface = true;
    test_capabilities_.gpu.texture_rectangle = true;
  }

  GLuint createTexture() override { return 1; }
  MOCK_METHOD1(activeTexture, void(GLenum texture));
  MOCK_METHOD2(bindTexture, void(GLenum target,
                                 GLuint texture_id));
  MOCK_METHOD3(texParameteri, void(GLenum target,
                                   GLenum pname,
                                   GLint param));
  MOCK_METHOD5(texImageIOSurface2DCHROMIUM, void(GLenum target,
                                                 GLint width,
                                                 GLint height,
                                                 GLuint ioSurfaceId,
                                                 GLuint plane));
  MOCK_METHOD4(drawElements, void(GLenum mode,
                                  GLsizei count,
                                  GLenum type,
                                  GLintptr offset));
  MOCK_METHOD1(deleteTexture, void(GLenum texture));
  MOCK_METHOD3(produceTextureDirectCHROMIUM,
               void(GLuint texture, GLenum target, const GLbyte* mailbox));
};

class LayerTreeHostTestIOSurfaceDrawing : public LayerTreeHostTest {
 protected:
  scoped_ptr<FakeOutputSurface> CreateFakeOutputSurface() override {
    scoped_ptr<MockIOSurfaceWebGraphicsContext3D> mock_context_owned(
        new MockIOSurfaceWebGraphicsContext3D);
    mock_context_ = mock_context_owned.get();

    if (delegating_renderer())
      return FakeOutputSurface::CreateDelegating3d(mock_context_owned.Pass());
    else
      return FakeOutputSurface::Create3d(mock_context_owned.Pass());
  }

  void SetupTree() override {
    LayerTreeHostTest::SetupTree();

    layer_tree_host()->root_layer()->SetIsDrawable(false);

    io_surface_id_ = 9;
    io_surface_size_ = gfx::Size(6, 7);

    scoped_refptr<IOSurfaceLayer> io_surface_layer = IOSurfaceLayer::Create();
    io_surface_layer->SetBounds(gfx::Size(10, 10));
    io_surface_layer->SetIsDrawable(true);
    io_surface_layer->SetContentsOpaque(true);
    io_surface_layer->SetIOSurfaceProperties(io_surface_id_, io_surface_size_);
    layer_tree_host()->root_layer()->AddChild(io_surface_layer);
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DidActivateTreeOnThread(LayerTreeHostImpl* host_impl) override {
    EXPECT_EQ(0u, host_impl->resource_provider()->num_resources());
    // In WillDraw, the IOSurfaceLayer sets up the io surface texture.

    EXPECT_CALL(*mock_context_, activeTexture(_)).Times(0);
    EXPECT_CALL(*mock_context_, bindTexture(GL_TEXTURE_RECTANGLE_ARB, 1))
        .Times(AtLeast(1));
    EXPECT_CALL(*mock_context_,
                texParameteri(
                    GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR))
        .Times(1);
    EXPECT_CALL(*mock_context_,
                texParameteri(
                    GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR))
        .Times(1);
    EXPECT_CALL(*mock_context_,
                texParameteri(GL_TEXTURE_RECTANGLE_ARB,
                              GL_TEXTURE_POOL_CHROMIUM,
                              GL_TEXTURE_POOL_UNMANAGED_CHROMIUM)).Times(1);
    EXPECT_CALL(*mock_context_,
                texParameteri(GL_TEXTURE_RECTANGLE_ARB,
                              GL_TEXTURE_WRAP_S,
                              GL_CLAMP_TO_EDGE)).Times(1);
    EXPECT_CALL(*mock_context_,
                texParameteri(GL_TEXTURE_RECTANGLE_ARB,
                              GL_TEXTURE_WRAP_T,
                              GL_CLAMP_TO_EDGE)).Times(1);

    EXPECT_CALL(*mock_context_,
                texImageIOSurface2DCHROMIUM(GL_TEXTURE_RECTANGLE_ARB,
                                            io_surface_size_.width(),
                                            io_surface_size_.height(),
                                            io_surface_id_,
                                            0)).Times(1);

    EXPECT_CALL(*mock_context_, bindTexture(_, 0)).Times(AnyNumber());
  }

  DrawResult PrepareToDrawOnThread(LayerTreeHostImpl* host_impl,
                                   LayerTreeHostImpl::FrameData* frame,
                                   DrawResult draw_result) override {
    Mock::VerifyAndClearExpectations(&mock_context_);
    ResourceProvider* resource_provider = host_impl->resource_provider();
    EXPECT_EQ(1u, resource_provider->num_resources());
    CHECK_EQ(1u, frame->render_passes.size());
    CHECK_LE(1u, frame->render_passes[0]->quad_list.size());
    const DrawQuad* quad = frame->render_passes[0]->quad_list.front();
    CHECK_EQ(DrawQuad::IO_SURFACE_CONTENT, quad->material);
    const IOSurfaceDrawQuad* io_surface_draw_quad =
        IOSurfaceDrawQuad::MaterialCast(quad);
    EXPECT_EQ(io_surface_size_, io_surface_draw_quad->io_surface_size);
    EXPECT_NE(0u, io_surface_draw_quad->io_surface_resource_id);
    EXPECT_EQ(static_cast<GLenum>(GL_TEXTURE_RECTANGLE_ARB),
              resource_provider->TargetForTesting(
                  io_surface_draw_quad->io_surface_resource_id));

    if (delegating_renderer()) {
      // The io surface layer's resource should be sent to the parent.
      EXPECT_CALL(*mock_context_, produceTextureDirectCHROMIUM(
                                      _, GL_TEXTURE_RECTANGLE_ARB, _)).Times(1);
    } else {
      // The io surface layer's texture is drawn.
      EXPECT_CALL(*mock_context_, activeTexture(GL_TEXTURE0)).Times(AtLeast(1));
      EXPECT_CALL(*mock_context_, drawElements(GL_TRIANGLES, 6, _, _))
          .Times(AtLeast(1));
    }

    return draw_result;
  }

  void DrawLayersOnThread(LayerTreeHostImpl* host_impl) override {
    Mock::VerifyAndClearExpectations(&mock_context_);

    EXPECT_CALL(*mock_context_, deleteTexture(1)).Times(AtLeast(1));
    EndTest();
  }

  void AfterTest() override {}

  int io_surface_id_;
  MockIOSurfaceWebGraphicsContext3D* mock_context_;
  gfx::Size io_surface_size_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(LayerTreeHostTestIOSurfaceDrawing);

class LayerTreeHostTestNumFramesPending : public LayerTreeHostTest {
 public:
  void BeginTest() override {
    frame_ = 0;
    PostSetNeedsCommitToMainThread();
  }

  // Round 1: commit + draw
  // Round 2: commit only (no draw/swap)
  // Round 3: draw only (no commit)

  void DidCommit() override {
    int commit = layer_tree_host()->source_frame_number();
    switch (commit) {
      case 2:
        // Round 2 done.
        EXPECT_EQ(1, frame_);
        layer_tree_host()->SetNeedsRedraw();
        break;
    }
  }

  void DidCompleteSwapBuffers() override {
    int commit = layer_tree_host()->source_frame_number();
    ++frame_;
    switch (frame_) {
      case 1:
        // Round 1 done.
        EXPECT_EQ(1, commit);
        layer_tree_host()->SetNeedsCommit();
        break;
      case 2:
        // Round 3 done.
        EXPECT_EQ(2, commit);
        EndTest();
        break;
    }
  }

  void AfterTest() override {}

 protected:
  int frame_;
};

// Flaky on all platforms: http://crbug.com/327498
TEST_F(LayerTreeHostTestNumFramesPending, DISABLED_DelegatingRenderer) {
  RunTest(true, true, true);
}

TEST_F(LayerTreeHostTestNumFramesPending, DISABLED_GLRenderer) {
  RunTest(true, false, true);
}

class LayerTreeHostTestDeferredInitialize : public LayerTreeHostTest {
 public:
  void InitializeSettings(LayerTreeSettings* settings) override {
    // PictureLayer can only be used with impl side painting enabled.
    settings->impl_side_painting = true;
  }

  void SetupTree() override {
    layer_ = FakePictureLayer::Create(&client_);
    // Force commits to not be aborted so new frames get drawn, otherwise
    // the renderer gets deferred initialized but nothing new needs drawing.
    layer_->set_always_update_resources(true);
    layer_tree_host()->SetRootLayer(layer_);
    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override {
    did_initialize_gl_ = false;
    did_release_gl_ = false;
    last_source_frame_number_drawn_ = -1;  // Never drawn.
    PostSetNeedsCommitToMainThread();
  }

  scoped_ptr<FakeOutputSurface> CreateFakeOutputSurface() override {
    scoped_ptr<TestWebGraphicsContext3D> context3d(
        TestWebGraphicsContext3D::Create());

    return FakeOutputSurface::CreateDeferredGL(
        scoped_ptr<SoftwareOutputDevice>(new SoftwareOutputDevice),
        delegating_renderer());
  }

  void DrawLayersOnThread(LayerTreeHostImpl* host_impl) override {
    ASSERT_TRUE(host_impl->RootLayer());
    FakePictureLayerImpl* layer_impl =
        static_cast<FakePictureLayerImpl*>(host_impl->RootLayer());

    // The same frame can be draw multiple times if new visible tiles are
    // rasterized. But we want to make sure we only post DeferredInitialize
    // and ReleaseGL once, so early out if the same frame is drawn again.
    if (last_source_frame_number_drawn_ ==
        host_impl->active_tree()->source_frame_number())
      return;

    last_source_frame_number_drawn_ =
        host_impl->active_tree()->source_frame_number();

    if (!did_initialize_gl_) {
      EXPECT_LE(1u, layer_impl->append_quads_count());
      ImplThreadTaskRunner()->PostTask(
          FROM_HERE,
          base::Bind(
              &LayerTreeHostTestDeferredInitialize::DeferredInitializeAndRedraw,
              base::Unretained(this),
              base::Unretained(host_impl)));
    } else if (did_initialize_gl_ && !did_release_gl_) {
      EXPECT_LE(2u, layer_impl->append_quads_count());
      ImplThreadTaskRunner()->PostTask(
          FROM_HERE,
          base::Bind(&LayerTreeHostTestDeferredInitialize::ReleaseGLAndRedraw,
                     base::Unretained(this),
                     base::Unretained(host_impl)));
    } else if (did_initialize_gl_ && did_release_gl_) {
      EXPECT_LE(3u, layer_impl->append_quads_count());
      EndTest();
    }
  }

  void DeferredInitializeAndRedraw(LayerTreeHostImpl* host_impl) {
    EXPECT_FALSE(did_initialize_gl_);
    // SetAndInitializeContext3D calls SetNeedsCommit.
    FakeOutputSurface* fake_output_surface =
        static_cast<FakeOutputSurface*>(host_impl->output_surface());
    scoped_refptr<TestContextProvider> context_provider =
        TestContextProvider::Create();  // Not bound to thread.
    scoped_refptr<TestContextProvider> worker_context_provider =
        TestContextProvider::Create();  // Not bound to thread.
    EXPECT_TRUE(fake_output_surface->InitializeAndSetContext3d(
        context_provider, worker_context_provider));
    did_initialize_gl_ = true;
  }

  void ReleaseGLAndRedraw(LayerTreeHostImpl* host_impl) {
    EXPECT_TRUE(did_initialize_gl_);
    EXPECT_FALSE(did_release_gl_);
    // ReleaseGL calls SetNeedsCommit.
    static_cast<FakeOutputSurface*>(host_impl->output_surface())->ReleaseGL();
    did_release_gl_ = true;
  }

  void SwapBuffersOnThread(LayerTreeHostImpl* host_impl, bool result) override {
    ASSERT_TRUE(result);
    DelegatedFrameData* delegated_frame_data =
        output_surface()->last_sent_frame().delegated_frame_data.get();
    if (!delegated_frame_data)
      return;

    // Return all resources immediately.
    TransferableResourceArray resources_to_return =
        output_surface()->resources_held_by_parent();

    CompositorFrameAck ack;
    for (size_t i = 0; i < resources_to_return.size(); ++i)
      output_surface()->ReturnResource(resources_to_return[i].id, &ack);
    host_impl->ReclaimResources(&ack);
  }

  void AfterTest() override {
    EXPECT_TRUE(did_initialize_gl_);
    EXPECT_TRUE(did_release_gl_);
  }

 private:
  FakeContentLayerClient client_;
  scoped_refptr<FakePictureLayer> layer_;
  bool did_initialize_gl_;
  bool did_release_gl_;
  int last_source_frame_number_drawn_;
};

MULTI_THREAD_TEST_F(LayerTreeHostTestDeferredInitialize);

class LayerTreeHostTestResourcelessSoftwareDraw : public LayerTreeHostTest {
 public:
  void SetupTree() override {
    root_layer_ = FakePictureLayer::Create(&client_);
    root_layer_->SetIsDrawable(true);
    root_layer_->SetBounds(gfx::Size(50, 50));

    parent_layer_ = FakePictureLayer::Create(&client_);
    parent_layer_->SetIsDrawable(true);
    parent_layer_->SetBounds(gfx::Size(50, 50));
    parent_layer_->SetForceRenderSurface(true);

    child_layer_ = FakePictureLayer::Create(&client_);
    child_layer_->SetIsDrawable(true);
    child_layer_->SetBounds(gfx::Size(50, 50));

    root_layer_->AddChild(parent_layer_);
    parent_layer_->AddChild(child_layer_);
    layer_tree_host()->SetRootLayer(root_layer_);

    LayerTreeHostTest::SetupTree();
  }

  scoped_ptr<FakeOutputSurface> CreateFakeOutputSurface() override {
    return FakeOutputSurface::CreateDeferredGL(
        make_scoped_ptr(new SoftwareOutputDevice), delegating_renderer());
  }

  void BeginTest() override {
    PostSetNeedsCommitToMainThread();
    swap_count_ = 0;
  }

  DrawResult PrepareToDrawOnThread(LayerTreeHostImpl* host_impl,
                                   LayerTreeHostImpl::FrameData* frame_data,
                                   DrawResult draw_result) override {
    if (host_impl->GetDrawMode() == DRAW_MODE_RESOURCELESS_SOFTWARE) {
      EXPECT_EQ(1u, frame_data->render_passes.size());
      // Has at least 3 quads for each layer.
      RenderPass* render_pass = frame_data->render_passes[0];
      EXPECT_GE(render_pass->quad_list.size(), 3u);
    } else {
      EXPECT_EQ(2u, frame_data->render_passes.size());

      // At least root layer quad in root render pass.
      EXPECT_GE(frame_data->render_passes[0]->quad_list.size(), 1u);
      // At least parent and child layer quads in parent render pass.
      EXPECT_GE(frame_data->render_passes[1]->quad_list.size(), 2u);
    }
    return draw_result;
  }

  void SwapBuffersCompleteOnThread(LayerTreeHostImpl* host_impl) override {
    swap_count_++;
    switch (swap_count_) {
      case 1: {
        gfx::Transform identity;
        gfx::Rect empty_rect;
        bool resourceless_software_draw = true;
        host_impl->SetExternalDrawConstraints(identity, empty_rect, empty_rect,
                                              empty_rect, identity,
                                              resourceless_software_draw);
        host_impl->SetFullRootLayerDamage();
        host_impl->SetNeedsRedraw();
        break;
      }
      case 2:
        EndTest();
        break;
      default:
        NOTREACHED();
    }
  }

  void AfterTest() override {}

 private:
  FakeContentLayerClient client_;
  scoped_refptr<Layer> root_layer_;
  scoped_refptr<Layer> parent_layer_;
  scoped_refptr<Layer> child_layer_;
  int swap_count_;
};

MULTI_THREAD_IMPL_TEST_F(LayerTreeHostTestResourcelessSoftwareDraw);

class LayerTreeHostTestDeferredInitializeWithGpuRasterization
    : public LayerTreeHostTestDeferredInitialize {
  void InitializeSettings(LayerTreeSettings* settings) override {
    // PictureLayer can only be used with impl side painting enabled.
    settings->impl_side_painting = true;
    settings->gpu_rasterization_enabled = true;
    settings->gpu_rasterization_forced = true;
  }
};

MULTI_THREAD_TEST_F(LayerTreeHostTestDeferredInitializeWithGpuRasterization);

// Test for UI Resource management.
class LayerTreeHostTestUIResource : public LayerTreeHostTest {
 public:
  LayerTreeHostTestUIResource() : num_ui_resources_(0) {}

  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->renderer_settings.texture_id_allocation_chunk_size = 1;
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DidCommit() override {
    int frame = layer_tree_host()->source_frame_number();
    switch (frame) {
      case 1:
        CreateResource();
        CreateResource();
        PostSetNeedsCommitToMainThread();
        break;
      case 2:
        // Usually ScopedUIResource are deleted from the manager in their
        // destructor.  Here we just want to test that a direct call to
        // DeleteUIResource works.
        layer_tree_host()->DeleteUIResource(ui_resources_[0]->id());
        PostSetNeedsCommitToMainThread();
        break;
      case 3:
        // DeleteUIResource can be called with an invalid id.
        layer_tree_host()->DeleteUIResource(ui_resources_[0]->id());
        PostSetNeedsCommitToMainThread();
        break;
      case 4:
        CreateResource();
        CreateResource();
        PostSetNeedsCommitToMainThread();
        break;
      case 5:
        ClearResources();
        EndTest();
        break;
    }
  }

  void PerformTest(LayerTreeHostImpl* impl) {
    TestWebGraphicsContext3D* context = TestContext();

    int frame = impl->active_tree()->source_frame_number();
    switch (frame) {
      case 0:
        ASSERT_EQ(0u, context->NumTextures());
        break;
      case 1:
        // Created two textures.
        ASSERT_EQ(2u, context->NumTextures());
        break;
      case 2:
        // One texture left after one deletion.
        ASSERT_EQ(1u, context->NumTextures());
        break;
      case 3:
        // Resource manager state should not change when delete is called on an
        // invalid id.
        ASSERT_EQ(1u, context->NumTextures());
        break;
      case 4:
        // Creation after deletion: two more creates should total up to
        // three textures.
        ASSERT_EQ(3u, context->NumTextures());
        break;
    }
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* impl) override {
    if (!impl->settings().impl_side_painting)
      PerformTest(impl);
  }

  void DidActivateTreeOnThread(LayerTreeHostImpl* impl) override {
    if (impl->settings().impl_side_painting)
      PerformTest(impl);
  }

  void AfterTest() override {}

 private:
  // Must clear all resources before exiting.
  void ClearResources() {
    for (int i = 0; i < num_ui_resources_; i++)
      ui_resources_[i] = nullptr;
  }

  void CreateResource() {
    ui_resources_[num_ui_resources_++] =
        FakeScopedUIResource::Create(layer_tree_host());
  }

  scoped_ptr<FakeScopedUIResource> ui_resources_[5];
  int num_ui_resources_;
};

MULTI_THREAD_TEST_F(LayerTreeHostTestUIResource);

class PushPropertiesCountingLayerImpl : public LayerImpl {
 public:
  static scoped_ptr<PushPropertiesCountingLayerImpl> Create(
      LayerTreeImpl* tree_impl, int id) {
    return make_scoped_ptr(new PushPropertiesCountingLayerImpl(tree_impl, id));
  }

  ~PushPropertiesCountingLayerImpl() override {}

  void PushPropertiesTo(LayerImpl* layer) override {
    LayerImpl::PushPropertiesTo(layer);
    push_properties_count_++;
    // Push state to the active tree because we can only access it from there.
    static_cast<PushPropertiesCountingLayerImpl*>(
        layer)->push_properties_count_ = push_properties_count_;
  }

  scoped_ptr<LayerImpl> CreateLayerImpl(LayerTreeImpl* tree_impl) override {
    return PushPropertiesCountingLayerImpl::Create(tree_impl, id());
  }

  size_t push_properties_count() const { return push_properties_count_; }
  void reset_push_properties_count() { push_properties_count_ = 0; }

 private:
  size_t push_properties_count_;

  PushPropertiesCountingLayerImpl(LayerTreeImpl* tree_impl, int id)
      : LayerImpl(tree_impl, id),
        push_properties_count_(0) {
    SetBounds(gfx::Size(1, 1));
  }
};

class PushPropertiesCountingLayer : public Layer {
 public:
  static scoped_refptr<PushPropertiesCountingLayer> Create() {
    return new PushPropertiesCountingLayer();
  }

  void PushPropertiesTo(LayerImpl* layer) override {
    Layer::PushPropertiesTo(layer);
    push_properties_count_++;
    if (persist_needs_push_properties_)
      needs_push_properties_ = true;
  }

  scoped_ptr<LayerImpl> CreateLayerImpl(LayerTreeImpl* tree_impl) override {
    return PushPropertiesCountingLayerImpl::Create(tree_impl, id());
  }

  void SetDrawsContent(bool draws_content) { SetIsDrawable(draws_content); }

  size_t push_properties_count() const { return push_properties_count_; }
  void reset_push_properties_count() { push_properties_count_ = 0; }

  void set_persist_needs_push_properties(bool persist) {
    persist_needs_push_properties_ = persist;
  }

 private:
  PushPropertiesCountingLayer()
      : push_properties_count_(0), persist_needs_push_properties_(false) {
    SetBounds(gfx::Size(1, 1));
  }
  ~PushPropertiesCountingLayer() override {}

  size_t push_properties_count_;
  bool persist_needs_push_properties_;
};

class LayerTreeHostTestLayersPushProperties : public LayerTreeHostTest {
 protected:
  void BeginTest() override {
    num_commits_ = 0;
    expected_push_properties_root_ = 0;
    expected_push_properties_child_ = 0;
    expected_push_properties_grandchild_ = 0;
    expected_push_properties_child2_ = 0;
    expected_push_properties_other_root_ = 0;
    expected_push_properties_leaf_layer_ = 0;
    PostSetNeedsCommitToMainThread();
  }

  void SetupTree() override {
    root_ = PushPropertiesCountingLayer::Create();
    root_->CreateRenderSurface();
    child_ = PushPropertiesCountingLayer::Create();
    child2_ = PushPropertiesCountingLayer::Create();
    grandchild_ = PushPropertiesCountingLayer::Create();
    leaf_always_pushing_layer_ = PushPropertiesCountingLayer::Create();
    leaf_always_pushing_layer_->set_persist_needs_push_properties(true);

    root_->AddChild(child_);
    root_->AddChild(child2_);
    child_->AddChild(grandchild_);
    child2_->AddChild(leaf_always_pushing_layer_);

    other_root_ = PushPropertiesCountingLayer::Create();
    other_root_->CreateRenderSurface();

    // Don't set the root layer here.
    LayerTreeHostTest::SetupTree();
  }

  void DidCommitAndDrawFrame() override {
    ++num_commits_;

    EXPECT_EQ(expected_push_properties_root_, root_->push_properties_count());
    EXPECT_EQ(expected_push_properties_child_, child_->push_properties_count());
    EXPECT_EQ(expected_push_properties_grandchild_,
              grandchild_->push_properties_count());
    EXPECT_EQ(expected_push_properties_child2_,
              child2_->push_properties_count());
    EXPECT_EQ(expected_push_properties_other_root_,
              other_root_->push_properties_count());
    EXPECT_EQ(expected_push_properties_leaf_layer_,
              leaf_always_pushing_layer_->push_properties_count());

    // The scrollbar layer always needs to be pushed.
    if (root_->layer_tree_host()) {
      EXPECT_TRUE(root_->descendant_needs_push_properties());
      EXPECT_FALSE(root_->needs_push_properties());
    }
    if (child2_->layer_tree_host()) {
      EXPECT_TRUE(child2_->descendant_needs_push_properties());
      EXPECT_FALSE(child2_->needs_push_properties());
    }
    if (leaf_always_pushing_layer_->layer_tree_host()) {
      EXPECT_FALSE(
          leaf_always_pushing_layer_->descendant_needs_push_properties());
      EXPECT_TRUE(leaf_always_pushing_layer_->needs_push_properties());
    }

    // child_ and grandchild_ don't persist their need to push properties.
    if (child_->layer_tree_host()) {
      EXPECT_FALSE(child_->descendant_needs_push_properties());
      EXPECT_FALSE(child_->needs_push_properties());
    }
    if (grandchild_->layer_tree_host()) {
      EXPECT_FALSE(grandchild_->descendant_needs_push_properties());
      EXPECT_FALSE(grandchild_->needs_push_properties());
    }

    if (other_root_->layer_tree_host()) {
      EXPECT_FALSE(other_root_->descendant_needs_push_properties());
      EXPECT_FALSE(other_root_->needs_push_properties());
    }

    switch (num_commits_) {
      case 1:
        layer_tree_host()->SetRootLayer(root_);
        // Layers added to the tree get committed.
        ++expected_push_properties_root_;
        ++expected_push_properties_child_;
        ++expected_push_properties_grandchild_;
        ++expected_push_properties_child2_;
        break;
      case 2:
        layer_tree_host()->SetNeedsCommit();
        // No layers need commit.
        break;
      case 3:
        layer_tree_host()->SetRootLayer(other_root_);
        // Layers added to the tree get committed.
        ++expected_push_properties_other_root_;
        break;
      case 4:
        layer_tree_host()->SetRootLayer(root_);
        // Layers added to the tree get committed.
        ++expected_push_properties_root_;
        ++expected_push_properties_child_;
        ++expected_push_properties_grandchild_;
        ++expected_push_properties_child2_;
        break;
      case 5:
        layer_tree_host()->SetNeedsCommit();
        // No layers need commit.
        break;
      case 6:
        child_->RemoveFromParent();
        // No layers need commit.
        break;
      case 7:
        root_->AddChild(child_);
        // Layers added to the tree get committed.
        ++expected_push_properties_child_;
        ++expected_push_properties_grandchild_;
        break;
      case 8:
        grandchild_->RemoveFromParent();
        // No layers need commit.
        break;
      case 9:
        child_->AddChild(grandchild_);
        // Layers added to the tree get committed.
        ++expected_push_properties_grandchild_;
        break;
      case 10:
        layer_tree_host()->SetViewportSize(gfx::Size(20, 20));
        // No layers need commit.
        break;
      case 11:
        layer_tree_host()->SetPageScaleFactorAndLimits(1.f, 0.8f, 1.1f);
        // No layers need commit.
        break;
      case 12:
        child_->SetPosition(gfx::Point(1, 1));
        // The modified layer needs commit
        ++expected_push_properties_child_;
        break;
      case 13:
        child2_->SetPosition(gfx::Point(1, 1));
        // The modified layer needs commit
        ++expected_push_properties_child2_;
        break;
      case 14:
        child_->RemoveFromParent();
        root_->AddChild(child_);
        // Layers added to the tree get committed.
        ++expected_push_properties_child_;
        ++expected_push_properties_grandchild_;
        break;
      case 15:
        grandchild_->SetPosition(gfx::Point(1, 1));
        // The modified layer needs commit
        ++expected_push_properties_grandchild_;
        break;
      case 16:
        // SetNeedsDisplay does not always set needs commit (so call it
        // explicitly), but is a property change.
        child_->SetNeedsDisplay();
        ++expected_push_properties_child_;
        layer_tree_host()->SetNeedsCommit();
        break;
      case 17:
        EndTest();
        break;
    }

    // The leaf layer always pushes.
    if (leaf_always_pushing_layer_->layer_tree_host())
      ++expected_push_properties_leaf_layer_;
  }

  void AfterTest() override {}

  int num_commits_;
  FakeContentLayerClient client_;
  scoped_refptr<PushPropertiesCountingLayer> root_;
  scoped_refptr<PushPropertiesCountingLayer> child_;
  scoped_refptr<PushPropertiesCountingLayer> child2_;
  scoped_refptr<PushPropertiesCountingLayer> grandchild_;
  scoped_refptr<PushPropertiesCountingLayer> other_root_;
  scoped_refptr<PushPropertiesCountingLayer> leaf_always_pushing_layer_;
  size_t expected_push_properties_root_;
  size_t expected_push_properties_child_;
  size_t expected_push_properties_child2_;
  size_t expected_push_properties_grandchild_;
  size_t expected_push_properties_other_root_;
  size_t expected_push_properties_leaf_layer_;
};

MULTI_THREAD_TEST_F(LayerTreeHostTestLayersPushProperties);

class LayerTreeHostTestImplLayersPushProperties
    : public LayerTreeHostTestLayersPushProperties {
 protected:
  void BeginTest() override {
    expected_push_properties_root_impl_ = 0;
    expected_push_properties_child_impl_ = 0;
    expected_push_properties_grandchild_impl_ = 0;
    expected_push_properties_child2_impl_ = 0;
    expected_push_properties_grandchild2_impl_ = 0;
    LayerTreeHostTestLayersPushProperties::BeginTest();
  }

  void DidActivateTreeOnThread(LayerTreeHostImpl* host_impl) override {
    // These commits are in response to the changes made in
    // LayerTreeHostTestLayersPushProperties::DidCommitAndDrawFrame()
    switch (num_commits_) {
      case 0:
        // Tree hasn't been setup yet don't bother to check anything.
        return;
      case 1:
        // Root gets set up, Everyone is initialized.
        ++expected_push_properties_root_impl_;
        ++expected_push_properties_child_impl_;
        ++expected_push_properties_grandchild_impl_;
        ++expected_push_properties_child2_impl_;
        ++expected_push_properties_grandchild2_impl_;
        break;
      case 2:
        // Tree doesn't change but the one leaf that always pushes is pushed.
        ++expected_push_properties_grandchild2_impl_;
        break;
      case 3:
        // Root is swapped here.
        // Clear the expected push properties the tree will be rebuilt.
        expected_push_properties_root_impl_ = 0;
        expected_push_properties_child_impl_ = 0;
        expected_push_properties_grandchild_impl_ = 0;
        expected_push_properties_child2_impl_ = 0;
        expected_push_properties_grandchild2_impl_ = 0;

        // Make sure the new root is pushed.
        EXPECT_EQ(1u, static_cast<PushPropertiesCountingLayerImpl*>(
                host_impl->RootLayer())->push_properties_count());
        return;
      case 4:
        // Root is swapped back all of the layers in the tree get pushed.
        ++expected_push_properties_root_impl_;
        ++expected_push_properties_child_impl_;
        ++expected_push_properties_grandchild_impl_;
        ++expected_push_properties_child2_impl_;
        ++expected_push_properties_grandchild2_impl_;
        break;
      case 5:
        // Tree doesn't change but the one leaf that always pushes is pushed.
        ++expected_push_properties_grandchild2_impl_;
        break;
      case 6:
        // First child is removed. Structure of the tree changes here so swap
        // some of the values. child_impl becomes child2_impl.
        expected_push_properties_child_impl_ =
            expected_push_properties_child2_impl_;
        expected_push_properties_child2_impl_ = 0;
        // grandchild_impl becomes grandchild2_impl.
        expected_push_properties_grandchild_impl_ =
            expected_push_properties_grandchild2_impl_;
        expected_push_properties_grandchild2_impl_ = 0;

        // grandchild_impl is now the leaf that always pushes. It is pushed.
        ++expected_push_properties_grandchild_impl_;
        break;
      case 7:
        // The leaf that always pushes is pushed.
        ++expected_push_properties_grandchild_impl_;

        // Child is added back. New layers are initialized.
        ++expected_push_properties_grandchild2_impl_;
        ++expected_push_properties_child2_impl_;
        break;
      case 8:
        // Leaf is removed.
        expected_push_properties_grandchild2_impl_ = 0;

        // Always pushing.
        ++expected_push_properties_grandchild_impl_;
        break;
      case 9:
        // Leaf is added back
        ++expected_push_properties_grandchild2_impl_;

        // The leaf that always pushes is pushed.
        ++expected_push_properties_grandchild_impl_;
        break;
      case 10:
        // The leaf that always pushes is pushed.
        ++expected_push_properties_grandchild_impl_;
        break;
      case 11:
        // The leaf that always pushes is pushed.
        ++expected_push_properties_grandchild_impl_;
        break;
      case 12:
        // The leaf that always pushes is pushed.
        ++expected_push_properties_grandchild_impl_;

        // This child position was changed.
        ++expected_push_properties_child2_impl_;
        break;
      case 13:
        // The position of this child was changed.
        ++expected_push_properties_child_impl_;

        // The leaf that always pushes is pushed.
        ++expected_push_properties_grandchild_impl_;
        break;
      case 14:
        // Second child is removed from tree. Don't discard counts because
        // they are added back before commit.

        // The leaf that always pushes is pushed.
        ++expected_push_properties_grandchild_impl_;

        // Second child added back.
        ++expected_push_properties_child2_impl_;
        ++expected_push_properties_grandchild2_impl_;

        break;
      case 15:
        // The position of this child was changed.
        ++expected_push_properties_grandchild2_impl_;

        // The leaf that always pushes is pushed.
        ++expected_push_properties_grandchild_impl_;
        break;
      case 16:
        // Second child is invalidated with SetNeedsDisplay
        ++expected_push_properties_child2_impl_;

        // The leaf that always pushed is pushed.
        ++expected_push_properties_grandchild_impl_;
        break;
    }

    PushPropertiesCountingLayerImpl* root_impl_ = NULL;
    PushPropertiesCountingLayerImpl* child_impl_ = NULL;
    PushPropertiesCountingLayerImpl* child2_impl_ = NULL;
    PushPropertiesCountingLayerImpl* grandchild_impl_ = NULL;
    PushPropertiesCountingLayerImpl* leaf_always_pushing_layer_impl_ = NULL;

    // Pull the layers that we need from the tree assuming the same structure
    // as LayerTreeHostTestLayersPushProperties
    root_impl_ = static_cast<PushPropertiesCountingLayerImpl*>(
        host_impl->RootLayer());

    if (root_impl_ && root_impl_->children().size() > 0) {
      child_impl_ = static_cast<PushPropertiesCountingLayerImpl*>(
          root_impl_->children()[0]);

      if (child_impl_ && child_impl_->children().size() > 0)
        grandchild_impl_ = static_cast<PushPropertiesCountingLayerImpl*>(
            child_impl_->children()[0]);
    }

    if (root_impl_ && root_impl_->children().size() > 1) {
      child2_impl_ = static_cast<PushPropertiesCountingLayerImpl*>(
          root_impl_->children()[1]);

      if (child2_impl_ && child2_impl_->children().size() > 0)
        leaf_always_pushing_layer_impl_ =
            static_cast<PushPropertiesCountingLayerImpl*>(
                child2_impl_->children()[0]);
    }

    if (root_impl_)
      EXPECT_EQ(expected_push_properties_root_impl_,
                root_impl_->push_properties_count());
    if (child_impl_)
      EXPECT_EQ(expected_push_properties_child_impl_,
                child_impl_->push_properties_count());
    if (grandchild_impl_)
      EXPECT_EQ(expected_push_properties_grandchild_impl_,
                grandchild_impl_->push_properties_count());
    if (child2_impl_)
      EXPECT_EQ(expected_push_properties_child2_impl_,
                child2_impl_->push_properties_count());
    if (leaf_always_pushing_layer_impl_)
      EXPECT_EQ(expected_push_properties_grandchild2_impl_,
                leaf_always_pushing_layer_impl_->push_properties_count());
  }

  size_t expected_push_properties_root_impl_;
  size_t expected_push_properties_child_impl_;
  size_t expected_push_properties_child2_impl_;
  size_t expected_push_properties_grandchild_impl_;
  size_t expected_push_properties_grandchild2_impl_;
};

TEST_F(LayerTreeHostTestImplLayersPushProperties, DelegatingRenderer) {
  RunTestWithImplSidePainting();
}

class LayerTreeHostTestPropertyChangesDuringUpdateArePushed
    : public LayerTreeHostTest {
 protected:
  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void SetupTree() override {
    root_ = Layer::Create();
    root_->CreateRenderSurface();
    root_->SetBounds(gfx::Size(1, 1));

    bool paint_scrollbar = true;
    bool has_thumb = false;
    scrollbar_layer_ = FakePaintedScrollbarLayer::Create(
        paint_scrollbar, has_thumb, root_->id());

    root_->AddChild(scrollbar_layer_);

    layer_tree_host()->SetRootLayer(root_);
    LayerTreeHostTest::SetupTree();
  }

  void DidCommitAndDrawFrame() override {
    switch (layer_tree_host()->source_frame_number()) {
      case 0:
        break;
      case 1: {
        // During update, the ignore_set_needs_commit_ bit is set to true to
        // avoid causing a second commit to be scheduled. If a property change
        // is made during this, however, it needs to be pushed in the upcoming
        // commit.
        scoped_ptr<base::AutoReset<bool>> ignore =
            scrollbar_layer_->IgnoreSetNeedsCommit();

        scrollbar_layer_->SetBounds(gfx::Size(30, 30));

        EXPECT_TRUE(scrollbar_layer_->needs_push_properties());
        EXPECT_TRUE(root_->descendant_needs_push_properties());
        layer_tree_host()->SetNeedsCommit();

        scrollbar_layer_->reset_push_properties_count();
        EXPECT_EQ(0u, scrollbar_layer_->push_properties_count());
        break;
      }
      case 2:
        EXPECT_EQ(1u, scrollbar_layer_->push_properties_count());
        EndTest();
        break;
    }
  }

  void AfterTest() override {}

  scoped_refptr<Layer> root_;
  scoped_refptr<FakePaintedScrollbarLayer> scrollbar_layer_;
};

MULTI_THREAD_TEST_F(LayerTreeHostTestPropertyChangesDuringUpdateArePushed);

class LayerTreeHostTestSetDrawableCausesCommit : public LayerTreeHostTest {
 protected:
  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void SetupTree() override {
    root_ = PushPropertiesCountingLayer::Create();
    root_->CreateRenderSurface();
    child_ = PushPropertiesCountingLayer::Create();
    root_->AddChild(child_);

    layer_tree_host()->SetRootLayer(root_);
    LayerTreeHostTest::SetupTree();
  }

  void DidCommitAndDrawFrame() override {
    switch (layer_tree_host()->source_frame_number()) {
      case 0:
        break;
      case 1: {
        // During update, the ignore_set_needs_commit_ bit is set to true to
        // avoid causing a second commit to be scheduled. If a property change
        // is made during this, however, it needs to be pushed in the upcoming
        // commit.
        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_FALSE(child_->needs_push_properties());
        EXPECT_EQ(0, root_->NumDescendantsThatDrawContent());
        root_->reset_push_properties_count();
        child_->reset_push_properties_count();
        child_->SetDrawsContent(true);
        EXPECT_EQ(1, root_->NumDescendantsThatDrawContent());
        EXPECT_EQ(0u, root_->push_properties_count());
        EXPECT_EQ(0u, child_->push_properties_count());
        EXPECT_TRUE(root_->needs_push_properties());
        EXPECT_TRUE(child_->needs_push_properties());
        break;
      }
      case 2:
        EXPECT_EQ(1u, root_->push_properties_count());
        EXPECT_EQ(1u, child_->push_properties_count());
        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_FALSE(child_->needs_push_properties());
        EndTest();
        break;
    }
  }

  void AfterTest() override {}

  scoped_refptr<PushPropertiesCountingLayer> root_;
  scoped_refptr<PushPropertiesCountingLayer> child_;
};

MULTI_THREAD_TEST_F(LayerTreeHostTestSetDrawableCausesCommit);

class LayerTreeHostTestCasePushPropertiesThreeGrandChildren
    : public LayerTreeHostTest {
 protected:
  void BeginTest() override {
    expected_push_properties_root_ = 0;
    expected_push_properties_child_ = 0;
    expected_push_properties_grandchild1_ = 0;
    expected_push_properties_grandchild2_ = 0;
    expected_push_properties_grandchild3_ = 0;
    PostSetNeedsCommitToMainThread();
  }

  void SetupTree() override {
    root_ = PushPropertiesCountingLayer::Create();
    root_->CreateRenderSurface();
    child_ = PushPropertiesCountingLayer::Create();
    grandchild1_ = PushPropertiesCountingLayer::Create();
    grandchild2_ = PushPropertiesCountingLayer::Create();
    grandchild3_ = PushPropertiesCountingLayer::Create();

    root_->AddChild(child_);
    child_->AddChild(grandchild1_);
    child_->AddChild(grandchild2_);
    child_->AddChild(grandchild3_);

    // Don't set the root layer here.
    LayerTreeHostTest::SetupTree();
  }

  void AfterTest() override {}

  FakeContentLayerClient client_;
  scoped_refptr<PushPropertiesCountingLayer> root_;
  scoped_refptr<PushPropertiesCountingLayer> child_;
  scoped_refptr<PushPropertiesCountingLayer> grandchild1_;
  scoped_refptr<PushPropertiesCountingLayer> grandchild2_;
  scoped_refptr<PushPropertiesCountingLayer> grandchild3_;
  size_t expected_push_properties_root_;
  size_t expected_push_properties_child_;
  size_t expected_push_properties_grandchild1_;
  size_t expected_push_properties_grandchild2_;
  size_t expected_push_properties_grandchild3_;
};

class LayerTreeHostTestPushPropertiesAddingToTreeRequiresPush
    : public LayerTreeHostTestCasePushPropertiesThreeGrandChildren {
 protected:
  void DidCommitAndDrawFrame() override {
    int last_source_frame_number = layer_tree_host()->source_frame_number() - 1;
    switch (last_source_frame_number) {
      case 0:
        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_FALSE(root_->descendant_needs_push_properties());
        EXPECT_FALSE(child_->needs_push_properties());
        EXPECT_FALSE(child_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild1_->needs_push_properties());
        EXPECT_FALSE(grandchild1_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild2_->needs_push_properties());
        EXPECT_FALSE(grandchild2_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild3_->needs_push_properties());
        EXPECT_FALSE(grandchild3_->descendant_needs_push_properties());

        layer_tree_host()->SetRootLayer(root_);

        EXPECT_TRUE(root_->needs_push_properties());
        EXPECT_TRUE(root_->descendant_needs_push_properties());
        EXPECT_TRUE(child_->needs_push_properties());
        EXPECT_TRUE(child_->descendant_needs_push_properties());
        EXPECT_TRUE(grandchild1_->needs_push_properties());
        EXPECT_FALSE(grandchild1_->descendant_needs_push_properties());
        EXPECT_TRUE(grandchild2_->needs_push_properties());
        EXPECT_FALSE(grandchild2_->descendant_needs_push_properties());
        EXPECT_TRUE(grandchild3_->needs_push_properties());
        EXPECT_FALSE(grandchild3_->descendant_needs_push_properties());
        break;
      case 1:
        EndTest();
        break;
    }
  }
};

MULTI_THREAD_TEST_F(LayerTreeHostTestPushPropertiesAddingToTreeRequiresPush);

class LayerTreeHostTestPushPropertiesRemovingChildStopsRecursion
    : public LayerTreeHostTestCasePushPropertiesThreeGrandChildren {
 protected:
  void DidCommitAndDrawFrame() override {
    int last_source_frame_number = layer_tree_host()->source_frame_number() - 1;
    switch (last_source_frame_number) {
      case 0:
        layer_tree_host()->SetRootLayer(root_);
        break;
      case 1:
        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_FALSE(root_->descendant_needs_push_properties());
        EXPECT_FALSE(child_->needs_push_properties());
        EXPECT_FALSE(child_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild1_->needs_push_properties());
        EXPECT_FALSE(grandchild1_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild2_->needs_push_properties());
        EXPECT_FALSE(grandchild2_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild3_->needs_push_properties());
        EXPECT_FALSE(grandchild3_->descendant_needs_push_properties());

        grandchild1_->RemoveFromParent();
        grandchild1_->SetPosition(gfx::Point(1, 1));

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_FALSE(root_->descendant_needs_push_properties());
        EXPECT_FALSE(child_->needs_push_properties());
        EXPECT_FALSE(child_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild2_->needs_push_properties());
        EXPECT_FALSE(grandchild2_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild3_->needs_push_properties());
        EXPECT_FALSE(grandchild3_->descendant_needs_push_properties());

        child_->AddChild(grandchild1_);

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_TRUE(root_->descendant_needs_push_properties());
        EXPECT_FALSE(child_->needs_push_properties());
        EXPECT_TRUE(child_->descendant_needs_push_properties());
        EXPECT_TRUE(grandchild1_->needs_push_properties());
        EXPECT_FALSE(grandchild1_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild2_->needs_push_properties());
        EXPECT_FALSE(grandchild2_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild3_->needs_push_properties());
        EXPECT_FALSE(grandchild3_->descendant_needs_push_properties());

        grandchild2_->SetPosition(gfx::Point(1, 1));

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_TRUE(root_->descendant_needs_push_properties());
        EXPECT_FALSE(child_->needs_push_properties());
        EXPECT_TRUE(child_->descendant_needs_push_properties());
        EXPECT_TRUE(grandchild1_->needs_push_properties());
        EXPECT_FALSE(grandchild1_->descendant_needs_push_properties());
        EXPECT_TRUE(grandchild2_->needs_push_properties());
        EXPECT_FALSE(grandchild2_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild3_->needs_push_properties());
        EXPECT_FALSE(grandchild3_->descendant_needs_push_properties());

        // grandchild2_ will still need a push properties.
        grandchild1_->RemoveFromParent();

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_TRUE(root_->descendant_needs_push_properties());
        EXPECT_FALSE(child_->needs_push_properties());
        EXPECT_TRUE(child_->descendant_needs_push_properties());

        // grandchild3_ does not need a push properties, so recursing should
        // no longer be needed.
        grandchild2_->RemoveFromParent();

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_FALSE(root_->descendant_needs_push_properties());
        EXPECT_FALSE(child_->needs_push_properties());
        EXPECT_FALSE(child_->descendant_needs_push_properties());
        EndTest();
        break;
    }
  }
};

MULTI_THREAD_TEST_F(LayerTreeHostTestPushPropertiesRemovingChildStopsRecursion);

class LayerTreeHostTestPushPropertiesRemovingChildStopsRecursionWithPersistence
    : public LayerTreeHostTestCasePushPropertiesThreeGrandChildren {
 protected:
  void DidCommitAndDrawFrame() override {
    int last_source_frame_number = layer_tree_host()->source_frame_number() - 1;
    switch (last_source_frame_number) {
      case 0:
        layer_tree_host()->SetRootLayer(root_);
        grandchild1_->set_persist_needs_push_properties(true);
        grandchild2_->set_persist_needs_push_properties(true);
        break;
      case 1:
        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_TRUE(root_->descendant_needs_push_properties());
        EXPECT_FALSE(child_->needs_push_properties());
        EXPECT_TRUE(child_->descendant_needs_push_properties());
        EXPECT_TRUE(grandchild1_->needs_push_properties());
        EXPECT_FALSE(grandchild1_->descendant_needs_push_properties());
        EXPECT_TRUE(grandchild2_->needs_push_properties());
        EXPECT_FALSE(grandchild2_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild3_->needs_push_properties());
        EXPECT_FALSE(grandchild3_->descendant_needs_push_properties());

        // grandchild2_ will still need a push properties.
        grandchild1_->RemoveFromParent();

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_TRUE(root_->descendant_needs_push_properties());
        EXPECT_FALSE(child_->needs_push_properties());
        EXPECT_TRUE(child_->descendant_needs_push_properties());

        // grandchild3_ does not need a push properties, so recursing should
        // no longer be needed.
        grandchild2_->RemoveFromParent();

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_FALSE(root_->descendant_needs_push_properties());
        EXPECT_FALSE(child_->needs_push_properties());
        EXPECT_FALSE(child_->descendant_needs_push_properties());
        EndTest();
        break;
    }
  }
};

MULTI_THREAD_TEST_F(
    LayerTreeHostTestPushPropertiesRemovingChildStopsRecursionWithPersistence);

class LayerTreeHostTestPushPropertiesSetPropertiesWhileOutsideTree
    : public LayerTreeHostTestCasePushPropertiesThreeGrandChildren {
 protected:
  void DidCommitAndDrawFrame() override {
    int last_source_frame_number = layer_tree_host()->source_frame_number() - 1;
    switch (last_source_frame_number) {
      case 0:
        layer_tree_host()->SetRootLayer(root_);
        break;
      case 1:
        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_FALSE(root_->descendant_needs_push_properties());
        EXPECT_FALSE(child_->needs_push_properties());
        EXPECT_FALSE(child_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild1_->needs_push_properties());
        EXPECT_FALSE(grandchild1_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild2_->needs_push_properties());
        EXPECT_FALSE(grandchild2_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild3_->needs_push_properties());
        EXPECT_FALSE(grandchild3_->descendant_needs_push_properties());

        // Change grandchildren while their parent is not in the tree.
        child_->RemoveFromParent();
        grandchild1_->SetPosition(gfx::Point(1, 1));
        grandchild2_->SetPosition(gfx::Point(1, 1));
        root_->AddChild(child_);

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_TRUE(root_->descendant_needs_push_properties());
        EXPECT_TRUE(child_->needs_push_properties());
        EXPECT_TRUE(child_->descendant_needs_push_properties());
        EXPECT_TRUE(grandchild1_->needs_push_properties());
        EXPECT_FALSE(grandchild1_->descendant_needs_push_properties());
        EXPECT_TRUE(grandchild2_->needs_push_properties());
        EXPECT_FALSE(grandchild2_->descendant_needs_push_properties());
        EXPECT_TRUE(grandchild3_->needs_push_properties());
        EXPECT_FALSE(grandchild3_->descendant_needs_push_properties());

        grandchild1_->RemoveFromParent();

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_TRUE(root_->descendant_needs_push_properties());
        EXPECT_TRUE(child_->needs_push_properties());
        EXPECT_TRUE(child_->descendant_needs_push_properties());

        grandchild2_->RemoveFromParent();

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_TRUE(root_->descendant_needs_push_properties());
        EXPECT_TRUE(child_->needs_push_properties());
        EXPECT_TRUE(child_->descendant_needs_push_properties());

        grandchild3_->RemoveFromParent();

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_TRUE(root_->descendant_needs_push_properties());
        EXPECT_TRUE(child_->needs_push_properties());
        EXPECT_FALSE(child_->descendant_needs_push_properties());

        EndTest();
        break;
    }
  }
};

MULTI_THREAD_TEST_F(
    LayerTreeHostTestPushPropertiesSetPropertiesWhileOutsideTree);

class LayerTreeHostTestPushPropertiesSetPropertyInParentThenChild
    : public LayerTreeHostTestCasePushPropertiesThreeGrandChildren {
 protected:
  void DidCommitAndDrawFrame() override {
    int last_source_frame_number = layer_tree_host()->source_frame_number() - 1;
    switch (last_source_frame_number) {
      case 0:
        layer_tree_host()->SetRootLayer(root_);
        break;
      case 1:
        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_FALSE(root_->descendant_needs_push_properties());
        EXPECT_FALSE(child_->needs_push_properties());
        EXPECT_FALSE(child_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild1_->needs_push_properties());
        EXPECT_FALSE(grandchild1_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild2_->needs_push_properties());
        EXPECT_FALSE(grandchild2_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild3_->needs_push_properties());
        EXPECT_FALSE(grandchild3_->descendant_needs_push_properties());

        child_->SetPosition(gfx::Point(1, 1));
        grandchild1_->SetPosition(gfx::Point(1, 1));
        grandchild2_->SetPosition(gfx::Point(1, 1));

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_TRUE(root_->descendant_needs_push_properties());
        EXPECT_TRUE(child_->needs_push_properties());
        EXPECT_TRUE(child_->descendant_needs_push_properties());
        EXPECT_TRUE(grandchild1_->needs_push_properties());
        EXPECT_FALSE(grandchild1_->descendant_needs_push_properties());
        EXPECT_TRUE(grandchild2_->needs_push_properties());
        EXPECT_FALSE(grandchild2_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild3_->needs_push_properties());
        EXPECT_FALSE(grandchild3_->descendant_needs_push_properties());

        grandchild1_->RemoveFromParent();

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_TRUE(root_->descendant_needs_push_properties());
        EXPECT_TRUE(child_->needs_push_properties());
        EXPECT_TRUE(child_->descendant_needs_push_properties());

        grandchild2_->RemoveFromParent();

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_TRUE(root_->descendant_needs_push_properties());
        EXPECT_TRUE(child_->needs_push_properties());
        EXPECT_FALSE(child_->descendant_needs_push_properties());

        child_->RemoveFromParent();

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_FALSE(root_->descendant_needs_push_properties());

        EndTest();
        break;
    }
  }
};

MULTI_THREAD_TEST_F(
    LayerTreeHostTestPushPropertiesSetPropertyInParentThenChild);

class LayerTreeHostTestPushPropertiesSetPropertyInChildThenParent
    : public LayerTreeHostTestCasePushPropertiesThreeGrandChildren {
 protected:
  void DidCommitAndDrawFrame() override {
    int last_source_frame_number = layer_tree_host()->source_frame_number() - 1;
    switch (last_source_frame_number) {
      case 0:
        layer_tree_host()->SetRootLayer(root_);
        break;
      case 1:
        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_FALSE(root_->descendant_needs_push_properties());
        EXPECT_FALSE(child_->needs_push_properties());
        EXPECT_FALSE(child_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild1_->needs_push_properties());
        EXPECT_FALSE(grandchild1_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild2_->needs_push_properties());
        EXPECT_FALSE(grandchild2_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild3_->needs_push_properties());
        EXPECT_FALSE(grandchild3_->descendant_needs_push_properties());

        grandchild1_->SetPosition(gfx::Point(1, 1));
        grandchild2_->SetPosition(gfx::Point(1, 1));
        child_->SetPosition(gfx::Point(1, 1));

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_TRUE(root_->descendant_needs_push_properties());
        EXPECT_TRUE(child_->needs_push_properties());
        EXPECT_TRUE(child_->descendant_needs_push_properties());
        EXPECT_TRUE(grandchild1_->needs_push_properties());
        EXPECT_FALSE(grandchild1_->descendant_needs_push_properties());
        EXPECT_TRUE(grandchild2_->needs_push_properties());
        EXPECT_FALSE(grandchild2_->descendant_needs_push_properties());
        EXPECT_FALSE(grandchild3_->needs_push_properties());
        EXPECT_FALSE(grandchild3_->descendant_needs_push_properties());

        grandchild1_->RemoveFromParent();

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_TRUE(root_->descendant_needs_push_properties());
        EXPECT_TRUE(child_->needs_push_properties());
        EXPECT_TRUE(child_->descendant_needs_push_properties());

        grandchild2_->RemoveFromParent();

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_TRUE(root_->descendant_needs_push_properties());
        EXPECT_TRUE(child_->needs_push_properties());
        EXPECT_FALSE(child_->descendant_needs_push_properties());

        child_->RemoveFromParent();

        EXPECT_FALSE(root_->needs_push_properties());
        EXPECT_FALSE(root_->descendant_needs_push_properties());

        EndTest();
        break;
    }
  }
};

MULTI_THREAD_TEST_F(
    LayerTreeHostTestPushPropertiesSetPropertyInChildThenParent);

// This test verifies that the tree activation callback is invoked correctly.
class LayerTreeHostTestTreeActivationCallback : public LayerTreeHostTest {
 public:
  LayerTreeHostTestTreeActivationCallback()
      : num_commits_(0), callback_count_(0) {}

  void BeginTest() override {
    EXPECT_TRUE(HasImplThread());
    PostSetNeedsCommitToMainThread();
  }

  DrawResult PrepareToDrawOnThread(LayerTreeHostImpl* host_impl,
                                   LayerTreeHostImpl::FrameData* frame_data,
                                   DrawResult draw_result) override {
    ++num_commits_;
    switch (num_commits_) {
      case 1:
        EXPECT_EQ(0, callback_count_);
        callback_count_ = 0;
        SetCallback(true);
        PostSetNeedsCommitToMainThread();
        break;
      case 2:
        EXPECT_EQ(1, callback_count_);
        callback_count_ = 0;
        SetCallback(false);
        PostSetNeedsCommitToMainThread();
        break;
      case 3:
        EXPECT_EQ(0, callback_count_);
        callback_count_ = 0;
        EndTest();
        break;
      default:
        ADD_FAILURE() << num_commits_;
        EndTest();
        break;
    }
    return LayerTreeHostTest::PrepareToDrawOnThread(
        host_impl, frame_data, draw_result);
  }

  void AfterTest() override { EXPECT_EQ(3, num_commits_); }

  void SetCallback(bool enable) {
    output_surface()->SetTreeActivationCallback(
        enable
            ? base::Bind(
                  &LayerTreeHostTestTreeActivationCallback::ActivationCallback,
                  base::Unretained(this))
            : base::Closure());
  }

  void ActivationCallback() { ++callback_count_; }

  int num_commits_;
  int callback_count_;
};

TEST_F(LayerTreeHostTestTreeActivationCallback, DirectRenderer) {
  RunTest(true, false, true);
}

TEST_F(LayerTreeHostTestTreeActivationCallback, DelegatingRenderer) {
  RunTest(true, true, true);
}

class LayerInvalidateCausesDraw : public LayerTreeHostTest {
 public:
  LayerInvalidateCausesDraw() : num_commits_(0), num_draws_(0) {}

  void BeginTest() override {
    ASSERT_TRUE(!!invalidate_layer_.get())
        << "Derived tests must set this in SetupTree";

    // One initial commit.
    PostSetNeedsCommitToMainThread();
  }

  void DidCommitAndDrawFrame() override {
    // After commit, invalidate the layer.  This should cause a commit.
    if (layer_tree_host()->source_frame_number() == 1)
      invalidate_layer_->SetNeedsDisplay();
  }

  void DrawLayersOnThread(LayerTreeHostImpl* impl) override {
    num_draws_++;
    if (impl->active_tree()->source_frame_number() == 1)
      EndTest();
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* impl) override {
    num_commits_++;
  }

  void AfterTest() override {
    EXPECT_GE(2, num_commits_);
    EXPECT_GE(2, num_draws_);
  }

 protected:
  scoped_refptr<Layer> invalidate_layer_;

 private:
  int num_commits_;
  int num_draws_;
};

// VideoLayer must support being invalidated and then passing that along
// to the compositor thread, even though no resources are updated in
// response to that invalidation.
class LayerTreeHostTestVideoLayerInvalidate : public LayerInvalidateCausesDraw {
 public:
  void SetupTree() override {
    LayerTreeHostTest::SetupTree();
    scoped_refptr<VideoLayer> video_layer =
        VideoLayer::Create(&provider_, media::VIDEO_ROTATION_0);
    video_layer->SetBounds(gfx::Size(10, 10));
    video_layer->SetIsDrawable(true);
    layer_tree_host()->root_layer()->AddChild(video_layer);

    invalidate_layer_ = video_layer;
  }

 private:
  FakeVideoFrameProvider provider_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(LayerTreeHostTestVideoLayerInvalidate);

// IOSurfaceLayer must support being invalidated and then passing that along
// to the compositor thread, even though no resources are updated in
// response to that invalidation.
class LayerTreeHostTestIOSurfaceLayerInvalidate
    : public LayerInvalidateCausesDraw {
 public:
  void SetupTree() override {
    LayerTreeHostTest::SetupTree();
    scoped_refptr<IOSurfaceLayer> layer = IOSurfaceLayer::Create();
    layer->SetBounds(gfx::Size(10, 10));
    uint32_t fake_io_surface_id = 7;
    layer->SetIOSurfaceProperties(fake_io_surface_id, layer->bounds());
    layer->SetIsDrawable(true);
    layer_tree_host()->root_layer()->AddChild(layer);

    invalidate_layer_ = layer;
  }
};

// TODO(danakj): IOSurface layer can not be transported. crbug.com/239335
SINGLE_AND_MULTI_THREAD_DIRECT_RENDERER_TEST_F(
    LayerTreeHostTestIOSurfaceLayerInvalidate);

class LayerTreeHostTestPushHiddenLayer : public LayerTreeHostTest {
 protected:
  void SetupTree() override {
    root_layer_ = Layer::Create();
    root_layer_->CreateRenderSurface();
    root_layer_->SetPosition(gfx::Point());
    root_layer_->SetBounds(gfx::Size(10, 10));

    parent_layer_ = SolidColorLayer::Create();
    parent_layer_->SetPosition(gfx::Point());
    parent_layer_->SetBounds(gfx::Size(10, 10));
    parent_layer_->SetIsDrawable(true);
    root_layer_->AddChild(parent_layer_);

    child_layer_ = SolidColorLayer::Create();
    child_layer_->SetPosition(gfx::Point());
    child_layer_->SetBounds(gfx::Size(10, 10));
    child_layer_->SetIsDrawable(true);
    parent_layer_->AddChild(child_layer_);

    layer_tree_host()->SetRootLayer(root_layer_);
    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DidCommitAndDrawFrame() override {
    switch (layer_tree_host()->source_frame_number()) {
      case 1:
        // The layer type used does not need to push properties every frame.
        EXPECT_FALSE(child_layer_->needs_push_properties());

        // Change the bounds of the child layer, but make it skipped
        // by CalculateDrawProperties.
        parent_layer_->SetOpacity(0.f);
        child_layer_->SetBounds(gfx::Size(5, 5));
        break;
      case 2:
        // The bounds of the child layer were pushed to the impl side.
        EXPECT_FALSE(child_layer_->needs_push_properties());

        EndTest();
        break;
    }
  }

  void DidActivateTreeOnThread(LayerTreeHostImpl* impl) override {
    LayerImpl* root = impl->active_tree()->root_layer();
    LayerImpl* parent = root->children()[0];
    LayerImpl* child = parent->children()[0];

    switch (impl->active_tree()->source_frame_number()) {
      case 1:
        EXPECT_EQ(gfx::Size(5, 5).ToString(), child->bounds().ToString());
        break;
    }
  }

  void AfterTest() override {}

  scoped_refptr<Layer> root_layer_;
  scoped_refptr<SolidColorLayer> parent_layer_;
  scoped_refptr<SolidColorLayer> child_layer_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(LayerTreeHostTestPushHiddenLayer);

class LayerTreeHostTestUpdateLayerInEmptyViewport : public LayerTreeHostTest {
 protected:
  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->impl_side_painting = true;
  }

  void SetupTree() override {
    root_layer_ = FakePictureLayer::Create(&client_);
    root_layer_->SetBounds(gfx::Size(10, 10));

    layer_tree_host()->SetRootLayer(root_layer_);
    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override {
    // The viewport is empty, but we still need to update layers on the main
    // thread.
    layer_tree_host()->SetViewportSize(gfx::Size(0, 0));
    PostSetNeedsCommitToMainThread();
  }

  void DidCommit() override {
    // The layer should be updated even though the viewport is empty, so we
    // are capable of drawing it on the impl tree.
    EXPECT_GT(root_layer_->update_count(), 0u);
    EndTest();
  }

  void AfterTest() override {}

  FakeContentLayerClient client_;
  scoped_refptr<FakePictureLayer> root_layer_;
};

MULTI_THREAD_TEST_F(LayerTreeHostTestUpdateLayerInEmptyViewport);

class LayerTreeHostTestAbortEvictedTextures : public LayerTreeHostTest {
 public:
  LayerTreeHostTestAbortEvictedTextures()
      : num_will_begin_main_frames_(0), num_impl_commits_(0) {}

 protected:
  void SetupTree() override {
    scoped_refptr<SolidColorLayer> root_layer = SolidColorLayer::Create();
    root_layer->SetBounds(gfx::Size(200, 200));
    root_layer->SetIsDrawable(true);
    root_layer->CreateRenderSurface();

    layer_tree_host()->SetRootLayer(root_layer);
    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void WillBeginMainFrame() override {
    num_will_begin_main_frames_++;
    switch (num_will_begin_main_frames_) {
      case 2:
        // Send a redraw to the compositor thread.  This will (wrongly) be
        // ignored unless aborting resets the texture state.
        layer_tree_host()->SetNeedsRedraw();
        break;
    }
  }

  void BeginCommitOnThread(LayerTreeHostImpl* impl) override {
    num_impl_commits_++;
  }

  void DrawLayersOnThread(LayerTreeHostImpl* impl) override {
    switch (impl->SourceAnimationFrameNumber()) {
      case 1:
        // Prevent draws until commit.
        impl->active_tree()->SetContentsTexturesPurged();
        EXPECT_FALSE(impl->CanDraw());
        // Trigger an abortable commit.
        impl->SetNeedsCommit();
        break;
      case 2:
        EndTest();
        break;
    }
  }

  void AfterTest() override {
    // Ensure that the commit was truly aborted.
    EXPECT_EQ(2, num_will_begin_main_frames_);
    EXPECT_EQ(1, num_impl_commits_);
  }

 private:
  int num_will_begin_main_frames_;
  int num_impl_commits_;
};

// Commits can only be aborted when using the thread proxy.
MULTI_THREAD_TEST_F(LayerTreeHostTestAbortEvictedTextures);

class LayerTreeHostTestMaxTransferBufferUsageBytes : public LayerTreeHostTest {
 protected:
  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->impl_side_painting = true;
    settings->use_zero_copy = false;
    settings->use_one_copy = false;
  }

  scoped_ptr<FakeOutputSurface> CreateFakeOutputSurface() override {
    scoped_refptr<TestContextProvider> context_provider =
        TestContextProvider::Create();
    context_provider->SetMaxTransferBufferUsageBytes(512 * 512);
    if (delegating_renderer())
      return FakeOutputSurface::CreateDelegating3d(context_provider);
    else
      return FakeOutputSurface::Create3d(context_provider);
  }

  void SetupTree() override {
    client_.set_fill_with_nonsolid_color(true);
    scoped_refptr<FakePictureLayer> root_layer =
        FakePictureLayer::Create(&client_);
    root_layer->SetBounds(gfx::Size(1024, 1024));
    root_layer->SetIsDrawable(true);

    layer_tree_host()->SetRootLayer(root_layer);
    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DidActivateTreeOnThread(LayerTreeHostImpl* impl) override {
    TestWebGraphicsContext3D* context = TestContext();

    // Expect that the transfer buffer memory used is equal to the
    // MaxTransferBufferUsageBytes value set in CreateOutputSurface.
    EXPECT_EQ(512 * 512u, context->max_used_transfer_buffer_usage_bytes());
    EndTest();
  }

  void AfterTest() override {}

 private:
  FakeContentLayerClient client_;
};

// Impl-side painting is a multi-threaded compositor feature.
MULTI_THREAD_TEST_F(LayerTreeHostTestMaxTransferBufferUsageBytes);

// Test ensuring that memory limits are sent to the prioritized resource
// manager.
class LayerTreeHostTestMemoryLimits : public LayerTreeHostTest {
 public:
  LayerTreeHostTestMemoryLimits() : num_commits_(0) {}

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void WillCommit() override {
    // Some commits are aborted, so increment number of attempted commits here.
    num_commits_++;
  }

  void DidCommit() override {
    switch (num_commits_) {
      case 1:
        // Verify default values.
        EXPECT_EQ(PrioritizedResourceManager::DefaultMemoryAllocationLimit(),
                  layer_tree_host()
                      ->contents_texture_manager()
                      ->MaxMemoryLimitBytes());
        EXPECT_EQ(PriorityCalculator::AllowEverythingCutoff(),
                  layer_tree_host()
                      ->contents_texture_manager()
                      ->ExternalPriorityCutoff());
        PostSetNeedsCommitToMainThread();
        break;
      case 2:
        // The values should remain the same until the commit after the policy
        // is changed.
        EXPECT_EQ(PrioritizedResourceManager::DefaultMemoryAllocationLimit(),
                  layer_tree_host()
                      ->contents_texture_manager()
                      ->MaxMemoryLimitBytes());
        EXPECT_EQ(PriorityCalculator::AllowEverythingCutoff(),
                  layer_tree_host()
                      ->contents_texture_manager()
                      ->ExternalPriorityCutoff());
        break;
      case 3:
        // Verify values were correctly passed.
        EXPECT_EQ(16u * 1024u * 1024u,
                  layer_tree_host()
                      ->contents_texture_manager()
                      ->MaxMemoryLimitBytes());
        EXPECT_EQ(PriorityCalculator::AllowVisibleAndNearbyCutoff(),
                  layer_tree_host()
                      ->contents_texture_manager()
                      ->ExternalPriorityCutoff());
        EndTest();
        break;
      case 4:
        // Make sure no extra commits happen.
        NOTREACHED();
        break;
    }
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* impl) override {
    switch (num_commits_) {
      case 1:
        break;
      case 2:
        // This will trigger a commit because the priority cutoff has changed.
        impl->SetMemoryPolicy(ManagedMemoryPolicy(
            16u * 1024u * 1024u,
            gpu::MemoryAllocation::CUTOFF_ALLOW_NICE_TO_HAVE,
            1000));
        break;
      case 3:
        // This will not trigger a commit because the priority cutoff has not
        // changed, and there is already enough memory for all allocations.
        impl->SetMemoryPolicy(ManagedMemoryPolicy(
            32u * 1024u * 1024u,
            gpu::MemoryAllocation::CUTOFF_ALLOW_NICE_TO_HAVE,
            1000));
        break;
      case 4:
        NOTREACHED();
        break;
    }
  }

  void AfterTest() override {}

 private:
  int num_commits_;
};

SINGLE_AND_MULTI_THREAD_NOIMPL_TEST_F(LayerTreeHostTestMemoryLimits);

}  // namespace

class LayerTreeHostTestSetMemoryPolicyOnLostOutputSurface
    : public LayerTreeHostTest {
 protected:
  LayerTreeHostTestSetMemoryPolicyOnLostOutputSurface()
      : first_output_surface_memory_limit_(4321234),
        second_output_surface_memory_limit_(1234321) {}

  scoped_ptr<FakeOutputSurface> CreateFakeOutputSurface() override {
    if (!first_context_provider_.get()) {
      first_context_provider_ = TestContextProvider::Create();
    } else {
      EXPECT_FALSE(second_context_provider_.get());
      second_context_provider_ = TestContextProvider::Create();
    }

    scoped_refptr<TestContextProvider> provider(second_context_provider_.get()
                                                    ? second_context_provider_
                                                    : first_context_provider_);
    scoped_ptr<FakeOutputSurface> output_surface;
    if (delegating_renderer())
      output_surface = FakeOutputSurface::CreateDelegating3d(provider);
    else
      output_surface = FakeOutputSurface::Create3d(provider);
    output_surface->SetMemoryPolicyToSetAtBind(
        make_scoped_ptr(new ManagedMemoryPolicy(
            second_context_provider_.get() ? second_output_surface_memory_limit_
                                           : first_output_surface_memory_limit_,
            gpu::MemoryAllocation::CUTOFF_ALLOW_NICE_TO_HAVE,
            ManagedMemoryPolicy::kDefaultNumResourcesLimit)));
    return output_surface.Pass();
  }

  void SetupTree() override {
    if (layer_tree_host()->settings().impl_side_painting)
      root_ = FakePictureLayer::Create(&client_);
    else
      root_ = FakeContentLayer::Create(&client_);
    root_->SetBounds(gfx::Size(20, 20));
    layer_tree_host()->SetRootLayer(root_);
    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DidCommitAndDrawFrame() override {
    // Lost context sometimes takes two frames to recreate. The third frame
    // is sometimes aborted, so wait until the fourth frame to verify that
    // the memory has been set, and the fifth frame to end the test.
    if (layer_tree_host()->source_frame_number() < 5) {
      layer_tree_host()->SetNeedsCommit();
    } else if (layer_tree_host()->source_frame_number() == 5) {
      EndTest();
    }
  }

  void SwapBuffersOnThread(LayerTreeHostImpl* impl, bool result) override {
    switch (impl->active_tree()->source_frame_number()) {
      case 1:
        EXPECT_EQ(first_output_surface_memory_limit_,
                  impl->memory_allocation_limit_bytes());
        // Lose the output surface.
        first_context_provider_->TestContext3d()->loseContextCHROMIUM(
            GL_GUILTY_CONTEXT_RESET_ARB, GL_INNOCENT_CONTEXT_RESET_ARB);
        break;
      case 4:
        EXPECT_EQ(second_output_surface_memory_limit_,
                  impl->memory_allocation_limit_bytes());
        break;
    }
  }

  void AfterTest() override {}

  scoped_refptr<TestContextProvider> first_context_provider_;
  scoped_refptr<TestContextProvider> second_context_provider_;
  size_t first_output_surface_memory_limit_;
  size_t second_output_surface_memory_limit_;
  FakeContentLayerClient client_;
  scoped_refptr<Layer> root_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(
    LayerTreeHostTestSetMemoryPolicyOnLostOutputSurface);

struct TestSwapPromiseResult {
  TestSwapPromiseResult()
      : did_swap_called(false),
        did_not_swap_called(false),
        dtor_called(false),
        reason(SwapPromise::DID_NOT_SWAP_UNKNOWN) {}

  bool did_swap_called;
  bool did_not_swap_called;
  bool dtor_called;
  SwapPromise::DidNotSwapReason reason;
  base::Lock lock;
};

class TestSwapPromise : public SwapPromise {
 public:
  explicit TestSwapPromise(TestSwapPromiseResult* result) : result_(result) {}

  ~TestSwapPromise() override {
    base::AutoLock lock(result_->lock);
    result_->dtor_called = true;
  }

  void DidSwap(CompositorFrameMetadata* metadata) override {
    base::AutoLock lock(result_->lock);
    EXPECT_FALSE(result_->did_swap_called);
    EXPECT_FALSE(result_->did_not_swap_called);
    result_->did_swap_called = true;
  }

  void DidNotSwap(DidNotSwapReason reason) override {
    base::AutoLock lock(result_->lock);
    EXPECT_FALSE(result_->did_swap_called);
    EXPECT_FALSE(result_->did_not_swap_called);
    result_->did_not_swap_called = true;
    result_->reason = reason;
  }

  int64 TraceId() const override { return 0; }

 private:
  // Not owned.
  TestSwapPromiseResult* result_;
};

class LayerTreeHostTestBreakSwapPromise : public LayerTreeHostTest {
 protected:
  LayerTreeHostTestBreakSwapPromise()
      : commit_count_(0), commit_complete_count_(0) {}

  void WillBeginMainFrame() override {
    ASSERT_LE(commit_count_, 2);
    scoped_ptr<SwapPromise> swap_promise(
        new TestSwapPromise(&swap_promise_result_[commit_count_]));
    layer_tree_host()->QueueSwapPromise(swap_promise.Pass());
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DidCommit() override {
    commit_count_++;
    if (commit_count_ == 2) {
      // This commit will finish.
      layer_tree_host()->SetNeedsCommit();
    }
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* host_impl) override {
    commit_complete_count_++;
    if (commit_complete_count_ == 1) {
      // This commit will be aborted because no actual update.
      PostSetNeedsUpdateLayersToMainThread();
    } else {
      EndTest();
    }
  }

  void AfterTest() override {
    // 3 commits are scheduled. 2 completes. 1 is aborted.
    EXPECT_EQ(commit_count_, 3);
    EXPECT_EQ(commit_complete_count_, 2);

    {
      // The first commit completes and causes swap buffer which finishes
      // the promise.
      base::AutoLock lock(swap_promise_result_[0].lock);
      EXPECT_TRUE(swap_promise_result_[0].did_swap_called);
      EXPECT_FALSE(swap_promise_result_[0].did_not_swap_called);
      EXPECT_TRUE(swap_promise_result_[0].dtor_called);
    }

    {
      // The second commit is aborted since it contains no updates.
      base::AutoLock lock(swap_promise_result_[1].lock);
      EXPECT_FALSE(swap_promise_result_[1].did_swap_called);
      EXPECT_TRUE(swap_promise_result_[1].did_not_swap_called);
      EXPECT_EQ(SwapPromise::COMMIT_NO_UPDATE, swap_promise_result_[1].reason);
      EXPECT_TRUE(swap_promise_result_[1].dtor_called);
    }

    {
      // The last commit completes but it does not cause swap buffer because
      // there is no damage in the frame data.
      base::AutoLock lock(swap_promise_result_[2].lock);
      EXPECT_FALSE(swap_promise_result_[2].did_swap_called);
      EXPECT_TRUE(swap_promise_result_[2].did_not_swap_called);
      EXPECT_EQ(SwapPromise::SWAP_FAILS, swap_promise_result_[2].reason);
      EXPECT_TRUE(swap_promise_result_[2].dtor_called);
    }
  }

  int commit_count_;
  int commit_complete_count_;
  TestSwapPromiseResult swap_promise_result_[3];
};

MULTI_THREAD_TEST_F(LayerTreeHostTestBreakSwapPromise);

class LayerTreeHostTestKeepSwapPromise : public LayerTreeTest {
 public:
  LayerTreeHostTestKeepSwapPromise() {}

  void BeginTest() override {
    layer_ = SolidColorLayer::Create();
    layer_->SetIsDrawable(true);
    layer_->SetBounds(gfx::Size(10, 10));
    layer_tree_host()->SetRootLayer(layer_);
    gfx::Size bounds(100, 100);
    layer_tree_host()->SetViewportSize(bounds);
    PostSetNeedsCommitToMainThread();
  }

  void DidCommit() override {
    MainThreadTaskRunner()->PostTask(
        FROM_HERE, base::Bind(&LayerTreeHostTestKeepSwapPromise::ChangeFrame,
                              base::Unretained(this)));
  }

  void ChangeFrame() {
    switch (layer_tree_host()->source_frame_number()) {
      case 1:
        layer_->SetBounds(gfx::Size(10, 11));
        layer_tree_host()->QueueSwapPromise(
            make_scoped_ptr(new TestSwapPromise(&swap_promise_result_)));
        break;
      case 2:
        break;
      default:
        NOTREACHED();
        break;
    }
  }

  void SwapBuffersOnThread(LayerTreeHostImpl* host_impl, bool result) override {
    EXPECT_TRUE(result);
    if (host_impl->active_tree()->source_frame_number() >= 1) {
      // The commit changes layers so it should cause a swap.
      base::AutoLock lock(swap_promise_result_.lock);
      EXPECT_TRUE(swap_promise_result_.did_swap_called);
      EXPECT_FALSE(swap_promise_result_.did_not_swap_called);
      EXPECT_TRUE(swap_promise_result_.dtor_called);
      EndTest();
    }
  }

  void AfterTest() override {}

 private:
  scoped_refptr<Layer> layer_;
  TestSwapPromiseResult swap_promise_result_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(LayerTreeHostTestKeepSwapPromise);

class LayerTreeHostTestBreakSwapPromiseForVisibility
    : public LayerTreeHostTest {
 protected:
  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void SetVisibleFalseAndQueueSwapPromise() {
    layer_tree_host()->SetVisible(false);
    scoped_ptr<SwapPromise> swap_promise(
        new TestSwapPromise(&swap_promise_result_));
    layer_tree_host()->QueueSwapPromise(swap_promise.Pass());
  }

  void ScheduledActionWillSendBeginMainFrame() override {
    MainThreadTaskRunner()->PostTask(
        FROM_HERE,
        base::Bind(&LayerTreeHostTestBreakSwapPromiseForVisibility
                       ::SetVisibleFalseAndQueueSwapPromise,
            base::Unretained(this)));
  }

  void BeginMainFrameAbortedOnThread(LayerTreeHostImpl* host_impl,
                                     CommitEarlyOutReason reason) override {
    EndTest();
  }

  void AfterTest() override {
    {
      base::AutoLock lock(swap_promise_result_.lock);
      EXPECT_FALSE(swap_promise_result_.did_swap_called);
      EXPECT_TRUE(swap_promise_result_.did_not_swap_called);
      EXPECT_EQ(SwapPromise::COMMIT_FAILS, swap_promise_result_.reason);
      EXPECT_TRUE(swap_promise_result_.dtor_called);
    }
  }

  TestSwapPromiseResult swap_promise_result_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(LayerTreeHostTestBreakSwapPromiseForVisibility);

class LayerTreeHostTestBreakSwapPromiseForContext : public LayerTreeHostTest {
 protected:
  LayerTreeHostTestBreakSwapPromiseForContext()
      : output_surface_lost_triggered_(false) {
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void LoseOutputSurfaceAndQueueSwapPromise() {
    layer_tree_host()->DidLoseOutputSurface();
    scoped_ptr<SwapPromise> swap_promise(
        new TestSwapPromise(&swap_promise_result_));
    layer_tree_host()->QueueSwapPromise(swap_promise.Pass());
  }

  void ScheduledActionWillSendBeginMainFrame() override {
    if (output_surface_lost_triggered_)
      return;
    output_surface_lost_triggered_ = true;

    MainThreadTaskRunner()->PostTask(
        FROM_HERE,
        base::Bind(&LayerTreeHostTestBreakSwapPromiseForContext
                       ::LoseOutputSurfaceAndQueueSwapPromise,
                   base::Unretained(this)));
  }

  void BeginMainFrameAbortedOnThread(LayerTreeHostImpl* host_impl,
                                     CommitEarlyOutReason reason) override {
    // This is needed so that the impl-thread state matches main-thread state.
    host_impl->DidLoseOutputSurface();
    EndTest();
  }

  void AfterTest() override {
    {
      base::AutoLock lock(swap_promise_result_.lock);
      EXPECT_FALSE(swap_promise_result_.did_swap_called);
      EXPECT_TRUE(swap_promise_result_.did_not_swap_called);
      EXPECT_EQ(SwapPromise::COMMIT_FAILS, swap_promise_result_.reason);
      EXPECT_TRUE(swap_promise_result_.dtor_called);
    }
  }

  bool output_surface_lost_triggered_;
  TestSwapPromiseResult swap_promise_result_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(
    LayerTreeHostTestBreakSwapPromiseForContext);

class SimpleSwapPromiseMonitor : public SwapPromiseMonitor {
 public:
  SimpleSwapPromiseMonitor(LayerTreeHost* layer_tree_host,
                           LayerTreeHostImpl* layer_tree_host_impl,
                           int* set_needs_commit_count,
                           int* set_needs_redraw_count)
      : SwapPromiseMonitor(layer_tree_host, layer_tree_host_impl),
        set_needs_commit_count_(set_needs_commit_count) {}

  ~SimpleSwapPromiseMonitor() override {}

  void OnSetNeedsCommitOnMain() override { (*set_needs_commit_count_)++; }

  void OnSetNeedsRedrawOnImpl() override {
    ADD_FAILURE() << "Should not get called on main thread.";
  }

  void OnForwardScrollUpdateToMainThreadOnImpl() override {
    ADD_FAILURE() << "Should not get called on main thread.";
  }

 private:
  int* set_needs_commit_count_;
};

class LayerTreeHostTestSimpleSwapPromiseMonitor : public LayerTreeHostTest {
 public:
  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void WillBeginMainFrame() override {
    if (TestEnded())
      return;

    int set_needs_commit_count = 0;
    int set_needs_redraw_count = 0;

    {
      scoped_ptr<SimpleSwapPromiseMonitor> swap_promise_monitor(
          new SimpleSwapPromiseMonitor(layer_tree_host(),
                                       NULL,
                                       &set_needs_commit_count,
                                       &set_needs_redraw_count));
      layer_tree_host()->SetNeedsCommit();
      EXPECT_EQ(1, set_needs_commit_count);
      EXPECT_EQ(0, set_needs_redraw_count);
    }

    // Now the monitor is destroyed, SetNeedsCommit() is no longer being
    // monitored.
    layer_tree_host()->SetNeedsCommit();
    EXPECT_EQ(1, set_needs_commit_count);
    EXPECT_EQ(0, set_needs_redraw_count);

    {
      scoped_ptr<SimpleSwapPromiseMonitor> swap_promise_monitor(
          new SimpleSwapPromiseMonitor(layer_tree_host(),
                                       NULL,
                                       &set_needs_commit_count,
                                       &set_needs_redraw_count));
      layer_tree_host()->SetNeedsUpdateLayers();
      EXPECT_EQ(2, set_needs_commit_count);
      EXPECT_EQ(0, set_needs_redraw_count);
    }

    {
      scoped_ptr<SimpleSwapPromiseMonitor> swap_promise_monitor(
          new SimpleSwapPromiseMonitor(layer_tree_host(),
                                       NULL,
                                       &set_needs_commit_count,
                                       &set_needs_redraw_count));
      layer_tree_host()->SetNeedsAnimate();
      EXPECT_EQ(3, set_needs_commit_count);
      EXPECT_EQ(0, set_needs_redraw_count);
    }

    EndTest();
  }

  void AfterTest() override {}
};

SINGLE_AND_MULTI_THREAD_TEST_F(LayerTreeHostTestSimpleSwapPromiseMonitor);

class LayerTreeHostTestHighResRequiredAfterEvictingUIResources
    : public LayerTreeHostTest {
 protected:
  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->impl_side_painting = true;
  }

  void SetupTree() override {
    LayerTreeHostTest::SetupTree();
    ui_resource_ = FakeScopedUIResource::Create(layer_tree_host());
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DidActivateTreeOnThread(LayerTreeHostImpl* host_impl) override {
    host_impl->EvictAllUIResources();
    // Existence of evicted UI resources will trigger NEW_CONTENT_TAKES_PRIORITY
    // mode. Active tree should require high-res to draw after entering this
    // mode to ensure that high-res tiles are also required for a pending tree
    // to be activated.
    EXPECT_TRUE(host_impl->RequiresHighResToDraw());
  }

  void DidCommit() override {
    int frame = layer_tree_host()->source_frame_number();
    switch (frame) {
      case 1:
        PostSetNeedsCommitToMainThread();
        break;
      case 2:
        ui_resource_ = nullptr;
        EndTest();
        break;
    }
  }

  void AfterTest() override {}

  FakeContentLayerClient client_;
  scoped_ptr<FakeScopedUIResource> ui_resource_;
};

// This test is flaky, see http://crbug.com/386199
// MULTI_THREAD_TEST_F(LayerTreeHostTestHighResRequiredAfterEvictingUIResources)

class LayerTreeHostTestGpuRasterizationDefault : public LayerTreeHostTest {
 protected:
  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->impl_side_painting = true;

    EXPECT_FALSE(settings->gpu_rasterization_enabled);
    EXPECT_FALSE(settings->gpu_rasterization_forced);
  }

  void SetupTree() override {
    LayerTreeHostTest::SetupTree();

    scoped_refptr<PictureLayer> layer = PictureLayer::Create(&layer_client_);
    layer->SetBounds(gfx::Size(10, 10));
    layer->SetIsDrawable(true);
    layer_tree_host()->root_layer()->AddChild(layer);
  }

  void BeginTest() override {
    Layer* root = layer_tree_host()->root_layer();
    PictureLayer* layer = static_cast<PictureLayer*>(root->child_at(0));
    RecordingSource* recording_source = layer->GetRecordingSourceForTesting();

    // Verify default values.
    EXPECT_TRUE(root->IsSuitableForGpuRasterization());
    EXPECT_TRUE(layer->IsSuitableForGpuRasterization());
    EXPECT_TRUE(recording_source->IsSuitableForGpuRasterization());
    EXPECT_FALSE(layer_tree_host()->has_gpu_rasterization_trigger());
    EXPECT_FALSE(layer_tree_host()->UseGpuRasterization());

    // Setting gpu rasterization trigger does not enable gpu rasterization.
    layer_tree_host()->SetHasGpuRasterizationTrigger(true);
    EXPECT_TRUE(layer_tree_host()->has_gpu_rasterization_trigger());
    EXPECT_FALSE(layer_tree_host()->UseGpuRasterization());

    PostSetNeedsCommitToMainThread();
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* host_impl) override {
    EXPECT_FALSE(host_impl->pending_tree()->use_gpu_rasterization());
    EXPECT_FALSE(host_impl->use_gpu_rasterization());
  }

  void DidActivateTreeOnThread(LayerTreeHostImpl* host_impl) override {
    EXPECT_FALSE(host_impl->active_tree()->use_gpu_rasterization());
    EXPECT_FALSE(host_impl->use_gpu_rasterization());
    EndTest();
  }

  void AfterTest() override {}

  FakeContentLayerClient layer_client_;
};

MULTI_THREAD_TEST_F(LayerTreeHostTestGpuRasterizationDefault);

class LayerTreeHostTestGpuRasterizationEnabled : public LayerTreeHostTest {
 protected:
  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->impl_side_painting = true;

    EXPECT_FALSE(settings->gpu_rasterization_enabled);
    settings->gpu_rasterization_enabled = true;
  }

  void SetupTree() override {
    LayerTreeHostTest::SetupTree();

    scoped_refptr<PictureLayer> layer = PictureLayer::Create(&layer_client_);
    layer->SetBounds(gfx::Size(10, 10));
    layer->SetIsDrawable(true);
    layer_tree_host()->root_layer()->AddChild(layer);
  }

  void BeginTest() override {
    Layer* root = layer_tree_host()->root_layer();
    PictureLayer* layer = static_cast<PictureLayer*>(root->child_at(0));
    RecordingSource* recording_source = layer->GetRecordingSourceForTesting();

    // Verify default values.
    EXPECT_TRUE(root->IsSuitableForGpuRasterization());
    EXPECT_TRUE(layer->IsSuitableForGpuRasterization());
    EXPECT_TRUE(recording_source->IsSuitableForGpuRasterization());
    EXPECT_FALSE(layer_tree_host()->has_gpu_rasterization_trigger());
    EXPECT_FALSE(layer_tree_host()->UseGpuRasterization());

    // Gpu rasterization trigger is relevant.
    layer_tree_host()->SetHasGpuRasterizationTrigger(true);
    EXPECT_TRUE(layer_tree_host()->has_gpu_rasterization_trigger());
    EXPECT_TRUE(layer_tree_host()->UseGpuRasterization());

    // Content-based veto is relevant as well.
    recording_source->SetUnsuitableForGpuRasterizationForTesting();
    EXPECT_FALSE(recording_source->IsSuitableForGpuRasterization());
    EXPECT_FALSE(layer->IsSuitableForGpuRasterization());
    // Veto will take effect when layers are updated.
    // The results will be verified after commit is completed below.
    // Since we are manually marking picture pile as unsuitable,
    // make sure that the layer gets a chance to update.
    layer->SetNeedsDisplay();
    PostSetNeedsCommitToMainThread();
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* host_impl) override {
    EXPECT_FALSE(host_impl->pending_tree()->use_gpu_rasterization());
    EXPECT_FALSE(host_impl->use_gpu_rasterization());
  }

  void DidActivateTreeOnThread(LayerTreeHostImpl* host_impl) override {
    EXPECT_FALSE(host_impl->active_tree()->use_gpu_rasterization());
    EXPECT_FALSE(host_impl->use_gpu_rasterization());
    EndTest();
  }

  void AfterTest() override {}

  FakeContentLayerClient layer_client_;
};

MULTI_THREAD_TEST_F(LayerTreeHostTestGpuRasterizationEnabled);

class LayerTreeHostTestGpuRasterizationForced : public LayerTreeHostTest {
 protected:
  void InitializeSettings(LayerTreeSettings* settings) override {
    ASSERT_TRUE(settings->impl_side_painting);

    EXPECT_FALSE(settings->gpu_rasterization_forced);
    settings->gpu_rasterization_forced = true;
  }

  void SetupTree() override {
    LayerTreeHostTest::SetupTree();

    scoped_refptr<FakePictureLayer> layer =
        FakePictureLayer::Create(&layer_client_);
    layer->SetBounds(gfx::Size(10, 10));
    layer->SetIsDrawable(true);
    layer_tree_host()->root_layer()->AddChild(layer);
  }

  void BeginTest() override {
    Layer* root = layer_tree_host()->root_layer();
    PictureLayer* layer = static_cast<PictureLayer*>(root->child_at(0));
    RecordingSource* recording_source = layer->GetRecordingSourceForTesting();

    // Verify default values.
    EXPECT_TRUE(root->IsSuitableForGpuRasterization());
    EXPECT_TRUE(layer->IsSuitableForGpuRasterization());
    EXPECT_TRUE(recording_source->IsSuitableForGpuRasterization());
    EXPECT_FALSE(layer_tree_host()->has_gpu_rasterization_trigger());

    // With gpu rasterization forced, gpu rasterization trigger is irrelevant.
    EXPECT_TRUE(layer_tree_host()->UseGpuRasterization());
    layer_tree_host()->SetHasGpuRasterizationTrigger(true);
    EXPECT_TRUE(layer_tree_host()->has_gpu_rasterization_trigger());
    EXPECT_TRUE(layer_tree_host()->UseGpuRasterization());

    // Content-based veto is irrelevant as well.
    recording_source->SetUnsuitableForGpuRasterizationForTesting();
    EXPECT_FALSE(recording_source->IsSuitableForGpuRasterization());
    EXPECT_FALSE(layer->IsSuitableForGpuRasterization());
    // Veto will take effect when layers are updated.
    // The results will be verified after commit is completed below.
    // Since we are manually marking picture pile as unsuitable,
    // make sure that the layer gets a chance to update.
    layer->SetNeedsDisplay();
    PostSetNeedsCommitToMainThread();
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* host_impl) override {
    EXPECT_TRUE(host_impl->sync_tree()->use_gpu_rasterization());
    EXPECT_TRUE(host_impl->use_gpu_rasterization());
  }

  void DidActivateTreeOnThread(LayerTreeHostImpl* host_impl) override {
    EXPECT_TRUE(host_impl->active_tree()->use_gpu_rasterization());
    EXPECT_TRUE(host_impl->use_gpu_rasterization());
    EndTest();
  }

  void AfterTest() override {}

  FakeContentLayerClient layer_client_;
};

SINGLE_AND_MULTI_THREAD_IMPL_TEST_F(LayerTreeHostTestGpuRasterizationForced);

class LayerTreeHostTestContinuousPainting : public LayerTreeHostTest {
 public:
  LayerTreeHostTestContinuousPainting()
      : num_commits_(0), num_draws_(0), bounds_(20, 20), child_layer_(NULL) {}

 protected:
  enum { kExpectedNumCommits = 10 };

  void SetupTree() override {
    scoped_refptr<Layer> root_layer = Layer::Create();
    root_layer->SetBounds(bounds_);
    root_layer->CreateRenderSurface();

    if (layer_tree_host()->settings().impl_side_painting) {
      picture_layer_ = FakePictureLayer::Create(&client_);
      child_layer_ = picture_layer_.get();
    } else {
      content_layer_ = ContentLayerWithUpdateTracking::Create(&client_);
      child_layer_ = content_layer_.get();
    }
    child_layer_->SetBounds(bounds_);
    child_layer_->SetIsDrawable(true);
    root_layer->AddChild(child_layer_);

    layer_tree_host()->SetRootLayer(root_layer);
    layer_tree_host()->SetViewportSize(bounds_);
    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override {
    MainThreadTaskRunner()->PostTask(
        FROM_HERE,
        base::Bind(
            &LayerTreeHostTestContinuousPainting::EnableContinuousPainting,
            base::Unretained(this)));
    // Wait 50x longer than expected.
    double milliseconds_per_frame =
        1000.0 / layer_tree_host()->settings().renderer_settings.refresh_rate;
    MainThreadTaskRunner()->PostDelayedTask(
        FROM_HERE,
        base::Bind(
            &LayerTreeHostTestContinuousPainting::DisableContinuousPainting,
            base::Unretained(this)),
        base::TimeDelta::FromMilliseconds(50 * kExpectedNumCommits *
                                          milliseconds_per_frame));
  }

  void BeginMainFrame(const BeginFrameArgs& args) override {
    child_layer_->SetNeedsDisplay();
  }

  void AfterTest() override {
    EXPECT_LE(kExpectedNumCommits, num_commits_);
    EXPECT_LE(kExpectedNumCommits, num_draws_);
    int update_count = content_layer_.get()
                           ? content_layer_->PaintContentsCount()
                           : picture_layer_->update_count();
    EXPECT_LE(kExpectedNumCommits, update_count);
  }

  void DrawLayersOnThread(LayerTreeHostImpl* impl) override {
    if (++num_draws_ == kExpectedNumCommits)
      EndTest();
  }

  void CommitCompleteOnThread(LayerTreeHostImpl* impl) override {
    ++num_commits_;
  }

 private:
  void EnableContinuousPainting() {
    LayerTreeDebugState debug_state = layer_tree_host()->debug_state();
    debug_state.continuous_painting = true;
    layer_tree_host()->SetDebugState(debug_state);
  }

  void DisableContinuousPainting() {
    LayerTreeDebugState debug_state = layer_tree_host()->debug_state();
    debug_state.continuous_painting = false;
    layer_tree_host()->SetDebugState(debug_state);
    EndTest();
  }

  int num_commits_;
  int num_draws_;
  const gfx::Size bounds_;
  FakeContentLayerClient client_;
  scoped_refptr<ContentLayerWithUpdateTracking> content_layer_;
  scoped_refptr<FakePictureLayer> picture_layer_;
  Layer* child_layer_;
};

MULTI_THREAD_TEST_F(LayerTreeHostTestContinuousPainting);

class LayerTreeHostTestSendBeginFramesToChildren : public LayerTreeHostTest {
 public:
  LayerTreeHostTestSendBeginFramesToChildren()
      : begin_frame_sent_to_children_(false) {
  }

  void BeginTest() override {
    // Kick off the test with a commit.
    PostSetNeedsCommitToMainThread();
  }

  void SendBeginFramesToChildren(const BeginFrameArgs& args) override {
    begin_frame_sent_to_children_ = true;
    EndTest();
  }

  void DidBeginMainFrame() override {
    // Children requested BeginFrames.
    layer_tree_host()->SetChildrenNeedBeginFrames(true);
  }

  void AfterTest() override {
    // Ensure that BeginFrame message is sent to children during parent
    // scheduler handles its BeginFrame.
    EXPECT_TRUE(begin_frame_sent_to_children_);
  }

 private:
  bool begin_frame_sent_to_children_;
};

SINGLE_THREAD_TEST_F(LayerTreeHostTestSendBeginFramesToChildren);

class LayerTreeHostTestSendBeginFramesToChildrenWithExternalBFS
    : public LayerTreeHostTest {
 public:
  LayerTreeHostTestSendBeginFramesToChildrenWithExternalBFS()
      : begin_frame_sent_to_children_(false) {
  }

  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->use_external_begin_frame_source = true;
  }

  void BeginTest() override {
    // Kick off the test with a commit.
    PostSetNeedsCommitToMainThread();
  }

  void SendBeginFramesToChildren(const BeginFrameArgs& args) override {
    begin_frame_sent_to_children_ = true;
    EndTest();
  }

  void DidBeginMainFrame() override {
    // Children requested BeginFrames.
    layer_tree_host()->SetChildrenNeedBeginFrames(true);
  }

  void AfterTest() override {
    // Ensure that BeginFrame message is sent to children during parent
    // scheduler handles its BeginFrame.
    EXPECT_TRUE(begin_frame_sent_to_children_);
  }

 private:
  bool begin_frame_sent_to_children_;
};

SINGLE_THREAD_TEST_F(LayerTreeHostTestSendBeginFramesToChildrenWithExternalBFS);

class LayerTreeHostTestActivateOnInvisible : public LayerTreeHostTest {
 public:
  LayerTreeHostTestActivateOnInvisible()
      : activation_count_(0), visible_(true) {}

  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->impl_side_painting = true;
  }

  void BeginTest() override {
    // Kick off the test with a commit.
    PostSetNeedsCommitToMainThread();
  }

  void BeginCommitOnThread(LayerTreeHostImpl* host_impl) override {
    // Make sure we don't activate using the notify signal from tile manager.
    host_impl->BlockNotifyReadyToActivateForTesting(true);
  }

  void DidCommit() override { layer_tree_host()->SetVisible(false); }

  void DidSetVisibleOnImplTree(LayerTreeHostImpl* host_impl,
                               bool visible) override {
    visible_ = visible;

    // Once invisible, we can go visible again.
    if (!visible) {
      PostSetVisibleToMainThread(true);
    } else {
      EXPECT_TRUE(host_impl->RequiresHighResToDraw());
      EndTest();
    }
  }

  void DidActivateTreeOnThread(LayerTreeHostImpl* host_impl) override {
    ++activation_count_;
    EXPECT_FALSE(visible_);
  }

  void AfterTest() override {
    // Ensure we activated even though the signal was blocked.
    EXPECT_EQ(1, activation_count_);
    EXPECT_TRUE(visible_);
  }

 private:
  int activation_count_;
  bool visible_;

  FakeContentLayerClient client_;
  scoped_refptr<FakePictureLayer> picture_layer_;
};

// TODO(vmpstr): Enable with single thread impl-side painting.
MULTI_THREAD_TEST_F(LayerTreeHostTestActivateOnInvisible);

// Do a synchronous composite and assert that the swap promise succeeds.
class LayerTreeHostTestSynchronousCompositeSwapPromise
    : public LayerTreeHostTest {
 public:
  LayerTreeHostTestSynchronousCompositeSwapPromise() : commit_count_(0) {}

  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->single_thread_proxy_scheduler = false;
  }

  void BeginTest() override {
    // Successful composite.
    scoped_ptr<SwapPromise> swap_promise0(
        new TestSwapPromise(&swap_promise_result_[0]));
    layer_tree_host()->QueueSwapPromise(swap_promise0.Pass());
    layer_tree_host()->Composite(gfx::FrameTime::Now());

    // Fail to swap (no damage).
    scoped_ptr<SwapPromise> swap_promise1(
        new TestSwapPromise(&swap_promise_result_[1]));
    layer_tree_host()->QueueSwapPromise(swap_promise1.Pass());
    layer_tree_host()->SetNeedsCommit();
    layer_tree_host()->Composite(gfx::FrameTime::Now());

    // Fail to draw (not visible).
    scoped_ptr<SwapPromise> swap_promise2(
        new TestSwapPromise(&swap_promise_result_[2]));
    layer_tree_host()->QueueSwapPromise(swap_promise2.Pass());
    layer_tree_host()->SetNeedsDisplayOnAllLayers();
    layer_tree_host()->SetVisible(false);
    layer_tree_host()->Composite(gfx::FrameTime::Now());

    EndTest();
  }

  void DidCommit() override {
    commit_count_++;
    ASSERT_LE(commit_count_, 3);
  }

  void AfterTest() override {
    EXPECT_EQ(3, commit_count_);

    // Initial swap promise should have succeded.
    {
      base::AutoLock lock(swap_promise_result_[0].lock);
      EXPECT_TRUE(swap_promise_result_[0].did_swap_called);
      EXPECT_FALSE(swap_promise_result_[0].did_not_swap_called);
      EXPECT_TRUE(swap_promise_result_[0].dtor_called);
    }

    // Second swap promise fails to swap.
    {
      base::AutoLock lock(swap_promise_result_[1].lock);
      EXPECT_FALSE(swap_promise_result_[1].did_swap_called);
      EXPECT_TRUE(swap_promise_result_[1].did_not_swap_called);
      EXPECT_EQ(SwapPromise::SWAP_FAILS, swap_promise_result_[1].reason);
      EXPECT_TRUE(swap_promise_result_[1].dtor_called);
    }

    // Third swap promises also fails to swap (and draw).
    {
      base::AutoLock lock(swap_promise_result_[2].lock);
      EXPECT_FALSE(swap_promise_result_[2].did_swap_called);
      EXPECT_TRUE(swap_promise_result_[2].did_not_swap_called);
      EXPECT_EQ(SwapPromise::SWAP_FAILS, swap_promise_result_[2].reason);
      EXPECT_TRUE(swap_promise_result_[2].dtor_called);
    }
  }

  int commit_count_;
  TestSwapPromiseResult swap_promise_result_[3];
};

// Impl-side painting is not supported for synchronous compositing.
SINGLE_THREAD_NOIMPL_TEST_F(LayerTreeHostTestSynchronousCompositeSwapPromise);

// Make sure page scale and top control deltas are applied to the client even
// when the LayerTreeHost doesn't have a root layer.
class LayerTreeHostAcceptsDeltasFromImplWithoutRootLayer
    : public LayerTreeHostTest {
 public:
  LayerTreeHostAcceptsDeltasFromImplWithoutRootLayer()
      : deltas_sent_to_client_(false) {}

  void BeginTest() override {
    layer_tree_host()->SetRootLayer(nullptr);
    info_.page_scale_delta = 3.14f;
    info_.top_controls_delta = 2.73f;

    PostSetNeedsCommitToMainThread();
  }

  void BeginMainFrame(const BeginFrameArgs& args) override {
    EXPECT_EQ(nullptr, layer_tree_host()->root_layer());

    layer_tree_host()->ApplyScrollAndScale(&info_);
    EndTest();
  }

  void ApplyViewportDeltas(const gfx::Vector2dF& inner,
                           const gfx::Vector2dF& outer,
                           const gfx::Vector2dF& elastic_overscroll_delta,
                           float scale_delta,
                           float top_controls_delta) override {
    EXPECT_EQ(info_.page_scale_delta, scale_delta);
    EXPECT_EQ(info_.top_controls_delta, top_controls_delta);
    deltas_sent_to_client_ = true;
  }

  void ApplyViewportDeltas(
      const gfx::Vector2d& scroll,
      float scale_delta,
      float top_controls_delta) override {
    EXPECT_EQ(info_.page_scale_delta, scale_delta);
    EXPECT_EQ(info_.top_controls_delta, top_controls_delta);
    deltas_sent_to_client_ = true;
  }

  void AfterTest() override {
    EXPECT_TRUE(deltas_sent_to_client_);
  }

  ScrollAndScaleSet info_;
  bool deltas_sent_to_client_;
};

MULTI_THREAD_TEST_F(LayerTreeHostAcceptsDeltasFromImplWithoutRootLayer);

class LayerTreeHostTestCrispUpAfterPinchEnds : public LayerTreeHostTest {
 protected:
  LayerTreeHostTestCrispUpAfterPinchEnds()
      : playback_allowed_event_(true, true) {}

  void SetupTree() override {
    frame_ = 1;
    posted_ = false;
    client_.set_fill_with_nonsolid_color(true);

    scoped_refptr<Layer> root = Layer::Create();
    root->SetBounds(gfx::Size(500, 500));

    scoped_refptr<Layer> pinch = Layer::Create();
    pinch->SetBounds(gfx::Size(500, 500));
    pinch->SetScrollClipLayerId(root->id());
    pinch->SetIsContainerForFixedPositionLayers(true);
    root->AddChild(pinch);

    scoped_ptr<FakePicturePile> pile(
        new FakePicturePile(ImplSidePaintingSettings().minimum_contents_scale,
                            ImplSidePaintingSettings().default_tile_grid_size));
    pile->SetPlaybackAllowedEvent(&playback_allowed_event_);
    scoped_refptr<FakePictureLayer> layer =
        FakePictureLayer::CreateWithRecordingSource(&client_, pile.Pass());
    layer->SetBounds(gfx::Size(500, 500));
    layer->SetContentsOpaque(true);
    // Avoid LCD text on the layer so we don't cause extra commits when we
    // pinch.
    layer->disable_lcd_text();
    pinch->AddChild(layer);

    layer_tree_host()->RegisterViewportLayers(NULL, root, pinch, pinch);
    layer_tree_host()->SetPageScaleFactorAndLimits(1.f, 1.f, 4.f);
    layer_tree_host()->SetRootLayer(root);
    LayerTreeHostTest::SetupTree();
  }

  // Returns the delta scale of all quads in the frame's root pass from their
  // ideal, or 0 if they are not all the same.
  float FrameQuadScaleDeltaFromIdeal(LayerTreeHostImpl::FrameData* frame_data) {
    if (frame_data->has_no_damage)
      return 0.f;
    float frame_scale = 0.f;
    RenderPass* root_pass = frame_data->render_passes.back();
    for (const auto& draw_quad : root_pass->quad_list) {
      // Checkerboards mean an incomplete frame.
      if (draw_quad->material != DrawQuad::TILED_CONTENT)
        return 0.f;
      const TileDrawQuad* quad = TileDrawQuad::MaterialCast(draw_quad);
      float quad_scale =
          quad->tex_coord_rect.width() / static_cast<float>(quad->rect.width());
      float transform_scale =
          SkMScalarToFloat(quad->quadTransform().matrix().get(0, 0));
      float scale = quad_scale / transform_scale;
      if (frame_scale != 0.f && frame_scale != scale)
        return 0.f;
      frame_scale = scale;
    }
    return frame_scale;
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  DrawResult PrepareToDrawOnThread(LayerTreeHostImpl* host_impl,
                                   LayerTreeHostImpl::FrameData* frame_data,
                                   DrawResult draw_result) override {
    float quad_scale_delta = FrameQuadScaleDeltaFromIdeal(frame_data);
    switch (frame_) {
      case 1:
        // Drew at page scale 1 before any pinching.
        EXPECT_EQ(1.f, host_impl->active_tree()->current_page_scale_factor());
        EXPECT_EQ(1.f, quad_scale_delta);
        PostNextAfterDraw(host_impl);
        break;
      case 2:
        if (quad_scale_delta != 1.f)
          break;
        // Drew at page scale 1.5 after pinching in.
        EXPECT_EQ(1.5f, host_impl->active_tree()->current_page_scale_factor());
        EXPECT_EQ(1.f, quad_scale_delta);
        PostNextAfterDraw(host_impl);
        break;
      case 3:
        // By pinching out, we will create a new tiling and raster it. This may
        // cause some additional draws, though we should still be drawing with
        // the old 1.5 tiling.
        if (frame_data->has_no_damage)
          break;
        // Drew at page scale 1 with the 1.5 tiling while pinching out.
        EXPECT_EQ(1.f, host_impl->active_tree()->current_page_scale_factor());
        EXPECT_EQ(1.5f, quad_scale_delta);
        // We don't PostNextAfterDraw here, instead we wait for the new tiling
        // to finish rastering so we don't get any noise in further steps.
        break;
      case 4:
        // Drew at page scale 1 with the 1.5 tiling after pinching out completed
        // while waiting for texture uploads to complete.
        EXPECT_EQ(1.f, host_impl->active_tree()->current_page_scale_factor());
        // This frame will not have any damage, since it's actually the same as
        // the last frame, and should contain no incomplete tiles. We just want
        // to make sure we drew here at least once after the pinch ended to be
        // sure that drawing after pinch doesn't leave us at the wrong scale
        EXPECT_TRUE(frame_data->has_no_damage);
        PostNextAfterDraw(host_impl);
        break;
      case 5:
        if (quad_scale_delta != 1.f)
          break;
        // Drew at scale 1 after texture uploads are done.
        EXPECT_EQ(1.f, host_impl->active_tree()->current_page_scale_factor());
        EXPECT_EQ(1.f, quad_scale_delta);
        EndTest();
        break;
    }
    return draw_result;
  }

  void PostNextAfterDraw(LayerTreeHostImpl* host_impl) {
    if (posted_)
      return;
    posted_ = true;
    ImplThreadTaskRunner()->PostDelayedTask(
        FROM_HERE, base::Bind(&LayerTreeHostTestCrispUpAfterPinchEnds::Next,
                              base::Unretained(this), host_impl),
        // Use a delay to allow raster/upload to happen in between frames. This
        // should cause flakiness if we fail to block raster/upload when
        // desired.
        base::TimeDelta::FromMilliseconds(16 * 4));
  }

  void Next(LayerTreeHostImpl* host_impl) {
    ++frame_;
    posted_ = false;
    switch (frame_) {
      case 2:
        // Pinch zoom in.
        host_impl->PinchGestureBegin();
        host_impl->PinchGestureUpdate(1.5f, gfx::Point(100, 100));
        host_impl->PinchGestureEnd();
        break;
      case 3:
        // Pinch zoom back to 1.f but don't end it.
        host_impl->PinchGestureBegin();
        host_impl->PinchGestureUpdate(1.f / 1.5f, gfx::Point(100, 100));
        break;
      case 4:
        // End the pinch, but delay tile production.
        playback_allowed_event_.Reset();
        host_impl->PinchGestureEnd();
        break;
      case 5:
        // Let tiles complete.
        playback_allowed_event_.Signal();
        break;
    }
  }

  void NotifyTileStateChangedOnThread(LayerTreeHostImpl* host_impl,
                                      const Tile* tile) override {
    if (frame_ == 3) {
      // On frame 3, we will have a lower res tile complete for the pinch-out
      // gesture even though it's not displayed. We wait for it here to prevent
      // flakiness.
      EXPECT_EQ(0.75f, tile->contents_scale());
      PostNextAfterDraw(host_impl);
    }
    // On frame_ == 4, we are preventing texture uploads from completing,
    // so this verifies they are not completing before frame_ == 5.
    // Flaky failures here indicate we're failing to prevent uploads from
    // completing.
    EXPECT_NE(4, frame_) << tile->contents_scale();
  }

  void AfterTest() override {}

  FakeContentLayerClient client_;
  int frame_;
  bool posted_;
  base::WaitableEvent playback_allowed_event_;
};

MULTI_THREAD_IMPL_TEST_F(LayerTreeHostTestCrispUpAfterPinchEnds);

class LayerTreeHostTestCrispUpAfterPinchEndsWithOneCopy
    : public LayerTreeHostTestCrispUpAfterPinchEnds {
 protected:
  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->use_one_copy = true;
  }

  scoped_ptr<FakeOutputSurface> CreateFakeOutputSurface() override {
    scoped_ptr<TestWebGraphicsContext3D> context3d =
        TestWebGraphicsContext3D::Create();
    context3d->set_support_image(true);
    context3d->set_support_sync_query(true);
#if defined(OS_MACOSX)
    context3d->set_support_texture_rectangle(true);
#endif

    if (delegating_renderer())
      return FakeOutputSurface::CreateDelegating3d(context3d.Pass());
    else
      return FakeOutputSurface::Create3d(context3d.Pass());
  }
};

MULTI_THREAD_IMPL_TEST_F(LayerTreeHostTestCrispUpAfterPinchEndsWithOneCopy);

class RasterizeWithGpuRasterizationCreatesResources : public LayerTreeHostTest {
 protected:
  RasterizeWithGpuRasterizationCreatesResources() {}

  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->impl_side_painting = true;
    settings->gpu_rasterization_forced = true;
  }

  void SetupTree() override {
    client_.set_fill_with_nonsolid_color(true);

    scoped_refptr<Layer> root = Layer::Create();
    root->SetBounds(gfx::Size(500, 500));

    scoped_ptr<FakePicturePile> pile(
        new FakePicturePile(ImplSidePaintingSettings().minimum_contents_scale,
                            ImplSidePaintingSettings().default_tile_grid_size));
    scoped_refptr<FakePictureLayer> layer =
        FakePictureLayer::CreateWithRecordingSource(&client_, pile.Pass());
    layer->SetBounds(gfx::Size(500, 500));
    layer->SetContentsOpaque(true);
    root->AddChild(layer);

    layer_tree_host()->SetRootLayer(root);
    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  DrawResult PrepareToDrawOnThread(LayerTreeHostImpl* host_impl,
                                   LayerTreeHostImpl::FrameData* frame_data,
                                   DrawResult draw_result) override {
    EXPECT_NE(0u, host_impl->resource_provider()->num_resources());
    EndTest();
    return draw_result;
  }
  void AfterTest() override {}

  FakeContentLayerClient client_;
};

MULTI_THREAD_IMPL_TEST_F(RasterizeWithGpuRasterizationCreatesResources);

class SynchronousGpuRasterizationRasterizesVisibleOnly
    : public LayerTreeHostTest {
 protected:
  SynchronousGpuRasterizationRasterizesVisibleOnly()
      : viewport_size_(1024, 2048) {}

  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->impl_side_painting = true;
    settings->gpu_rasterization_enabled = true;
    settings->gpu_rasterization_forced = true;
    settings->threaded_gpu_rasterization_enabled = false;
  }

  void SetupTree() override {
    client_.set_fill_with_nonsolid_color(true);

    scoped_ptr<FakePicturePile> pile(
        new FakePicturePile(ImplSidePaintingSettings().minimum_contents_scale,
                            ImplSidePaintingSettings().default_tile_grid_size));
    scoped_refptr<FakePictureLayer> root =
        FakePictureLayer::CreateWithRecordingSource(&client_, pile.Pass());
    root->SetBounds(gfx::Size(viewport_size_.width(), 10000));
    root->SetContentsOpaque(true);

    layer_tree_host()->SetRootLayer(root);
    LayerTreeHostTest::SetupTree();
    layer_tree_host()->SetViewportSize(viewport_size_);
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  DrawResult PrepareToDrawOnThread(LayerTreeHostImpl* host_impl,
                                   LayerTreeHostImpl::FrameData* frame_data,
                                   DrawResult draw_result) override {
    EXPECT_EQ(4u, host_impl->resource_provider()->num_resources());

    // Verify which tiles got resources using an eviction iterator, which has to
    // return all tiles that have resources.
    scoped_ptr<EvictionTilePriorityQueue> eviction_queue(
        host_impl->BuildEvictionQueue(SAME_PRIORITY_FOR_BOTH_TREES));
    int tile_count = 0;
    for (; !eviction_queue->IsEmpty(); eviction_queue->Pop()) {
      Tile* tile = eviction_queue->Top();
      // Ensure this tile is within the viewport.
      EXPECT_TRUE(tile->content_rect().Intersects(gfx::Rect(viewport_size_)));
      // Ensure that the tile is 1/4 of the viewport tall (plus padding).
      EXPECT_EQ(tile->content_rect().height(),
                (viewport_size_.height() / 4) + 2);
      ++tile_count;
    }
    EXPECT_EQ(4, tile_count);
    EndTest();
    return draw_result;
  }

  void AfterTest() override {}

 private:
  FakeContentLayerClient client_;
  gfx::Size viewport_size_;
};

MULTI_THREAD_IMPL_TEST_F(SynchronousGpuRasterizationRasterizesVisibleOnly);

class ThreadedGpuRasterizationRasterizesBorderTiles : public LayerTreeHostTest {
 protected:
  ThreadedGpuRasterizationRasterizesBorderTiles()
      : viewport_size_(1024, 2048) {}

  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->impl_side_painting = true;
    settings->gpu_rasterization_enabled = true;
    settings->gpu_rasterization_forced = true;
    settings->threaded_gpu_rasterization_enabled = true;
  }

  void SetupTree() override {
    client_.set_fill_with_nonsolid_color(true);

    scoped_ptr<FakePicturePile> pile(
        new FakePicturePile(ImplSidePaintingSettings().minimum_contents_scale,
                            ImplSidePaintingSettings().default_tile_grid_size));
    scoped_refptr<FakePictureLayer> root =
        FakePictureLayer::CreateWithRecordingSource(&client_, pile.Pass());
    root->SetBounds(gfx::Size(10000, 10000));
    root->SetContentsOpaque(true);

    layer_tree_host()->SetRootLayer(root);
    LayerTreeHostTest::SetupTree();
    layer_tree_host()->SetViewportSize(viewport_size_);
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  DrawResult PrepareToDrawOnThread(LayerTreeHostImpl* host_impl,
                                   LayerTreeHostImpl::FrameData* frame_data,
                                   DrawResult draw_result) override {
    EXPECT_EQ(10u, host_impl->resource_provider()->num_resources());
    EndTest();
    return draw_result;
  }

  void AfterTest() override {}

 private:
  FakeContentLayerClient client_;
  gfx::Size viewport_size_;
};

MULTI_THREAD_IMPL_TEST_F(ThreadedGpuRasterizationRasterizesBorderTiles);

class LayerTreeHostTestContinuousDrawWhenCreatingVisibleTiles
    : public LayerTreeHostTest {
 protected:
  LayerTreeHostTestContinuousDrawWhenCreatingVisibleTiles()
      : playback_allowed_event_(true, true) {}

  void InitializeSettings(LayerTreeSettings* settings) override {
    settings->impl_side_painting = true;
  }

  void SetupTree() override {
    step_ = 1;
    continuous_draws_ = 0;
    client_.set_fill_with_nonsolid_color(true);

    scoped_refptr<Layer> root = Layer::Create();
    root->SetBounds(gfx::Size(500, 500));

    scoped_refptr<Layer> pinch = Layer::Create();
    pinch->SetBounds(gfx::Size(500, 500));
    pinch->SetScrollClipLayerId(root->id());
    pinch->SetIsContainerForFixedPositionLayers(true);
    root->AddChild(pinch);

    scoped_ptr<FakePicturePile> pile(
        new FakePicturePile(ImplSidePaintingSettings().minimum_contents_scale,
                            ImplSidePaintingSettings().default_tile_grid_size));
    pile->SetPlaybackAllowedEvent(&playback_allowed_event_);
    scoped_refptr<FakePictureLayer> layer =
        FakePictureLayer::CreateWithRecordingSource(&client_, pile.Pass());
    layer->SetBounds(gfx::Size(500, 500));
    layer->SetContentsOpaque(true);
    // Avoid LCD text on the layer so we don't cause extra commits when we
    // pinch.
    layer->disable_lcd_text();
    pinch->AddChild(layer);

    layer_tree_host()->RegisterViewportLayers(NULL, root, pinch, pinch);
    layer_tree_host()->SetPageScaleFactorAndLimits(1.f, 1.f, 4.f);
    layer_tree_host()->SetRootLayer(root);
    LayerTreeHostTest::SetupTree();
  }

  // Returns the delta scale of all quads in the frame's root pass from their
  // ideal, or 0 if they are not all the same.
  float FrameQuadScaleDeltaFromIdeal(LayerTreeHostImpl::FrameData* frame_data) {
    if (frame_data->has_no_damage)
      return 0.f;
    float frame_scale = 0.f;
    RenderPass* root_pass = frame_data->render_passes.back();
    for (const auto& draw_quad : root_pass->quad_list) {
      const TileDrawQuad* quad = TileDrawQuad::MaterialCast(draw_quad);
      float quad_scale =
          quad->tex_coord_rect.width() / static_cast<float>(quad->rect.width());
      float transform_scale =
          SkMScalarToFloat(quad->quadTransform().matrix().get(0, 0));
      float scale = quad_scale / transform_scale;
      if (frame_scale != 0.f && frame_scale != scale)
        return 0.f;
      frame_scale = scale;
    }
    return frame_scale;
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  DrawResult PrepareToDrawOnThread(LayerTreeHostImpl* host_impl,
                                   LayerTreeHostImpl::FrameData* frame_data,
                                   DrawResult draw_result) override {
    float quad_scale_delta = FrameQuadScaleDeltaFromIdeal(frame_data);
    switch (step_) {
      case 1:
        // Drew at scale 1 before any pinching.
        EXPECT_EQ(1.f, host_impl->active_tree()->current_page_scale_factor());
        EXPECT_EQ(1.f, quad_scale_delta);
        break;
      case 2:
        if (quad_scale_delta != 1.f / 1.5f)
          break;
        // Drew at scale 1 still though the ideal is 1.5.
        EXPECT_EQ(1.5f, host_impl->active_tree()->current_page_scale_factor());
        EXPECT_EQ(1.f / 1.5f, quad_scale_delta);
        break;
      case 3:
        // Continuous draws are attempted.
        EXPECT_EQ(1.5f, host_impl->active_tree()->current_page_scale_factor());
        if (!frame_data->has_no_damage)
          EXPECT_EQ(1.f / 1.5f, quad_scale_delta);
        break;
      case 4:
        if (quad_scale_delta != 1.f)
          break;
        // Drew at scale 1.5 when all the tiles completed.
        EXPECT_EQ(1.5f, host_impl->active_tree()->current_page_scale_factor());
        EXPECT_EQ(1.f, quad_scale_delta);
        break;
      case 5:
        // TODO(danakj): We get more draws before the NotifyReadyToDraw
        // because it is asynchronous from the previous draw and happens late.
        break;
      case 6:
        // NotifyReadyToDraw happened. If we were already inside a frame, we may
        // try to draw once more.
        break;
      case 7:
        NOTREACHED() << "No draws should happen once we have a complete frame.";
        break;
    }
    return draw_result;
  }

  void DrawLayersOnThread(LayerTreeHostImpl* host_impl) override {
    switch (step_) {
      case 1:
        // Delay tile production.
        playback_allowed_event_.Reset();
        // Pinch zoom in to cause new tiles to be required.
        host_impl->PinchGestureBegin();
        host_impl->PinchGestureUpdate(1.5f, gfx::Point(100, 100));
        host_impl->PinchGestureEnd();
        ++step_;
        break;
      case 2:
        ++step_;
        break;
      case 3:
        // We should continue to try draw while there are incomplete visible
        // tiles.
        if (++continuous_draws_ > 5) {
          // Allow the tiles to complete.
          playback_allowed_event_.Signal();
          ++step_;
        }
        break;
      case 4:
        ++step_;
        break;
      case 5:
        // Waiting for NotifyReadyToDraw.
        break;
      case 6:
        // NotifyReadyToDraw happened.
        ++step_;
        break;
    }
  }

  void NotifyReadyToDrawOnThread(LayerTreeHostImpl* host_impl) override {
    if (step_ == 5) {
      ++step_;
      // NotifyReadyToDraw has happened, we may draw once more, but should not
      // get any more draws after that. End the test after a timeout to watch
      // for any extraneous draws.
      // TODO(brianderson): We could remove this delay and instead wait until
      // the BeginFrameSource decides it doesn't need to send frames anymore,
      // or test that it already doesn't here.
      EndTestAfterDelayMs(16 * 4);
    }
  }

  void NotifyTileStateChangedOnThread(LayerTreeHostImpl* host_impl,
                                      const Tile* tile) override {
    // On step_ == 2, we are preventing texture uploads from completing,
    // so this verifies they are not completing before step_ == 3.
    // Flaky failures here indicate we're failing to prevent uploads from
    // completing.
    EXPECT_NE(2, step_);
  }

  void AfterTest() override { EXPECT_GT(continuous_draws_, 5); }

  FakeContentLayerClient client_;
  int step_;
  int continuous_draws_;
  base::WaitableEvent playback_allowed_event_;
};

MULTI_THREAD_TEST_F(LayerTreeHostTestContinuousDrawWhenCreatingVisibleTiles);

class LayerTreeHostTestOneActivatePerPrepareTiles : public LayerTreeHostTest {
 public:
  LayerTreeHostTestOneActivatePerPrepareTiles()
      : notify_ready_to_activate_count_(0u),
        scheduled_prepare_tiles_count_(0) {}

  void SetupTree() override {
    client_.set_fill_with_nonsolid_color(true);
    scoped_refptr<FakePictureLayer> root_layer =
        FakePictureLayer::Create(&client_);
    root_layer->SetBounds(gfx::Size(1500, 1500));
    root_layer->SetIsDrawable(true);

    layer_tree_host()->SetRootLayer(root_layer);
    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override {
    layer_tree_host()->SetViewportSize(gfx::Size(16, 16));
    PostSetNeedsCommitToMainThread();
  }

  void InitializedRendererOnThread(LayerTreeHostImpl* host_impl,
                                   bool success) override {
    ASSERT_TRUE(success);
    host_impl->tile_manager()->SetScheduledRasterTaskLimitForTesting(1);
  }

  void NotifyReadyToActivateOnThread(LayerTreeHostImpl* impl) override {
    ++notify_ready_to_activate_count_;
    EndTestAfterDelayMs(100);
  }

  void ScheduledActionPrepareTiles() override {
    ++scheduled_prepare_tiles_count_;
  }

  void AfterTest() override {
    // Expect at most a notification for each scheduled prepare tiles, plus one
    // for the initial commit (which doesn't go through scheduled actions).
    // The reason this is not an equality is because depending on timing, we
    // might get a prepare tiles but not yet get a notification that we're
    // ready to activate. The intent of a test is to ensure that we don't
    // get more than one notification per prepare tiles, so this is OK.
    EXPECT_LE(notify_ready_to_activate_count_,
              1u + scheduled_prepare_tiles_count_);
  }

 protected:
  FakeContentLayerClient client_;
  size_t notify_ready_to_activate_count_;
  size_t scheduled_prepare_tiles_count_;
};

MULTI_THREAD_IMPL_TEST_F(LayerTreeHostTestOneActivatePerPrepareTiles);

class LayerTreeHostTestFrameTimingRequestsSaveTimestamps
    : public LayerTreeHostTest {
 public:
  LayerTreeHostTestFrameTimingRequestsSaveTimestamps()
      : check_results_on_commit_(false) {}

  void SetupTree() override {
    scoped_refptr<FakePictureLayer> root_layer =
        FakePictureLayer::Create(&client_);
    root_layer->SetBounds(gfx::Size(200, 200));
    root_layer->SetIsDrawable(true);

    scoped_refptr<FakePictureLayer> child_layer =
        FakePictureLayer::Create(&client_);
    child_layer->SetBounds(gfx::Size(1500, 1500));
    child_layer->SetIsDrawable(true);

    std::vector<FrameTimingRequest> requests;
    requests.push_back(FrameTimingRequest(1, gfx::Rect(0, 0, 100, 100)));
    requests.push_back(FrameTimingRequest(2, gfx::Rect(300, 0, 100, 100)));
    child_layer->SetFrameTimingRequests(requests);

    root_layer->AddChild(child_layer);
    layer_tree_host()->SetRootLayer(root_layer);
    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void BeginCommitOnThread(LayerTreeHostImpl* host_impl) override {
    if (!check_results_on_commit_)
      return;

    // Since in reality, the events will be read by LayerTreeHost during commit,
    // we check the requests here to ensure that they are correct at the next
    // commit time (as opposed to checking in DrawLayers for instance).
    // TODO(vmpstr): Change this to read things from the main thread when this
    // information is propagated to the main thread (not yet implemented).
    FrameTimingTracker* tracker = host_impl->frame_timing_tracker();
    scoped_ptr<FrameTimingTracker::CompositeTimingSet> timing_set =
        tracker->GroupCountsByRectId();
    EXPECT_EQ(1u, timing_set->size());
    auto rect_1_it = timing_set->find(1);
    EXPECT_TRUE(rect_1_it != timing_set->end());
    const auto& timing_events = rect_1_it->second;
    EXPECT_EQ(1u, timing_events.size());
    EXPECT_EQ(host_impl->active_tree()->source_frame_number(),
              timing_events[0].frame_id);
    EXPECT_GT(timing_events[0].timestamp, base::TimeTicks());

    EndTest();
  }

  void DrawLayersOnThread(LayerTreeHostImpl* host_impl) override {
    check_results_on_commit_ = true;
    PostSetNeedsCommitToMainThread();
  }

  void AfterTest() override {}

 private:
  FakeContentLayerClient client_;
  bool check_results_on_commit_;
};

MULTI_THREAD_IMPL_TEST_F(LayerTreeHostTestFrameTimingRequestsSaveTimestamps);

class LayerTreeHostTestActivationCausesPrepareTiles : public LayerTreeHostTest {
 public:
  LayerTreeHostTestActivationCausesPrepareTiles()
      : scheduled_prepare_tiles_count_(0) {}

  void SetupTree() override {
    client_.set_fill_with_nonsolid_color(true);
    scoped_refptr<FakePictureLayer> root_layer =
        FakePictureLayer::Create(&client_);
    root_layer->SetBounds(gfx::Size(150, 150));
    root_layer->SetIsDrawable(true);

    layer_tree_host()->SetRootLayer(root_layer);
    LayerTreeHostTest::SetupTree();
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void NotifyReadyToActivateOnThread(LayerTreeHostImpl* impl) override {
    // Ensure we've already activated.
    EXPECT_FALSE(impl->pending_tree());

    // After activating, we either need to prepare tiles, or we've already
    // called a scheduled prepare tiles. This is done because activation might
    // cause us to have to memory available (old active tree is gone), so we
    // need to ensure we will get a PrepareTiles call.
    if (!impl->prepare_tiles_needed())
      EXPECT_GE(scheduled_prepare_tiles_count_, 1);
    EndTest();
  }

  void ScheduledActionPrepareTiles() override {
    ++scheduled_prepare_tiles_count_;
  }

  void AfterTest() override {}

 protected:
  FakeContentLayerClient client_;
  int scheduled_prepare_tiles_count_;
};

MULTI_THREAD_IMPL_TEST_F(LayerTreeHostTestActivationCausesPrepareTiles);

// This tests an assertion that DidCommit and WillCommit happen in the same
// stack frame with no tasks that run between them.  Various embedders of
// cc depend on this logic.  ui::Compositor holds a compositor lock between
// these events and the inspector timeline wants begin/end CompositeLayers
// to be properly nested with other begin/end events.
class LayerTreeHostTestNoTasksBetweenWillAndDidCommit
    : public LayerTreeHostTest {
 public:
  LayerTreeHostTestNoTasksBetweenWillAndDidCommit() : did_commit_(false) {}

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void WillCommit() override {
    MainThreadTaskRunner()->PostTask(
        FROM_HERE, base::Bind(&LayerTreeHostTestNoTasksBetweenWillAndDidCommit::
                                  EndTestShouldRunAfterDidCommit,
                              base::Unretained(this)));
  }

  void EndTestShouldRunAfterDidCommit() {
    EXPECT_TRUE(did_commit_);
    EndTest();
  }

  void DidCommit() override {
    EXPECT_FALSE(did_commit_);
    did_commit_ = true;
  }

  void AfterTest() override { EXPECT_TRUE(did_commit_); }

 private:
  bool did_commit_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(LayerTreeHostTestNoTasksBetweenWillAndDidCommit);

// Verify that if a LayerImpl holds onto a copy request for multiple
// frames that it will continue to have a render surface through
// multiple commits, even though the Layer itself has no reason
// to have a render surface.
class LayerPreserveRenderSurfaceFromOutputRequests : public LayerTreeHostTest {
 protected:
  void SetupTree() override {
    scoped_refptr<Layer> root = Layer::Create();
    root->CreateRenderSurface();
    root->SetBounds(gfx::Size(10, 10));
    child_ = Layer::Create();
    child_->SetBounds(gfx::Size(20, 20));
    root->AddChild(child_);

    layer_tree_host()->SetRootLayer(root);
    LayerTreeHostTest::SetupTree();
  }

  static void CopyOutputCallback(scoped_ptr<CopyOutputResult> result) {}

  void BeginTest() override {
    child_->RequestCopyOfOutput(
        CopyOutputRequest::CreateBitmapRequest(base::Bind(CopyOutputCallback)));
    EXPECT_TRUE(child_->HasCopyRequest());
    PostSetNeedsCommitToMainThread();
  }

  void DidCommit() override { EXPECT_FALSE(child_->HasCopyRequest()); }

  void CommitCompleteOnThread(LayerTreeHostImpl* host_impl) override {
    LayerImpl* child_impl = host_impl->sync_tree()->LayerById(child_->id());

    switch (host_impl->sync_tree()->source_frame_number()) {
      case 0:
        EXPECT_TRUE(child_impl->HasCopyRequest());
        EXPECT_TRUE(child_impl->render_surface());
        break;
      case 1:
        if (host_impl->proxy()->CommitToActiveTree()) {
          EXPECT_TRUE(child_impl->HasCopyRequest());
          EXPECT_TRUE(child_impl->render_surface());
        } else {
          EXPECT_FALSE(child_impl->HasCopyRequest());
          EXPECT_FALSE(child_impl->render_surface());
        }
        break;
      default:
        NOTREACHED();
        break;
    }
  }

  void DidActivateTreeOnThread(LayerTreeHostImpl* host_impl) override {
    LayerImpl* child_impl = host_impl->active_tree()->LayerById(child_->id());
    EXPECT_TRUE(child_impl->HasCopyRequest());
    EXPECT_TRUE(child_impl->render_surface());

    switch (host_impl->active_tree()->source_frame_number()) {
      case 0:
        // Lose output surface to prevent drawing and cause another commit.
        host_impl->DidLoseOutputSurface();
        break;
      case 1:
        EndTest();
        break;
      default:
        NOTREACHED();
        break;
    }
  }

  void AfterTest() override {}

 private:
  scoped_refptr<Layer> child_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(LayerPreserveRenderSurfaceFromOutputRequests);

}  // namespace cc
