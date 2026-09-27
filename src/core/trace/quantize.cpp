// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Quantization for Inkscape
 *
 * Authors:
 *   Stéphane Gimenez <dev@gim.name>
 *
 * Copyright (C) 2006 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#include <memory>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>
#include <cstdio>
#include <glib.h>

#include "pool.h"
#include "imagemap.h"
#include "quantize.h"

namespace Inkscape {
namespace Trace {

namespace {

/**
 * an octree node datastructure
 */
struct Ocnode
{
    Ocnode *parent;           // parent node
    Ocnode **ref;             // node's reference
    Ocnode *child[8];         // children
    int nchild;               // number of children
    int width;                // width level of this node
    RGB rgb;                  // rgb's prefix of that node
    unsigned long weight;     // number of pixels this node accounts for
    unsigned long rs, gs, bs; // sum of pixels colors this node accounts for
    int nleaf;                // number of leaves under this node
    unsigned long mi;         // minimum impact
};

/*
-- algorithm principle:

- inspired by the octree method, we associate a tree to a given color map

- nodes in those trees have this shape:

                                parent
                                   |
        color_prefix(stored in rgb):width
     colors_sum(stored in rs,gs,bs)/weight
         /               |               \
     child1           child2           child3

- (grayscale) trees associated to pixels with colors 87 = 0b1010111 and
  69 = 0b1000101 are:

           .                 .    <-- roots of the trees
           |                 |
    1010111:0  and    1000101:0   <-- color prefixes, written in binary form
         87/1              69/1   <-- color sums, written in decimal form

- the result of merging the two trees is:

                   .
                   |
                 10:5       <----- longest common prefix and binary width
                156/2       <---.  of the covered color range.
            /            \      |
    1000101:0      1010111:0    '- sum of colors and quantity of pixels
         69/1           87/1       this node accounts for

  one should consider three cases when two trees are to be merged:
  - one tree range is included in the range of the other one, and the first
    tree has to be inserted as a child (or merged with the corresponding
    child) of the other.
  - their ranges are the same, and their children have to be merged under
    a single root.
  - ranges have no intersection, and a fork node has to be created (like in
    the given example).

- a tree for an image is built dividing the image in 2 parts and merging
  the trees obtained recursively for the two parts. a tree for a one pixel
  part is a leaf like one of those which were given above.

- last, this tree is reduced a specified number of leaves, deleting first
  leaves with minimal impact i.e. [ weight * 2^(2*parentwidth) ] value :
  a fair approximation of the impact a leaf removal would have on the final
  result : it's the corresponding covered area times the square of the
  introduced color distance.

  deletion of a node A below a node with only two children is done as
  follows :

  - when the sibling is a leaf, the sibling is deleted as well, both nodes
    are then represented by their parent.

     |               |
     .       ==>     .
    / \
   A   .

  - otherwise the deletion of A deletes also its parent, which plays no
    role anymore:

     |                |
     .       ==>       \
    / \                 |
   A   .                .
      / \              / \

  in that way, every leaf removal operation really decreases the remaining
  total number of leaves by one.

- very last, color indexes are attributed to leaves; associated colors are
  averages, computed from weight and color components sums.

-- improvements to the usual octree method:

- since this algorithm shall often be used to perform quantization using a
  very low (2-16) set of colors and not with a usual 256 value, we choose
  more carefully which nodes are to be deleted.

- depth of leaves is not fixed to an arbitrary number (which should be 8
  when color components are in 0-255), so there is no need to go down to a
  depth of 8 for each pixel (at full precision), unless it is really
  required.

- tree merging also fastens the overall tree building, and intermediate
  processing could be done.

- a huge optimization against the stupid removal algorithm (i.e. find a best
  match over the whole tree, remove it and do it again) was implemented:
  nodes are marked with the minimal impact of the removal of a leaf below
  it. we proceed to the removal recursively. we stop when current removal
  level is above the current node minimal, otherwise reached leaves are
  removed, and every change over minimal impacts is propagated back to the
  whole tree when the recursion ends.

-- specific optimizations

- pool allocation is used to allocate nodes (increased performance on large
  images).

*/

RGB operator>>(RGB rgb, int s)
{
    RGB res;
    res.r = rgb.r >> s;
    res.g = rgb.g >> s;
    res.b = rgb.b >> s;
    return res;
}

bool operator==(RGB rgb1, RGB rgb2)
{
    return rgb1.r == rgb2.r && rgb1.g == rgb2.g && rgb1.b == rgb2.b;
}

int childIndex(RGB rgb)
{
    return ((rgb.r & 1) << 2) | ((rgb.g & 1) << 1) | (rgb.b & 1);
}

/**
 * allocate a new node
 */
Ocnode *ocnodeNew(Pool<Ocnode> &pool)
{
    Ocnode *node = pool.draw();
    node->ref = nullptr;
    node->parent = nullptr;
    node->nchild = 0;
    for (auto &i : node->child) {
        i = nullptr;
    }
    node->mi = 0;
    return node;
}

void ocnodeFree(Pool<Ocnode> &pool, Ocnode *node)
{
    pool.drop(node);
}

/**
 * free a full octree
 */
void octreeDelete(Pool<Ocnode> &pool, Ocnode *node)
{
    if (!node) return;
    for (auto &i : node->child) {
        octreeDelete(pool, i);
    }
    ocnodeFree(pool, node);
}

/**
 *  pretty-print an octree, debugging purposes
 */
#if 0
void ocnodePrint(Ocnode *node, int indent)
{
    if (!node) return;
    printf("width:%d weight:%lu rgb:%6x nleaf:%d mi:%lu\n",
           node->width,
           node->weight,
           (unsigned int)(
           ((node->rs / node->weight) << 16) +
           ((node->gs / node->weight) << 8) +
           (node->bs / node->weight)),
           node->nleaf,
           node->mi
           );
    for (int i = 0; i < 8; i++) if (node->child[i])
        {
        for (int k = 0; k < indent; k++) printf(" ");//indentation
        printf("[%d:%p] ", i, node->child[i]);
        ocnodePrint(node->child[i], indent+2);
        }
}

void octreePrint(Ocnode *node)
{
    printf("<<octree>>\n");
    if (node) printf("[r:%p] ", node); ocnodePrint(node, 2);
}
#endif

/**
 * builds a single <rgb> color leaf at location <ref>
 */
void ocnodeLeaf(Pool<Ocnode> &pool, Ocnode **ref, RGB rgb)
{
    assert(ref);
    Ocnode *node = ocnodeNew(pool);
    node->width = 0;
    node->rgb = rgb;
    node->rs = rgb.r; node->gs = rgb.g; node->bs = rgb.b;
    node->weight = 1;
    node->nleaf = 1;
    node->mi = 0;
    node->ref = ref;
    *ref = node;
}

/**
 * IVF PATCH (alpha-nopremult): leaf with a custom histogram weight
 * (used for semi-transparent pixels, whose weight is their alpha).
 */
void ocnodeLeafWeighted(Pool<Ocnode> &pool, Ocnode **ref, RGB rgb, unsigned long weight)
{
    assert(ref);
    Ocnode *node = ocnodeNew(pool);
    node->width = 0;
    node->rgb = rgb;
    node->rs = rgb.r * weight; node->gs = rgb.g * weight; node->bs = rgb.b * weight;
    node->weight = weight;
    node->nleaf = 1;
    node->mi = 0;
    node->ref = ref;
    *ref = node;
}

/**
 *  merge nodes <node1> and <node2> at location <ref> with parent <parent>
 */
int octreeMerge(Pool<Ocnode> &pool, Ocnode *parent, Ocnode **ref, Ocnode *node1, Ocnode *node2)
{
    assert(ref);
    if (!node1 && !node2) return 0;
    assert(node1 != node2);
    if (parent && !*ref) parent->nchild++;
    if (!node1) {
        *ref = node2; node2->ref = ref; node2->parent = parent;
        return node2->nleaf;
    }
    if (!node2) {
        *ref = node1; node1->ref = ref; node1->parent = parent;
        return node1->nleaf;
    }
    int dwitdth = node1->width - node2->width;
    if (dwitdth > 0 && node1->rgb == node2->rgb >> dwitdth) {
        // place node2 below node1
        *ref = node1; node1->ref = ref; node1->parent = parent;
        int i = childIndex(node2->rgb >> (dwitdth - 1));
        node1->rs += node2->rs; node1->gs += node2->gs; node1->bs += node2->bs;
        node1->weight += node2->weight;
        node1->mi = 0;
        if (node1->child[i]) node1->nleaf -= node1->child[i]->nleaf;
        node1->nleaf += octreeMerge(pool, node1, &node1->child[i], node1->child[i], node2);
        return node1->nleaf;
    } else if (dwitdth < 0 && node2->rgb == node1->rgb >> (-dwitdth)) {
        // place node1 below node2
        *ref = node2; node2->ref = ref; node2->parent = parent;
        int i = childIndex(node1->rgb >> (-dwitdth - 1));
        node2->rs += node1->rs; node2->gs += node1->gs; node2->bs += node1->bs;
        node2->weight += node1->weight;
        node2->mi = 0;
        if (node2->child[i]) node2->nleaf -= node2->child[i]->nleaf;
        node2->nleaf += octreeMerge(pool, node2, &node2->child[i], node2->child[i], node1);
        return node2->nleaf;
    } else {
        // nodes have either no intersection or the same root
        Ocnode *newnode;
        newnode = ocnodeNew(pool);
        newnode->rs = node1->rs + node2->rs;
        newnode->gs = node1->gs + node2->gs;
        newnode->bs = node1->bs + node2->bs;
        newnode->weight = node1->weight + node2->weight;
        *ref = newnode; newnode->ref = ref; newnode->parent = parent;
        if (dwitdth == 0 && node1->rgb == node2->rgb) {
            // merge the nodes in <newnode>
            newnode->width = node1->width; // == node2->width
            newnode->rgb = node1->rgb;     // == node2->rgb
            newnode->nchild = 0;
            newnode->nleaf = 0;
            if (node1->nchild == 0 && node2->nchild == 0) {
                newnode->nleaf = 1;
            } else {
                for (int i = 0; i < 8; i++) {
                    if (node1->child[i] || node2->child[i]) {
                        newnode->nleaf += octreeMerge(pool, newnode, &newnode->child[i], node1->child[i], node2->child[i]);
                    }
                }
            }
            ocnodeFree(pool, node1); ocnodeFree(pool, node2);
            return newnode->nleaf;
        } else {
            // use <newnode> as a fork node with children <node1> and <node2>
            int newwidth = std::max(node1->width, node2->width);
            RGB rgb1 = node1->rgb >> (newwidth - node1->width);
            RGB rgb2 = node2->rgb >> (newwidth - node2->width);
            // according to the previous tests <rgb1> != <rgb2> before the loop
            while (!(rgb1 == rgb2)) {
                rgb1 = rgb1 >> 1;
                rgb2 = rgb2 >> 1;
                newwidth++;
            }
            newnode->width = newwidth;
            newnode->rgb = rgb1; // == rgb2
            newnode->nchild = 2;
            newnode->nleaf = node1->nleaf + node2->nleaf;
            int i1 = childIndex(node1->rgb >> (newwidth - node1->width - 1));
            int i2 = childIndex(node2->rgb >> (newwidth - node2->width - 1));
            node1->parent = newnode;
            node1->ref = &newnode->child[i1];
            newnode->child[i1] = node1;
            node2->parent = newnode;
            node2->ref = &newnode->child[i2];
            newnode->child[i2] = node2;
            return newnode->nleaf;
        }
    }
}

/**
 * upatade mi value for leaves
 */
void ocnodeMi(Ocnode *node)
{
    node->mi = node->parent ? node->weight << (2 * node->parent->width) : 0;
}

/**
 * remove leaves whose prune impact value is lower than <lvl>. at most
 * <count> leaves are removed, and <count> is decreased on each removal.
 * all parameters including minimal impact values are regenerated.
 */
void ocnodeStrip(Pool<Ocnode> &pool, Ocnode **ref, int &count, unsigned long lvl)
{
    Ocnode *node = *ref;
    if (!node) return;
    assert(ref == node->ref);
    if (node->nchild == 0) { // leaf node
        if (!node->mi) ocnodeMi(node); // mi generation may be required
        if (node->mi > lvl) return; // leaf is above strip level
        ocnodeFree(pool, node);
        *ref = nullptr;
        count--;
    } else {
        if (node->mi && node->mi > lvl) return; // node is above strip level
        node->nchild = 0;
        node->nleaf = 0;
        node->mi = 0;
        Ocnode **lonelychild = nullptr;
        for (auto & i : node->child) {
            if (i) {
                ocnodeStrip(pool, &i, count, lvl);
                if (i) {
                    lonelychild = &i;
                    node->nchild++;
                    node->nleaf += i->nleaf;
                    if (!node->mi || node->mi > i->mi) {
                        node->mi = i->mi;
                    }
                }
            }
        }
        // tree adjustments
        if (node->nchild == 0) {
            count++;
            node->nleaf = 1;
            ocnodeMi(node);
        } else if (node->nchild == 1) {
            if ((*lonelychild)->nchild == 0) {
                // remove the <lonelychild> leaf under a 1 child node
                node->nchild = 0;
                node->nleaf = 1;
                ocnodeMi(node);
                ocnodeFree(pool, *lonelychild);
                *lonelychild = nullptr;
            } else {
                // make a bridge to <lonelychild> over a 1 child node
                (*lonelychild)->parent = node->parent;
                (*lonelychild)->ref = ref;
                ocnodeFree(pool, node);
                *ref = *lonelychild;
            }
        }
    }
}

/**
 * reduce the leaves of an octree to a given number
 */
void octreePrune(Pool<Ocnode> &pool, Ocnode **ref, int ncolor)
{
    assert(ref);
    assert(ncolor > 0);
    // IVF PATCH (prune-nullcheck): with fully-transparent images the build
    // creates no leaves and *ref is nullptr; upstream dereferenced it before
    // checking. Check first, then compute.
    if (!*ref) return;
    int n = (*ref)->nleaf - ncolor;
    if (n <= 0) return;
    while (n > 0) {
        ocnodeStrip(pool, ref, n, (*ref)->mi);
    }
}

/**
 * build an octree associated to the area of a color map <rgbmap>,
 * included in the specified (x1,y1)--(x2,y2) rectangle.
 */
void octreeBuildArea(Pool<Ocnode> &pool, RgbMap const &rgbmap, Ocnode **ref, int x1, int y1, int x2, int y2, int ncolor)
{
    int dx = x2 - x1, dy = y2 - y1;
    int xm = x1 + dx / 2, ym = y1 + dy / 2;
    Ocnode *ref1 = nullptr;
    Ocnode *ref2 = nullptr;
    if (dx == 1 && dy == 1) {        // IVF PATCH (alpha-quant): pixels below the 50% alpha threshold
        // contribute no leaf (they are background, not solid content — see
        // alpha-threshold in imagemap.h); semi-transparent pixels contribute
        // their color weighted by alpha. This prevents the quantizer from
        // inventing a "white background" layer out of transparent regions
        // (docs/FIX_FONDO.md).
        if (rgbmap.isTransparent(x1, y1)) {
            return; // leave this one-pixel area without a node
        }
        // IVF PATCH (alpha-nopremult): the stored color is un-composited;
        // alpha participates as the histogram weight so semi-transparent
        // pixels contribute proportionally to their visibility.
        if (rgbmap.hasAlpha()) {
            auto rgb = rgbmap.getPixel(x1, y1);
            int const a = rgbmap.getAlpha(x1, y1);
            ocnodeLeafWeighted(pool, ref, rgb, std::max(1, a));
        } else {
            ocnodeLeaf(pool, ref, rgbmap.getPixel(x1, y1));
        }
    } else if (dx > dy) {
        octreeBuildArea(pool, rgbmap, &ref1, x1, y1, xm, y2, ncolor);
        octreeBuildArea(pool, rgbmap, &ref2, xm, y1, x2, y2, ncolor);
        octreeMerge(pool, nullptr, ref, ref1, ref2);
    } else {
        octreeBuildArea(pool, rgbmap, &ref1, x1, y1, x2, ym, ncolor);
        octreeBuildArea(pool, rgbmap, &ref2, x1, ym, x2, y2, ncolor);
        octreeMerge(pool, nullptr, ref, ref1, ref2);
	}

    // octreePrune(ref, 2 * ncolor);
    // affects result quality for almost same performance :/
}

/**
 * build an octree associated to the <rgbmap> color map,
 * pruned to <ncolor> colors.
 */
Ocnode *octreeBuild(Pool<Ocnode> &pool, RgbMap const &rgbmap, int ncolor)
{
    // create the octree
    Ocnode *node = nullptr;
    octreeBuildArea(pool,
                    rgbmap, &node,
                    0, 0, rgbmap.width, rgbmap.height, ncolor);

    // prune the octree
    octreePrune(pool, &node, ncolor);

    return node;
}

/**
 * compute the color palette associated to an octree.
 */
void octreeIndex(Ocnode *node, RGB *rgbpal, int &index)
{
    if (!node) return;
    if (node->nchild == 0) {
        rgbpal[index].r = node->rs / node->weight;
        rgbpal[index].g = node->gs / node->weight;
        rgbpal[index].b = node->bs / node->weight;
        index++;
    } else {
        for (auto &i : node->child) {
            if (i) {
                octreeIndex(i, rgbpal, index);
            }
        }
    }
}

/**
 * compute the squared distance between two colors
 */
int distRGB(RGB rgb1, RGB rgb2)
{
    return (rgb1.r - rgb2.r) * (rgb1.r - rgb2.r)
         + (rgb1.g - rgb2.g) * (rgb1.g - rgb2.g)
         + (rgb1.b - rgb2.b) * (rgb1.b - rgb2.b);
}

/**
 * find the index of closest color in a palette
 *
 * IVF PATCH (revert-lab-match) [v1.7]: back to upstream RGB distance.
 * The v1.6 Lab metric (distLab, uncached per pixel × palette) was a
 * measured regression: it amplified sub-ΔE differences between k-means-
 * drifted near-identical centroids, shattering flat regions into speck
 * islands (docs/AUDITORIA_v4.md §CAUSA-B). Upstream semantics restored.
 */
int findRGB(RGB const *rgbs, int ncolor, RGB rgb)
{
    int index = -1, dist = 0;
    for (int k = 0; k < ncolor; k++) {
        int const d = distRGB(rgbs[k], rgb);
        if (index == -1 || d < dist) { dist = d; index = k; }
    }
    return index;
}

constexpr unsigned IndexSentinel = (unsigned)-1;


/**
 * IVF PATCH (index-despeckle / modal-despeckle) [v1.6]: remove misquantized
 * islands WITHOUT eroding fine real texture.
 *
 * Antialiased boundaries between adjacent palette entries produce scattered
 * single pixels and 1-px streaks of alternating indices; small same-index
 * islands of real texture (grain, dashes) also exist. Size rules and
 * bbox-halo rules cannot separate them reliably (a streak IS small; a grain
 * cluster IS near nothing of its own index).
 *
 * The separation that actually works: REAL structure survives the modal
 * filter, noise does not.
 *
 * Pass 1: label every 8-connected same-index component; a component is REAL
 *         iff its size >= max_island.
 * Pass 2 (modal smoothing): every non-real island takes the majority index
 *         of its 8-neighborhood. Single pixels, streaks and ragged edges
 *         snap to their parent region; real texture (whose every pixel sits
 *         inside a real component) is untouched by definition.
 * Pass 3 (lonely-island absorption): remaining small islands with no real
 *         same-index structure within 1 px of their bbox are absorbed into
 *         their dominant neighbor; islands of any size surrounded only by
 *         transparency are absorbed if small (background pinholes).
 */
void imapDespeckle(IndexedMap &imap, int max_island)
{
    if (max_island <= 0 || imap.width <= 0 || imap.height <= 0) {
        return;
    }
    int const w = imap.width;
    int const h = imap.height;
    int const dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    int const dy[8] = { -1, -1, -1,  0, 0,  1, 1, 1 };

    // ---- Pass 1: component labeling + "real" mask ----
    std::vector<int> comp(size_t(w) * h, -1);
    std::vector<char> real(size_t(w) * h, 0);
    std::vector<int> stack;
    std::vector<int> cells;
    for (int y0 = 0; y0 < h; y0++) {
        for (int x0 = 0; x0 < w; x0++) {
            int const start = x0 + y0 * w;
            unsigned const idx0 = imap.getPixel(x0, y0);
            if (comp[start] >= 0 || idx0 == IndexSentinel) {
                continue;
            }
            cells.clear();
            stack.clear();
            stack.push_back(start);
            comp[start] = start;
            while (!stack.empty()) {
                int const cur = stack.back();
                stack.pop_back();
                cells.push_back(cur);
                int const cx = cur % w;
                int const cy = cur / w;
                for (int k = 0; k < 8; k++) {
                    int const nx = cx + dx[k];
                    int const ny = cy + dy[k];
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
                        continue;
                    }
                    int const ni = nx + ny * w;
                    if (imap.getPixel(nx, ny) == idx0 && comp[ni] < 0) {
                        comp[ni] = start;
                        stack.push_back(ni);
                    }
                }
            }
            if (int(cells.size()) >= max_island) {
                for (int c : cells) {
                    real[c] = 1;
                }
            }
        }
    }

