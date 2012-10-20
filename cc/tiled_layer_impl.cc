// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "cc/tiled_layer_impl.h"

#include "CCAppendQuadsData.h"
#include "CCCheckerboardDrawQuad.h"
#include "CCDebugBorderDrawQuad.h"
#include "CCLayerTilingData.h"
#include "CCMathUtil.h"
#include "CCQuadSink.h"
#include "FloatQuad.h"
#include "base/basictypes.h"
#include "base/stringprintf.h"
#include "cc/solid_color_draw_quad.h"
#include "cc/tile_draw_quad.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "third_party/skia/include/core/SkColor.h"

using namespace std;
using WebKit::WebTransformationMatrix;

namespace cc {

static const int debugTileBorderWidth = 1;
static const int debugTileBorderAlpha = 100;
static const int debugTileBorderColorRed = 80;
static const int debugTileBorderColorGreen = 200;
static const int debugTileBorderColorBlue = 200;
static const int debugTileBorderMissingTileColorRed = 255;
static const int debugTileBorderMissingTileColorGreen = 0;
static const int debugTileBorderMissingTileColorBlue = 0;

static const int defaultCheckerboardColorRed = 241;
static const int defaultCheckerboardColorGreen = 241;
static const int defaultCheckerboardColorBlue = 241;
static const int debugTileEvictedCheckerboardColorRed = 255;
static const int debugTileEvictedCheckerboardColorGreen = 200;
static const int debugTileEvictedCheckerboardColorBlue = 200;
static const int debugTileInvalidatedCheckerboardColorRed = 128;
static const int debugTileInvalidatedCheckerboardColorGreen = 200;
static const int debugTileInvalidatedCheckerboardColorBlue = 245;

class DrawableTile : public CCLayerTilingData::Tile {
public:
    static scoped_ptr<DrawableTile> create() { return make_scoped_ptr(new DrawableTile()); }

    CCResourceProvider::ResourceId resourceId() const { return m_resourceId; }
    void setResourceId(CCResourceProvider::ResourceId resourceId) { m_resourceId = resourceId; }

private:
    DrawableTile() : m_resourceId(0) { }

    CCResourceProvider::ResourceId m_resourceId;