    // ---- Pass 2: modal smoothing of non-real islands ----
    // Collect first, apply second (so neighborhood reads are pristine).
    std::vector<std::pair<int, unsigned>> reassign;
    std::vector<char> vis(size_t(w) * h, 0);
    int nbh[256];
    for (int y0 = 0; y0 < h; y0++) {
        for (int x0 = 0; x0 < w; x0++) {
            int const start = x0 + y0 * w;
            unsigned const idx0 = imap.getPixel(x0, y0);
            if (vis[start] || idx0 == IndexSentinel || real[start]) {
                continue;
            }
            // flood one non-real component
            cells.clear();
            stack.clear();
            stack.push_back(start);
            vis[start] = 1;
            while (!stack.empty()) {
                int const cur = stack.back();
                stack.pop_back();
                cells.push_back(cur);
                int const cx = cur % w;
                int const cy = cur / w;
                for (int k = 0; k < 8; k++) {
                    int const nx = cx + dx[k];
                    int const ny = cy + dy[k];
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
                        continue;
                    }
                    int const ni = nx + ny * w;
                    unsigned const nv = imap.getPixel(nx, ny);
                    if (nv == idx0 && !vis[ni] && !real[ni]) {
                        vis[ni] = 1;
                        stack.push_back(ni);
                    }
                }
            }
            // neighborhood majority (any index; own pixels excluded via real
            // mask: a non-real island by definition has no real own pixels)
            std::fill(nbh, nbh + 256, 0);
            for (int c : cells) {
                int const cx = c % w;
                int const cy = c / w;
                for (int k = 0; k < 8; k++) {
                    int const nx = cx + dx[k];
                    int const ny = cy + dy[k];
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
                        continue;
                    }
                    unsigned const nv = imap.getPixel(nx, ny);
                    if (nv != idx0 && nv != IndexSentinel) {
                        nbh[nv % 256]++;
                    }
                }
            }
            int best = -1;
            for (int i = 0; i < 256; i++) {
                if (nbh[i] > 0 && (best == -1 || nbh[i] > nbh[best])) {
                    best = i;
                }
            }
            if (best >= 0) {
                for (int c : cells) {
                    reassign.push_back({c, unsigned(best)});
                }
            }
            // best < 0: surrounded only by transparency — leave for pass 3
        }
    }
    for (auto const &[c, v] : reassign) {
        imap.setPixel(c % w, c / w, v);
    }

    // ---- Pass 3: absorb small islands isolated from real structure ----
    std::vector<char> visited(size_t(w) * h, 0);
    std::vector<char> inCell(size_t(w) * h, 0);
    for (int y0 = 0; y0 < h; y0++) {
        for (int x0 = 0; x0 < w; x0++) {
            int const start = x0 + y0 * w;
            unsigned const idx = imap.getPixel(x0, y0);
            if (visited[start] || idx == IndexSentinel) {
                continue;
            }
            std::fill(nbh, nbh + 256, 0);
            cells.clear();
            stack.clear();
            stack.push_back(start);
            visited[start] = 1;
            while (!stack.empty()) {
                int const cur = stack.back();
                stack.pop_back();
                cells.push_back(cur);
                inCell[cur] = 1;
                int const cx = cur % w;
                int const cy = cur / w;
                for (int k = 0; k < 8; k++) {
                    int const nx = cx + dx[k];
                    int const ny = cy + dy[k];
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
                        continue;
                    }
                    unsigned const nidx = imap.getPixel(nx, ny);
                    if (nidx == idx) {
                        int const ni = nx + ny * w;
                        if (!visited[ni]) {
                            visited[ni] = 1;
                            stack.push_back(ni);
                        }
                    } else if (nidx != IndexSentinel) {
                        nbh[nidx % 256]++;
                    }
                }
            }
            bool const small = int(cells.size()) < max_island;
            int const iso_cap = max_island * 8;

            // protection: real same-index structure within 1 px of the bbox
            int minx = w, maxx = -1, miny = h, maxy = -1;
            for (int c : cells) {
                int const cx = c % w;
                int const cy = c / w;
                if (cx < minx) minx = cx;
                if (cx > maxx) maxx = cx;
                if (cy < miny) miny = cy;
                if (cy > maxy) maxy = cy;
            }
            bool protected_by_real = false;
            {
                int const x1 = std::max(0, minx - 1);
                int const y1 = std::max(0, miny - 1);
                int const x2 = std::min(w - 1, maxx + 1);
                int const y2 = std::min(h - 1, maxy + 1);
                for (int yy = y1; yy <= y2 && !protected_by_real; yy++) {
                    for (int xx = x1; xx <= x2; xx++) {
                        int const p = xx + yy * w;
                        if (inCell[p]) {
                            continue;
                        }
                        if (real[p] && imap.getPixel(xx, yy) == idx) {
                            protected_by_real = true;
                            break;
                        }
                    }
                }
            }

            if (protected_by_real) {
                for (int c : cells) { inCell[c] = 0; }
                continue;
            }
            if (!small && int(cells.size()) > iso_cap) {
                // large + fully isolated: likely a deliberate lone feature
                for (int c : cells) { inCell[c] = 0; }
                continue;
            }
            int best = -1;
            for (int i = 0; i < 256; i++) {
                if (nbh[i] > 0 && (best == -1 || nbh[i] > nbh[best])) {
                    best = i;
                }
            }
            if (best < 0) {
                // surrounded only by transparency:
                // small pinholes in the background -> absorb; else keep
                if (!small) {
                    for (int c : cells) { inCell[c] = 0; }
                }
                continue;
            }
            for (int c : cells) {
                imap.setPixel(c % w, c / w, unsigned(best));
                inCell[c] = 0;
            }
        }
    }
}

} // namespace