    DISALLOW_COPY_AND_ASSIGN(DrawableTile);
};

CCTiledLayerImpl::CCTiledLayerImpl(int id)
    : CCLayerImpl(id)
    , m_skipsDraw(true)
    , m_contentsSwizzled(false)
{
}

CCTiledLayerImpl::~CCTiledLayerImpl()
{
}

CCResourceProvider::ResourceId CCTiledLayerImpl::contentsResourceId() const
{
    // This function is only valid for single texture layers, e.g. masks.
    DCHECK(m_tiler);
    DCHECK(m_tiler->numTilesX() == 1);
    DCHECK(m_tiler->numTilesY() == 1);

    DrawableTile* tile = tileAt(0, 0);
    CCResourceProvider::ResourceId resourceId = tile ? tile->resourceId() : 0;
    return resourceId;
}

void CCTiledLayerImpl::dumpLayerProperties(std::string* str, int indent) const
{
    str->append(indentString(indent));
    base::StringAppendF(str, "skipsDraw: %d\n", (!m_tiler || m_skipsDraw));
    CCLayerImpl::dumpLayerProperties(str, indent);
}

bool CCTiledLayerImpl::hasTileAt(int i, int j) const
{
    return m_tiler->tileAt(i, j);
}

bool CCTiledLayerImpl::hasResourceIdForTileAt(int i, int j) const
{
    return hasTileAt(i, j) && tileAt(i, j)->resourceId();
}

DrawableTile* CCTiledLayerImpl::tileAt(int i, int j) const
{
    return static_cast<DrawableTile*>(m_tiler->tileAt(i, j));
}

DrawableTile* CCTiledLayerImpl::createTile(int i, int j)
{
    scoped_ptr<DrawableTile> tile(DrawableTile::create());
    DrawableTile* addedTile = tile.get();
    m_tiler->addTile(tile.PassAs<CCLayerTilingData::Tile>(), i, j);
    return addedTile;
}

void CCTiledLayerImpl::appendQuads(CCQuadSink& quadSink, CCAppendQuadsData& appendQuadsData)
{
    const IntRect& contentRect = visibleContentRect();

    if (!m_tiler || m_tiler->hasEmptyBounds() || contentRect.isEmpty())
        return;

    CCSharedQuadState* sharedQuadState = quadSink.useSharedQuadState(createSharedQuadState());
    appendDebugBorderQuad(quadSink, sharedQuadState, appendQuadsData);

    int left, top, right, bottom;
    m_tiler->contentRectToTileIndices(contentRect, left, top, right, bottom);

    if (hasDebugBorders()) {
        for (int j = top; j <= bottom; ++j) {
            for (int i = left; i <= right; ++i) {
                DrawableTile* tile = tileAt(i, j);
                IntRect tileRect = m_tiler->tileBounds(i, j);
                SkColor borderColor;

                if (m_skipsDraw || !tile || !tile->resourceId())
                    borderColor = SkColorSetARGB(debugTileBorderAlpha, debugTileBorderMissingTileColorRed, debugTileBorderMissingTileColorGreen, debugTileBorderMissingTileColorBlue);
                else
                    borderColor = SkColorSetARGB(debugTileBorderAlpha, debugTileBorderColorRed, debugTileBorderColorGreen, debugTileBorderColorBlue);
                quadSink.append(CCDebugBorderDrawQuad::create(sharedQuadState, tileRect, borderColor, debugTileBorderWidth).PassAs<CCDrawQuad>(), appendQuadsData);
            }
        }
    }

    if (m_skipsDraw)
        return;

    for (int j = top; j <= bottom; ++j) {
        for (int i = left; i <= right; ++i) {
            DrawableTile* tile = tileAt(i, j);
            IntRect tileRect = m_tiler->tileBounds(i, j);
            IntRect displayRect = tileRect;
            tileRect.intersect(contentRect);

            // Skip empty tiles.
            if (tileRect.isEmpty())
                continue;

            if (!tile || !tile->resourceId()) {
                if (drawCheckerboardForMissingTiles()) {
                    SkColor defaultColor = SkColorSetRGB(defaultCheckerboardColorRed, defaultCheckerboardColorGreen, defaultCheckerboardColorBlue);
                    SkColor evictedColor = SkColorSetRGB(debugTileEvictedCheckerboardColorRed, debugTileEvictedCheckerboardColorGreen, debugTileEvictedCheckerboardColorBlue);
                    SkColor invalidatedColor = SkColorSetRGB(debugTileInvalidatedCheckerboardColorRed, debugTileEvictedCheckerboardColorGreen, debugTileEvictedCheckerboardColorBlue);

                    SkColor checkerColor;
                    if (hasDebugBorders())
                        checkerColor = tile ? invalidatedColor : evictedColor;
                    else
                        checkerColor = defaultColor;

                    appendQuadsData.hadMissingTiles |= quadSink.append(CCCheckerboardDrawQuad::create(sharedQuadState, tileRect, checkerColor).PassAs<CCDrawQuad>(), appendQuadsData);
                } else
                    appendQuadsData.hadMissingTiles |= quadSink.append(CCSolidColorDrawQuad::create(sharedQuadState, tileRect, backgroundColor()).PassAs<CCDrawQuad>(), appendQuadsData);
                continue;
            }

            IntRect tileOpaqueRect = tile->opaqueRect();
            tileOpaqueRect.intersect(contentRect);

            // Keep track of how the top left has moved, so the texture can be
            // offset the same amount.
            IntSize displayOffset = tileRect.minXMinYCorner() - displayRect.minXMinYCorner();
            IntPoint textureOffset = m_tiler->textureOffset(i, j) + displayOffset;
            float tileWidth = static_cast<float>(m_tiler->tileSize().width());
            float tileHeight = static_cast<float>(m_tiler->tileSize().height());
            IntSize textureSize(tileWidth, tileHeight);

            bool clipped = false;
            FloatQuad visibleContentInTargetQuad = CCMathUtil::mapQuad(drawTransform(), FloatQuad(visibleContentRect()), clipped);
            bool isAxisAlignedInTarget = !clipped && visibleContentInTargetQuad.isRectilinear();
            bool useAA = m_tiler->hasBorderTexels() && !isAxisAlignedInTarget;

            bool leftEdgeAA = !i && useAA;
            bool topEdgeAA = !j && useAA;
            bool rightEdgeAA = i == m_tiler->numTilesX() - 1 && useAA;
            bool bottomEdgeAA = j == m_tiler->numTilesY() - 1 && useAA;

            const GLint textureFilter = m_tiler->hasBorderTexels() ? GL_LINEAR : GL_NEAREST;
            quadSink.append(CCTileDrawQuad::create(sharedQuadState, tileRect, tileOpaqueRect, tile->resourceId(), textureOffset, textureSize, textureFilter, contentsSwizzled(), leftEdgeAA, topEdgeAA, rightEdgeAA, bottomEdgeAA).PassAs<CCDrawQuad>(), appendQuadsData);
        }
    }
}

void CCTiledLayerImpl::setTilingData(const CCLayerTilingData& tiler)
{
    if (m_tiler)
        m_tiler->reset();
    else
        m_tiler = CCLayerTilingData::create(tiler.tileSize(), tiler.hasBorderTexels() ? CCLayerTilingData::HasBorderTexels : CCLayerTilingData::NoBorderTexels);
    *m_tiler = tiler;
}

void CCTiledLayerImpl::pushTileProperties(int i, int j, CCResourceProvider::ResourceId resourceId, const IntRect& opaqueRect)
{
    DrawableTile* tile = tileAt(i, j);
    if (!tile)
        tile = createTile(i, j);
    tile->setResourceId(resourceId);
    tile->setOpaqueRect(opaqueRect);
}

void CCTiledLayerImpl::pushInvalidTile(int i, int j)
{
    DrawableTile* tile = tileAt(i, j);
    if (!tile)
        tile = createTile(i, j);
    tile->setResourceId(0);
    tile->setOpaqueRect(IntRect());
}

Region CCTiledLayerImpl::visibleContentOpaqueRegion() const
{
    if (m_skipsDraw)
        return Region();
    if (contentsOpaque())
        return visibleContentRect();
    return m_tiler->opaqueRegionInContentRect(visibleContentRect());
}

void CCTiledLayerImpl::didLoseContext()
{
    m_tiler->reset();
}

const char* CCTiledLayerImpl::layerTypeAsString() const
{
    return "ContentLayer";
}

} // namespace cc