/**
 * quantize an RGB image to a reduced number of colors.
 */
IndexedMap rgbMapQuantize(RgbMap const &rgbmap, int ncolor, int despeckle_island)
{
    assert(ncolor > 0);

    // IVF PATCH (alpha-quant): if the image has fully transparent pixels,
    // quantize a reduced palette (ncolor is for opaque content) — upstream
    // palette size is preserved, only the histogram input changes.
    auto imap = IndexedMap(rgbmap.width, rgbmap.height);

    Pool<Ocnode> pool;
    auto tree = octreeBuild(pool, rgbmap, ncolor);

    auto rgbs = std::make_unique<RGB[]>(ncolor);
    int index = 0;
    octreeIndex(tree, rgbs.get(), index);

    octreeDelete(pool, tree);

    // IVF PATCH (palette-sort): upstream sorted all `ncolor` slots even
    // though octreeIndex only filled `index` of them (the rest are zeroed
    // by value-initialization), corrupting the palette with spurious black
    // entries whenever the image used fewer colors than requested. Sort
    // only the valid entries.
    std::sort(rgbs.get(), rgbs.get() + index, [] (auto &ra, auto &rb) {
        return (ra.r + ra.g + ra.b) < (rb.r + rb.g + rb.b);
    });

    // IVF PATCH (palette-merge): collapse palette slots that differ by at
    // most PALETTE_MERGE_DIST (=2) per channel. AI upscalers inject 1-2
    // per-channel jitter into flat regions, and the octree then spends
    // several scans on near-identical colors (e.g. #7b4c4c / #7d4e4d),
    // each traced as an extra stacked layer that amplifies noise. The
    // threshold is deliberately tiny so real gradients (adjacent steps are
    // usually further apart) are untouched.
    constexpr int PALETTE_MERGE_DIST = 2;
    int kept = 0;
    for (int i = 0; i < index; i++) {
        bool dup = false;
        for (int j = 0; j < kept; j++) {
            if (std::abs(int(rgbs[i].r) - int(rgbs[j].r)) <= PALETTE_MERGE_DIST &&
                std::abs(int(rgbs[i].g) - int(rgbs[j].g)) <= PALETTE_MERGE_DIST &&
                std::abs(int(rgbs[i].b) - int(rgbs[j].b)) <= PALETTE_MERGE_DIST) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            rgbs[kept] = rgbs[i];
            kept++;
        }
    }
    index = kept;

    // IVF PATCH (remove-kmeans) [v1.7]: the v1.6 kmeans-refine call was
    // REMOVED. Lloyd iterations moved centroids without any minimum-
    // separation constraint and ran AFTER palette-merge, re-creating
    // near-identical palette entries that the merge had just collapsed;
    // with the Lab metric those slots split flat regions into speck
    // islands (the "motas" regression, docs/AUDITORIA_v4.md §CAUSA-A).
    // Upstream Inkscape has no k-means; the octree palette is kept as-is.

    // make the new map
    // fill in the color lookup table
    for (int i = 0; i < index; i++) {
        imap.clut[i] = rgbs[i];
    }
    imap.nrColors = index;

    // fill in new map pixels — upstream assignment (revert-lab-match),
    // using only the valid palette entries (0..index).
    for (int y = 0; y < rgbmap.height; y++) {
        for (int x = 0; x < rgbmap.width; x++) {
            auto rgb = rgbmap.getPixel(x, y);
            int idx = findRGB(rgbs.get(), index, rgb);
            imap.setPixel(x, y, idx);
        }
    }

    // IVF PATCH (index-despeckle): absorb tiny misquantized islands (see the
    // imapDespeckle comment) BEFORE the transparent sentinel is stamped, so
    // the pass never touches or creates sentinel entries.
    imapDespeckle(imap, despeckle_island);

    // IVF PATCH (alpha-quant): mark background pixels (alpha below the 50%
    // threshold, see alpha-threshold in imagemap.h) with the sentinel index.
    // [v1.5.3] fix: the test used to be alpha == 0, which treated barely
    // visible anti-aliased edge pixels (alpha 1..127) as SOLID content: on
    // the cube test image the frame's AA boundary against transparency left
    // 50 isolated black specks that no size-based despeckle could remove
    // (the "puntos negros" bug).
    if (rgbmap.hasAlpha()) {
        for (int y = 0; y < rgbmap.height; y++) {
            for (int x = 0; x < rgbmap.width; x++) {
                if (rgbmap.isTransparent(x, y)) {
                    imap.setPixel(x, y, (unsigned)-1);
                }
            }
        }
    }

    // IVF PATCH (alpha): propagate the source alpha map to the indexed map so
    // background-policy logic can tell transparent pixels apart.
    if (rgbmap.hasAlpha()) {
        imap.alpha = rgbmap.alpha;
    }

    return imap;
}

} // namespace Trace
} // namespace Inkscape
