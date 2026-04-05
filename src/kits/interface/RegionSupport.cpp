/* $Xorg: Region.c,v 1.6 2001/02/09 02:03:35 xorgcvs Exp $ */
/************************************************************************

Copyright 1987, 1988, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.


Copyright 1987, 1988 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

************************************************************************/
/* $XFree86: xc/lib/X11/Region.c,v 1.9 2002/06/04 22:19:57 dawes Exp $ */
/*
 * The functions in this file implement the BRegion* abstraction, similar to one
 * used in the X11 sample server. A BRegion* is simply an area, as the name
 * implies, and is implemented as a "y-x-banded" array of rectangles. To
 * explain: Each BRegion* is made up of a certain number of rectangles sorted
 * by y coordinate first, and then by x coordinate.
 *
 * Furthermore, the rectangles are banded such that every rectangle with a
 * given upper-left y coordinate (top) will have the same lower-right y
 * coordinate (bottom) and vice versa. If a rectangle has scanlines in a band, it
 * will span the entire vertical distance of the band. This means that some
 * areas that could be merged into a taller rectangle will be represented as
 * several shorter rectangles to account for shorter rectangles to its left
 * or right but within its "vertical scope".
 *
 * An added constraint on the rectangles is that they must cover as much
 * horizontal area as possible. E.g. no two rectangles in a band are allowed
 * to touch.
 *
 * Whenever possible, bands will be merged together to cover a greater vertical
 * distance (and thus reduce the number of rectangles). Two bands can be merged
 * only if the bottom of one touches the top of the other and they have
 * rectangles in the same places (of the same width, of course). This maintains
 * the y-x-banding that's so nice to have...
 */

#include "RegionSupport.h"

#include <stdlib.h>
#include <new>

using std::nothrow;

#include <SupportDefs.h>


#ifdef DEBUG
#include <stdio.h>
#define assert(expr) {if (!(expr)) fprintf(stderr,\
"Assertion failed file %s, line %d: " #expr "\n", __FILE__, __LINE__); }
#else
#define assert(expr)
#endif


/**
 * @brief Test whether two clipping_rects overlap.
 *
 * Evaluates to 1 if the two rectangles share at least one pixel, 0 otherwise.
 * Note that right and bottom coordinates are exclusive (not part of the region).
 */
#define EXTENTCHECK(r1, r2) \
	((r1)->right > (r2)->left && \
	 (r1)->left < (r2)->right && \
	 (r1)->bottom > (r2)->top && \
	 (r1)->top < (r2)->bottom)

/**
 * @brief Expand the bounding rectangle of \a idRect to include rectangle \a r.
 *
 * Updates each edge of idRect->fBounds in-place; used as new rectangles are
 * appended to a region during set operations.
 */
#define EXTENTS(r,idRect){\
            if ((r)->left < (idRect)->fBounds.left)\
              (idRect)->fBounds.left = (r)->left;\
            if ((r)->top < (idRect)->fBounds.top)\
              (idRect)->fBounds.top = (r)->top;\
            if ((r)->right > (idRect)->fBounds.right)\
              (idRect)->fBounds.right = (r)->right;\
            if ((r)->bottom > (idRect)->fBounds.bottom)\
              (idRect)->fBounds.bottom = (r)->bottom;\
        }

/**
 * @brief Grow the rectangle array of \a reg if it is full.
 *
 * When fCount is within one of fDataSize, the array is doubled via realloc().
 * Returns 0 (false) from the enclosing function on allocation failure.
 */
#define MEMCHECK(reg, rect, firstrect){\
        if ((reg)->fCount >= ((reg)->fDataSize - 1)){\
          (firstrect) = (clipping_rect *) realloc \
          ((char *)(firstrect), (unsigned) (2 * (sizeof(clipping_rect)) * ((reg)->fDataSize)));\
          if ((firstrect) == 0)\
            return(0);\
          (reg)->fDataSize *= 2;\
          (rect) = &(firstrect)[(reg)->fCount];\
         }\
       }

/**
 * @brief Guard macro: true when the last appended rectangle does not subsume the new one.
 *
 * Returns non-zero (safe to append) when the previous rectangle in \a Reg
 * does not already cover the candidate rectangle defined by (Rx1, Ry1, Rx2, Ry2).
 */
#define CHECK_PREVIOUS(Reg, R, Rx1, Ry1, Rx2, Ry2)\
               (!(((Reg)->fCount > 0)&&\
                  ((R-1)->top == (Ry1)) &&\
                  ((R-1)->bottom == (Ry2)) &&\
                  ((R-1)->left <= (Rx1)) &&\
                  ((R-1)->right >= (Rx2))))

/**
 * @brief Append a rectangle to a region and update its bounding box.
 *
 * The rectangle is only added when it is non-empty and is not already subsumed
 * by the previous rectangle (CHECK_PREVIOUS).  EXTENTS is called to grow
 * the region's fBounds accordingly.
 */
#define ADDRECT(reg, r, rx1, ry1, rx2, ry2){\
    if (((rx1) < (rx2)) && ((ry1) < (ry2)) &&\
        CHECK_PREVIOUS((reg), (r), (rx1), (ry1), (rx2), (ry2))){\
              (r)->left = (rx1);\
              (r)->top = (ry1);\
              (r)->right = (rx2);\
              (r)->bottom = (ry2);\
              EXTENTS((r), (reg));\
              (reg)->fCount++;\
              (r)++;\
            }\
        }



/**
 * @brief Append a rectangle to a region without updating the bounding box.
 *
 * Like ADDRECT but skips the EXTENTS call, so the caller is responsible for
 * maintaining fBounds separately.
 */
#define ADDRECTNOX(reg, r, rx1, ry1, rx2, ry2){\
            if ((rx1 < rx2) && (ry1 < ry2) &&\
                CHECK_PREVIOUS((reg), (r), (rx1), (ry1), (rx2), (ry2))){\
              (r)->left = (rx1);\
              (r)->top = (ry1);\
              (r)->right = (rx2);\
              (r)->bottom = (ry2);\
              (reg)->fCount++;\
              (r)++;\
            }\
        }

/** @brief Empty a region by calling its MakeEmpty() method. */
#define EMPTY_REGION(pReg) pReg->MakeEmpty()

/** @brief Evaluate to non-zero (true) when a region contains at least one rectangle. */
#define REGION_NOT_EMPTY(pReg) pReg->fCount

/**
 * @brief Test whether point (x, y) lies inside rectangle \a r.
 *
 * Uses half-open intervals: right and bottom edges are exclusive.
 */
#define INBOX(r, x, y) \
      ( ( ((r).right >  x)) && \
        ( ((r).left <= x)) && \
        ( ((r).bottom >  y)) && \
        ( ((r).top <= y)) )



/**
 * @brief Allocate and return a new empty BRegion on the heap.
 *
 * @return Pointer to a newly allocated, empty BRegion, or NULL if allocation
 *         fails.
 */
BRegion*
BRegion::Support::CreateRegion(void)
{
    return new (nothrow) BRegion();
}

/**
 * @brief Delete a BRegion previously created by CreateRegion().
 *
 * Safe to call with a NULL pointer.
 *
 * @param r Region to destroy.
 */
void
BRegion::Support::DestroyRegion(BRegion* r)
{
	delete r;
}


/**
 * @brief Recompute the bounding rectangle of a region from its rectangle array.
 *
 * Called by XSubtractRegion() and XIntersectRegion() because those operations
 * cannot easily maintain fBounds incrementally the way XUnionRegion() can.
 * If the region is empty all four edges are set to zero.
 *
 * @param pReg Region whose fBounds field is to be recalculated.
 */
void
BRegion::Support::miSetExtents(BRegion* pReg)
{
    clipping_rect*	pBox;
	clipping_rect* pBoxEnd;
	clipping_rect* pExtents;

    if (pReg->fCount == 0)
    {
	pReg->fBounds.left = 0;
	pReg->fBounds.top = 0;
	pReg->fBounds.right = 0;
	pReg->fBounds.bottom = 0;
	return;
    }

    pExtents = &pReg->fBounds;
    pBox = pReg->fData;
    pBoxEnd = &pBox[pReg->fCount - 1];

    /*
     * Since pBox is the first rectangle in the region, it must have the
     * smallest top and since pBoxEnd is the last rectangle in the region,
     * it must have the largest bottom, because of banding. Initialize left and
     * right from  pBox and pBoxEnd, resp., as good things to initialize them
     * to...
     */
    pExtents->left = pBox->left;
    pExtents->top = pBox->top;
    pExtents->right = pBoxEnd->right;
    pExtents->bottom = pBoxEnd->bottom;

    assert(pExtents->top < pExtents->bottom);
    while (pBox <= pBoxEnd)
    {
	if (pBox->left < pExtents->left)
	{
	    pExtents->left = pBox->left;
	}
	if (pBox->right > pExtents->right)
	{
	    pExtents->right = pBox->right;
	}
	pBox++;
    }
    assert(pExtents->left < pExtents->right);
}


/**
 * @brief Translate all rectangles and the bounding box of a region in place.
 *
 * Adds (\a x, \a y) to every coordinate of every rectangle in \a pRegion,
 * including fBounds.  No allocation is performed.
 *
 * @param pRegion Region to translate.
 * @param x       Horizontal displacement in pixels (may be negative).
 * @param y       Vertical displacement in pixels (may be negative).
 */
void
BRegion::Support::XOffsetRegion(
    BRegion* pRegion,
    int x,
    int y)
{
    int nbox;
    clipping_rect *pbox;

    pbox = pRegion->fData;
    nbox = pRegion->fCount;

    while(nbox--)
    {
	pbox->left += x;
	pbox->right += x;
	pbox->top += y;
	pbox->bottom += y;
	pbox++;
    }
    pRegion->fBounds.left += x;
    pRegion->fBounds.right += x;
    pRegion->fBounds.top += y;
    pRegion->fBounds.bottom += y;
}

/*
   Utility procedure Compress:
   Replace r by the region r', where
     p in r' iff (Quantifer m <= dx) (p + m in r), and
     Quantifier is Exists if grow is true, For all if grow is false, and
     (x,y) + m = (x+m,y) if xdir is true; (x,y+m) if xdir is false.

   Thus, if xdir is true and grow is false, r is replaced by the region
   of all points p such that p and the next dx points on the same
   horizontal scan line are all in r.  We do this using by noting
   that p is the head of a run of length 2^i + k iff p is the head
   of a run of length 2^i and p+2^i is the head of a run of length
   k. Thus, the loop invariant: s contains the region corresponding
   to the runs of length shift.  r contains the region corresponding
   to the runs of length 1 + dxo & (shift-1), where dxo is the original
   value of dx.  dx = dxo & ~(shift-1).  As parameters, s and t are
   scratch regions, so that we don't have to allocate them on every
   call.
*/

#if 0
#define ZOpRegion(a,b,c) if (grow) BRegion::Support::XUnionRegion(a,b,c); \
			 else BRegion::Support::XIntersectRegion(a,b,c)
#define ZShiftRegion(a,b) if (xdir) BRegion::Support::XOffsetRegion(a,b,0); \
			  else BRegion::Support::XOffsetRegion(a,0,b)
#define ZCopyRegion(a,b) BRegion::Support::XUnionRegion(a,a,b)

static void
Compress(
    BRegion* r, BRegion* s, BRegion* t,
    register unsigned dx,
    register int xdir, register int grow)
{
    register unsigned shift = 1;

    ZCopyRegion(r, s);
    while (dx) {
        if (dx & shift) {
            ZShiftRegion(r, -(int)shift);
            ZOpRegion(r, s, r);
            dx -= shift;
            if (!dx) break;
        }
        ZCopyRegion(s, t);
        ZShiftRegion(s, -(int)shift);
        ZOpRegion(s, t, s);
        shift <<= 1;
    }
}

#undef ZOpRegion
#undef ZShiftRegion
#undef ZCopyRegion

int
XShrinkRegion(
    BRegion* r,
    int dx, int dy)
{
    BRegion* s;
    BRegion* t;
    int grow;

    if (!dx && !dy) return 0;
    if ((! (s = CreateRegion()))  || (! (t = CreateRegion()))) return 0;
    if ((grow = (dx < 0))) dx = -dx;
    if (dx) Compress(r, s, t, (unsigned) 2*dx, true, grow);
    if ((grow = (dy < 0))) dy = -dy;
    if (dy) Compress(r, s, t, (unsigned) 2*dy, false, grow);
    XOffsetRegion(r, dx, dy);
    DestroyRegion(s);
    DestroyRegion(t);
    return 0;
}

#ifdef notdef
/***********************************************************
 *     Bop down the array of fData until we have passed
 *     scanline y.  fCount is the fDataSize of the array.
 ***********************************************************/

static clipping_rect*
IndexRects(
    register clipping_rect *rect,
    register int rectCount,
    register int y)
{
     while ((rectCount--) && (rect->bottom <= y))
        rect++;
     return(rect);
}
#endif
#endif // 0

/*======================================================================
 *	    BRegion* Intersection
 *====================================================================*/
/**
 * @brief Overlap-band callback for XIntersectRegion().
 *
 * Processes one horizontal band where both source regions have rectangles.
 * For each overlapping pair the intersection rectangle is appended to \a pReg.
 * Both rectangle lists are advanced past each rectangle as it is consumed.
 *
 * @param pReg    Destination region being built.
 * @param r1      First rectangle of the current band in region 1.
 * @param r1End   One past the last rectangle of the current band in region 1.
 * @param r2      First rectangle of the current band in region 2.
 * @param r2End   One past the last rectangle of the current band in region 2.
 * @param top     Top y coordinate of the current band.
 * @param bottom  Bottom y coordinate of the current band (exclusive).
 * @return 0 (lint).
 */
int
BRegion::Support::miIntersectO (
    BRegion*	pReg,
    clipping_rect*	r1,
    clipping_rect*  	  	r1End,
    clipping_rect*	r2,
    clipping_rect*  	  	r2End,
    int    	  	top,
    int    	  	bottom)
{
    int  	left;
    int  	right;
    clipping_rect*	pNextRect;

    pNextRect = &pReg->fData[pReg->fCount];

    while ((r1 != r1End) && (r2 != r2End))
    {
	left = max_c(r1->left,r2->left);
	right = min_c(r1->right,r2->right);

	/*
	 * If there's any overlap between the two rectangles, add that
	 * overlap to the new region.
	 * There's no need to check for subsumption because the only way
	 * such a need could arise is if some region has two rectangles
	 * right next to each other. Since that should never happen...
	 */
	if (left < right)
	{
	    assert(top<bottom);

	    MEMCHECK(pReg, pNextRect, pReg->fData);
	    pNextRect->left = left;
	    pNextRect->top = top;
	    pNextRect->right = right;
	    pNextRect->bottom = bottom;
	    pReg->fCount += 1;
	    pNextRect++;
	    assert(pReg->fCount <= pReg->fDataSize);
	}

	/*
	 * Need to advance the pointers. Shift the one that extends
	 * to the right the least, since the other still has a chance to
	 * overlap with that region's next rectangle, if you see what I mean.
	 */
	if (r1->right < r2->right)
	{
	    r1++;
	}
	else if (r2->right < r1->right)
	{
	    r2++;
	}
	else
	{
	    r1++;
	    r2++;
	}
    }
    return 0;	/* lint */
}

/**
 * @brief Compute the intersection of two regions and store it in \a newReg.
 *
 * Performs a trivial rejection when either region is empty or the bounding
 * boxes do not overlap.  Otherwise delegates to miRegionOp() with
 * miIntersectO() as the overlap handler and NULL non-overlap handlers
 * (non-overlapping portions contribute nothing to an intersection).
 * fBounds is recomputed by miSetExtents() after the operation.
 *
 * @param reg1   First source region.
 * @param reg2   Second source region.
 * @param newReg Destination region (may alias reg1 or reg2).
 * @return 1 always.
 */
int
BRegion::Support::XIntersectRegion(
    const BRegion* 	  	reg1,
    const BRegion*	  	reg2,          /* source regions     */
    BRegion* 	newReg)               /* destination BRegion* */
{
   /* check for trivial reject */
    if ( (!(reg1->fCount)) || (!(reg2->fCount))  ||
	(!EXTENTCHECK(&reg1->fBounds, &reg2->fBounds)))
        newReg->fCount = 0;
    else
	miRegionOp (newReg, reg1, reg2,
    		miIntersectO, NULL, NULL);

    /*
     * Can't alter newReg's fBounds before we call miRegionOp because
     * it might be one of the source regions and miRegionOp depends
     * on the fBounds of those regions being the same. Besides, this
     * way there's no checking against rectangles that will be nuked
     * due to coalescing, so we have to examine fewer rectangles.
     */
    miSetExtents(newReg);
    return 1;
}

/**
 * @brief Copy one region into another.
 *
 * Uses BRegion's copy-assignment operator, which performs a deep copy of
 * the rectangle array and the bounding box.
 *
 * @param dstrgn Destination region.
 * @param rgn    Source region.
 */
void
BRegion::Support::miRegionCopy(
    BRegion* dstrgn,
    const BRegion* rgn)

{
	*dstrgn = *rgn;
}

#if 0
#ifdef notdef

/*
 *  combinRegs(newReg, reg1, reg2)
 *    if one region is above or below the other.
*/

static void
combineRegs(
    register BRegion* newReg,
    BRegion* reg1,
    BRegion* reg2)
{
    register BRegion* tempReg;
    register clipping_rect *rects_;
    register clipping_rect *rects1;
    register clipping_rect *rects2;
    register int total;

    rects1 = reg1->fData;
    rects2 = reg2->fData;

    total = reg1->fCount + reg2->fCount;
    if (! (tempReg = CreateRegion()))
	return;
    tempReg->fDataSize = total;
    /*  region 1 is below region 2  */
    if (reg1->fBounds.top > reg2->fBounds.top)
    {
        miRegionCopy(tempReg, reg2);
        rects_ = &tempReg->fData[tempReg->fCount];
        total -= tempReg->fCount;
        while (total--)
            *rects_++ = *rects1++;
    }
    else
    {
        miRegionCopy(tempReg, reg1);
        rects_ = &tempReg->fData[tempReg->fCount];
        total -= tempReg->fCount;
        while (total--)
            *rects_++ = *rects2++;
    }
    tempReg->fBounds = reg1->fBounds;
    tempReg->fCount = reg1->fCount + reg2->fCount;
    EXTENTS(&reg2->fBounds, tempReg);
    miRegionCopy(newReg, tempReg);

    DestroyRegion(tempReg);
}

/*
 *  QuickCheck checks to see if it does not have to go through all the
 *  the ugly code for the region call.  It returns 1 if it did all
 *  the work for Union, otherwise 0 - still work to be done.
*/

static int
QuickCheck(BRegion* newReg, BRegion* reg1, BRegion* reg2)
{

    /*  if unioning with itself or no fData to union with  */
    if ( (reg1 == reg2) || (!(reg1->fCount)) )
    {
        miRegionCopy(newReg, reg2);
        return true;
    }

    /*   if nothing to union   */
    if (!(reg2->fCount))
    {
        miRegionCopy(newReg, reg1);
        return true;
    }

    /*   could put an extent check to see if add above or below */

    if ((reg1->fBounds.top >= reg2->fBounds.bottom) ||
        (reg2->fBounds.top >= reg1->fBounds.bottom) )
    {
        combineRegs(newReg, reg1, reg2);
        return true;
    }
    return false;
}

/*   TopRects(fData, reg1, reg2)
 * N.B. We now assume that reg1 and reg2 intersect.  Therefore we are
 * NOT checking in the two while loops for stepping off the end of the
 * region.
 */

static int
TopRects(
    register BRegion* newReg,
    register clipping_rect *rects_,
    register BRegion* reg1,
    register BRegion* reg2,
    clipping_rect *FirstRect)
{
    register clipping_rect *tempRects;

    /*  need to add some fData from region 1 */
    if (reg1->fBounds.top < reg2->fBounds.top)
    {
        tempRects = reg1->fData;
        while(tempRects->top < reg2->fBounds.top)
	{
	    MEMCHECK(newReg, rects_, FirstRect);
            ADDRECTNOX(newReg,rects_, tempRects->left, tempRects->top,
		       tempRects->right, min_c(tempRects->bottom, reg2->fBounds.top));
            tempRects++;
	}
    }
    /*  need to add some fData from region 2 */
    if (reg2->fBounds.top < reg1->fBounds.top)
    {
        tempRects = reg2->fData;
        while (tempRects->top < reg1->fBounds.top)
        {
            MEMCHECK(newReg, rects_, FirstRect);
            ADDRECTNOX(newReg, rects_, tempRects->left,tempRects->top,
		       tempRects->right, min_c(tempRects->bottom, reg1->fBounds.top));
            tempRects++;
	}
    }
    return 1;
}
#endif // notdef
#endif // 0

/*======================================================================
 *	    Generic BRegion* Operator
 *====================================================================*/

/**
 * @brief Attempt to merge the current band with the previous band in \a pReg.
 *
 * Two adjacent horizontal bands can be merged into taller rectangles when
 * they have the same number of rectangles, the bottom of the previous band
 * equals the top of the current band, and every pair of rectangles shares
 * the same left and right edges.  On success the duplicate rectangles are
 * removed and the bottom of each previous-band rectangle is extended.
 *
 * @param pReg      Region being built by miRegionOp().
 * @param prevStart Index into pReg->fData of the first rectangle in the previous band.
 * @param curStart  Index into pReg->fData of the first rectangle in the current band.
 * @return The updated index to use as \c prevStart in the next miCoalesce() call.
 */
int
BRegion::Support::miCoalesce(
    BRegion*	pReg,	    	/* BRegion* to coalesce */
    int	    	  	prevStart,  	/* Index of start of previous band */
    int	    	  	curStart)   	/* Index of start of current band */
{
    clipping_rect*	pPrevBox;   	/* Current box in previous band */
    clipping_rect*	pCurBox;    	/* Current box in current band */
    clipping_rect*	pRegEnd;    	/* End of region */
    int	    	  	curNumRects;	/* Number of rectangles in current
					 * band */
    int	    	  	prevNumRects;	/* Number of rectangles in previous
					 * band */
    int	    	  	bandY1;	    	/* Y1 coordinate for current band */

    pRegEnd = &pReg->fData[pReg->fCount];

    pPrevBox = &pReg->fData[prevStart];
    prevNumRects = curStart - prevStart;

    /*
     * Figure out how many rectangles are in the current band. Have to do
     * this because multiple bands could have been added in miRegionOp
     * at the end when one region has been exhausted.
     */
    pCurBox = &pReg->fData[curStart];
    bandY1 = pCurBox->top;
    for (curNumRects = 0;
	 (pCurBox != pRegEnd) && (pCurBox->top == bandY1);
	 curNumRects++)
    {
	pCurBox++;
    }

    if (pCurBox != pRegEnd)
    {
	/*
	 * If more than one band was added, we have to find the start
	 * of the last band added so the next coalescing job can start
	 * at the right place... (given when multiple bands are added,
	 * this may be pointless -- see above).
	 */
	pRegEnd--;
	while (pRegEnd[-1].top == pRegEnd->top)
	{
	    pRegEnd--;
	}
	curStart = pRegEnd - pReg->fData;
	pRegEnd = pReg->fData + pReg->fCount;
    }

    if ((curNumRects == prevNumRects) && (curNumRects != 0)) {
	pCurBox -= curNumRects;
	/*
	 * The bands may only be coalesced if the bottom of the previous
	 * matches the top scanline of the current.
	 */
	if (pPrevBox->bottom == pCurBox->top)
	{
	    /*
	     * Make sure the bands have boxes in the same places. This
	     * assumes that boxes have been added in such a way that they
	     * cover the most area possible. I.e. two boxes in a band must
	     * have some horizontal space between them.
	     */
	    do
	    {
		if ((pPrevBox->left != pCurBox->left) ||
		    (pPrevBox->right != pCurBox->right))
		{
		    /*
		     * The bands don't line up so they can't be coalesced.
		     */
		    return (curStart);
		}
		pPrevBox++;
		pCurBox++;
		prevNumRects -= 1;
	    } while (prevNumRects != 0);

	    pReg->fCount -= curNumRects;
	    pCurBox -= curNumRects;
	    pPrevBox -= curNumRects;

	    /*
	     * The bands may be merged, so set the bottom y of each box
	     * in the previous band to that of the corresponding box in
	     * the current band.
	     */
	    do
	    {
		pPrevBox->bottom = pCurBox->bottom;
		pPrevBox++;
		pCurBox++;
		curNumRects -= 1;
	    } while (curNumRects != 0);

	    /*
	     * If only one band was added to the region, we have to backup
	     * curStart to the start of the previous band.
	     *
	     * If more than one band was added to the region, copy the
	     * other bands down. The assumption here is that the other bands
	     * came from the same region as the current one and no further
	     * coalescing can be done on them since it's all been done
	     * already... curStart is already in the right place.
	     */
	    if (pCurBox == pRegEnd)
	    {
		curStart = prevStart;
	    }
	    else
	    {
		do
		{
		    *pPrevBox++ = *pCurBox++;
		} while (pCurBox != pRegEnd);
	    }

	}
    }
    return (curStart);
}

/**
 * @brief Generic region set-operation engine used by union, intersection, and subtraction.
 *
 * Sweeps two y-x-banded regions in lock-step.  For each horizontal band the
 * function categorises the area as:
 *  - non-overlapping (only region 1 has rectangles): calls \a nonOverlap1Func.
 *  - non-overlapping (only region 2 has rectangles): calls \a nonOverlap2Func.
 *  - overlapping (both regions have rectangles):     calls \a overlapFunc.
 *
 * After processing each band miCoalesce() is called to merge it with the
 * previous band when possible, keeping the rectangle count minimal.
 *
 * @param newReg          Destination region (may alias reg1 or reg2).
 * @param reg1            First source region.
 * @param reg2            Second source region.
 * @param overlapFunc     Callback invoked for bands covered by both regions.
 * @param nonOverlap1Func Callback invoked for bands covered only by reg1, or NULL.
 * @param nonOverlap2Func Callback invoked for bands covered only by reg2, or NULL.
 *
 * @note fBounds is not updated by this function; callers must call miSetExtents()
 *       or set fBounds manually afterwards.
 *
 * @see miCoalesce()
 * @see XIntersectRegion()
 * @see XUnionRegion()
 * @see XSubtractRegion()
 *
 *-----------------------------------------------------------------------
 */
void
BRegion::Support::miRegionOp(
    BRegion* 	newReg,	    	    	/* Place to store result */
    const BRegion*	  	reg1,	    	    	/* First region in operation */
    const BRegion*	  	reg2,	    	    	/* 2d region in operation */
    int    	  	(*overlapFunc)(
        BRegion*     pReg,
        clipping_rect*     r1,
        clipping_rect*              r1End,
        clipping_rect*     r2,
        clipping_rect*              r2End,
        int               top,
        int               bottom),                /* Function to call for over-
						 * lapping bands */
    int    	  	(*nonOverlap1Func)(
        BRegion*     pReg,
        clipping_rect*     r,
        clipping_rect*              rEnd,
        int      top,
        int      bottom),                /* Function to call for non-
						 * overlapping bands in region
						 * 1 */
    int    	  	(*nonOverlap2Func)(
        BRegion*     pReg,
        clipping_rect*     r,
        clipping_rect*              rEnd,
        int      top,
        int      bottom))                /* Function to call for non-
						 * overlapping bands in region
						 * 2 */
{
    clipping_rect*	r1; 	    	    	/* Pointer into first region */
    clipping_rect*	r2; 	    	    	/* Pointer into 2d region */
    clipping_rect*  	  	r1End;	    	    	/* End of 1st region */
    clipping_rect*  	  	r2End;	    	    	/* End of 2d region */
    int  	ybot;	    	    	/* Bottom of intersection */
    int  	ytop;	    	    	/* Top of intersection */
 //   clipping_rect*  	  	oldRects;   	    	/* Old fData for newReg */
    int	    	  	prevBand;   	    	/* Index of start of
						 * previous band in newReg */
    int	    	  	curBand;    	    	/* Index of start of current
						 * band in newReg */
    clipping_rect* 	r1BandEnd;  	    	/* End of current band in r1 */
    clipping_rect* 	r2BandEnd;  	    	/* End of current band in r2 */
    int     	  	top;	    	    	/* Top of non-overlapping
						 * band */
    int     	  	bot;	    	    	/* Bottom of non-overlapping
						 * band */

    /*
     * Initialization:
     *	set r1, r2, r1End and r2End appropriately, preserve the important
     * parts of the destination region until the end in case it's one of
     * the two source regions, then mark the "new" region empty, allocating
     * another array of rectangles for it to use.
     */
    r1 = reg1->fData;
    r2 = reg2->fData;
    r1End = r1 + reg1->fCount;
    r2End = r2 + reg2->fCount;

//    oldRects = newReg->fData;

    EMPTY_REGION(newReg);

    /*
     * Allocate a reasonable number of rectangles for the new region. The idea
     * is to allocate enough so the individual functions don't need to
     * reallocate and copy the array, which is time consuming, yet we don't
     * have to worry about using too much memory. I hope to be able to
     * nuke the realloc() at the end of this function eventually.
     */
    if (!newReg->_SetSize(max_c(reg1->fCount,reg2->fCount) * 2)) {
		return;
    }

    /*
     * Initialize ybot and ytop.
     * In the upcoming loop, ybot and ytop serve different functions depending
     * on whether the band being handled is an overlapping or non-overlapping
     * band.
     * 	In the case of a non-overlapping band (only one of the regions
     * has points in the band), ybot is the bottom of the most recent
     * intersection and thus clips the top of the rectangles in that band.
     * ytop is the top of the next intersection between the two regions and
     * serves to clip the bottom of the rectangles in the current band.
     *	For an overlapping band (where the two regions intersect), ytop clips
     * the top of the rectangles of both regions and ybot clips the bottoms.
     */
    if (reg1->fBounds.top < reg2->fBounds.top)
	ybot = reg1->fBounds.top;
    else
	ybot = reg2->fBounds.top;

    /*
     * prevBand serves to mark the start of the previous band so rectangles
     * can be coalesced into larger rectangles. qv. miCoalesce, above.
     * In the beginning, there is no previous band, so prevBand == curBand
     * (curBand is set later on, of course, but the first band will always
     * start at index 0). prevBand and curBand must be indices because of
     * the possible expansion, and resultant moving, of the new region's
     * array of rectangles.
     */
    prevBand = 0;

    do
    {
	curBand = newReg->fCount;

	/*
	 * This algorithm proceeds one source-band (as opposed to a
	 * destination band, which is determined by where the two regions
	 * intersect) at a time. r1BandEnd and r2BandEnd serve to mark the
	 * rectangle after the last one in the current band for their
	 * respective regions.
	 */
	r1BandEnd = r1;
	while ((r1BandEnd != r1End) && (r1BandEnd->top == r1->top))
	{
	    r1BandEnd++;
	}

	r2BandEnd = r2;
	while ((r2BandEnd != r2End) && (r2BandEnd->top == r2->top))
	{
	    r2BandEnd++;
	}

	/*
	 * First handle the band that doesn't intersect, if any.
	 *
	 * Note that attention is restricted to one band in the
	 * non-intersecting region at once, so if a region has n
	 * bands between the current position and the next place it overlaps
	 * the other, this entire loop will be passed through n times.
	 */
	if (r1->top < r2->top)
	{
	    top = max_c(r1->top,ybot);
	    bot = min_c(r1->bottom,r2->top);

	    if ((top != bot) && (nonOverlap1Func != NULL))
	    {
		(* nonOverlap1Func) (newReg, r1, r1BandEnd, top, bot);
	    }

	    ytop = r2->top;
	}
	else if (r2->top < r1->top)
	{
	    top = max_c(r2->top,ybot);
	    bot = min_c(r2->bottom,r1->top);

	    if ((top != bot) && (nonOverlap2Func != NULL))
	    {
		(* nonOverlap2Func) (newReg, r2, r2BandEnd, top, bot);
	    }

	    ytop = r1->top;
	}
	else
	{
	    ytop = r1->top;
	}

	/*
	 * If any rectangles got added to the region, try and coalesce them
	 * with rectangles from the previous band. Note we could just do
	 * this test in miCoalesce, but some machines incur a not
	 * inconsiderable cost for function calls, so...
	 */
	if (newReg->fCount != curBand)
	{
	    prevBand = miCoalesce (newReg, prevBand, curBand);
	}

	/*
	 * Now see if we've hit an intersecting band. The two bands only
	 * intersect if ybot > ytop
	 */
	ybot = min_c(r1->bottom, r2->bottom);
	curBand = newReg->fCount;
	if (ybot > ytop)
	{
	    (* overlapFunc) (newReg, r1, r1BandEnd, r2, r2BandEnd, ytop, ybot);

	}

	if (newReg->fCount != curBand)
	{
	    prevBand = miCoalesce (newReg, prevBand, curBand);
	}

	/*
	 * If we've finished with a band (bottom == ybot) we skip forward
	 * in the region to the next band.
	 */
	if (r1->bottom == ybot)
	{
	    r1 = r1BandEnd;
	}
	if (r2->bottom == ybot)
	{
	    r2 = r2BandEnd;
	}
    } while ((r1 != r1End) && (r2 != r2End));

    /*
     * Deal with whichever region still has rectangles left.
     */
    curBand = newReg->fCount;
    if (r1 != r1End)
    {
	if (nonOverlap1Func != NULL)
	{
	    do
	    {
		r1BandEnd = r1;
		while ((r1BandEnd < r1End) && (r1BandEnd->top == r1->top))
		{
		    r1BandEnd++;
		}
		(* nonOverlap1Func) (newReg, r1, r1BandEnd,
				     max_c(r1->top,ybot), r1->bottom);
		r1 = r1BandEnd;
	    } while (r1 != r1End);
	}
    }
    else if ((r2 != r2End) && (nonOverlap2Func != NULL))
    {
	do
	{
	    r2BandEnd = r2;
	    while ((r2BandEnd < r2End) && (r2BandEnd->top == r2->top))
	    {
		 r2BandEnd++;
	    }
	    (* nonOverlap2Func) (newReg, r2, r2BandEnd,
				max_c(r2->top,ybot), r2->bottom);
	    r2 = r2BandEnd;
	} while (r2 != r2End);
    }

    if (newReg->fCount != curBand)
    {
	(void) miCoalesce (newReg, prevBand, curBand);
    }

    /*
     * A bit of cleanup. To keep regions from growing without bound,
     * we shrink the array of rectangles to match the new number of
     * rectangles in the region. This never goes to 0, however...
     *
     * Only do this stuff if the number of rectangles allocated is more than
     * twice the number of rectangles in the region (a simple optimization...).
     */
//    if (newReg->fCount < (newReg->fDataSize >> 1))
//    {
//	if (REGION_NOT_EMPTY(newReg))
//	{
//	    clipping_rect* prev_rects = newReg->fData;
//	    newReg->fDataSize = newReg->fCount;
//	    newReg->fData = (clipping_rect*) realloc ((char *) newReg->fData,
//				   (unsigned) (sizeof(clipping_rect) * newReg->fDataSize));
//	    if (! newReg->fData)
//		newReg->fData = prev_rects;
//	}
//	else
//	{
//	    /*
//	     * No point in doing the extra work involved in an realloc if
//	     * the region is empty
//	     */
//	    newReg->fDataSize = 1;
//	    free((char *) newReg->fData);
//	    newReg->fData = (clipping_rect*) malloc(sizeof(clipping_rect));
//	}
//    }
//    free ((char *) oldRects);
    return;
}


/*======================================================================
 *	    BRegion* Union
 *====================================================================*/

/**
 * @brief Non-overlap-band callback for XUnionRegion().
 *
 * Appends each rectangle in the band [r, rEnd) directly to \a pReg with the
 * supplied vertical extent [\a top, \a bottom).  No subsumption check is
 * needed because in a union all non-overlapping area is always included.
 *
 * @param pReg   Destination region being built.
 * @param r      First rectangle of the non-overlapping band.
 * @param rEnd   One past the last rectangle of the band.
 * @param top    Top y coordinate of the band.
 * @param bottom Bottom y coordinate of the band (exclusive).
 * @return 0 (lint).
 */
int
BRegion::Support::miUnionNonO(BRegion* pReg,
	clipping_rect* r, clipping_rect* rEnd,
    int top, int bottom)
{
	clipping_rect*	pNextRect = &pReg->fData[pReg->fCount];

	assert(top < bottom);

	while (r != rEnd) {
		assert(r->left < r->right);
		MEMCHECK(pReg, pNextRect, pReg->fData);
		pNextRect->left = r->left;
		pNextRect->top = top;
		pNextRect->right = r->right;
		pNextRect->bottom = bottom;
		pReg->fCount += 1;
		pNextRect++;

		assert(pReg->fCount<=pReg->fDataSize);
		r++;
	}
	return 0;
}


/**
 * @brief Overlap-band callback for XUnionRegion().
 *
 * Merges rectangles from both bands into \a pReg by always picking the
 * left-most leading edge.  Adjacent or overlapping rectangles within the
 * merged band are coalesced horizontally via the MERGERECT macro so that
 * the band contains the minimum number of non-touching rectangles.
 *
 * @param pReg   Destination region being built.
 * @param r1     First rectangle of the current band in region 1.
 * @param r1End  One past the last rectangle of the current band in region 1.
 * @param r2     First rectangle of the current band in region 2.
 * @param r2End  One past the last rectangle of the current band in region 2.
 * @param top    Top y coordinate of the current band.
 * @param bottom Bottom y coordinate of the current band (exclusive).
 * @return 0 (lint).
 */
int
BRegion::Support::miUnionO (
    BRegion*	pReg,
    clipping_rect*	r1,
    clipping_rect*  	  	r1End,
    clipping_rect*	r2,
    clipping_rect*  	  	r2End,
    int	top,
    int	bottom)
{
    clipping_rect*	pNextRect;

    pNextRect = &pReg->fData[pReg->fCount];

#define MERGERECT(r) \
    if ((pReg->fCount != 0) &&  \
	(pNextRect[-1].top == top) &&  \
	(pNextRect[-1].bottom == bottom) &&  \
	(pNextRect[-1].right >= r->left))  \
    {  \
	if (pNextRect[-1].right < r->right)  \
	{  \
	    pNextRect[-1].right = r->right;  \
	    assert(pNextRect[-1].left<pNextRect[-1].right); \
	}  \
    }  \
    else  \
    {  \
	MEMCHECK(pReg, pNextRect, pReg->fData);  \
	pNextRect->top = top;  \
	pNextRect->bottom = bottom;  \
	pNextRect->left = r->left;  \
	pNextRect->right = r->right;  \
	pReg->fCount += 1;  \
        pNextRect += 1;  \
    }  \
    assert(pReg->fCount<=pReg->fDataSize);\
    r++;

    assert (top<bottom);
    while ((r1 != r1End) && (r2 != r2End))
    {
	if (r1->left < r2->left)
	{
	    MERGERECT(r1);
	}
	else
	{
	    MERGERECT(r2);
	}
    }

    if (r1 != r1End)
    {
	do
	{
	    MERGERECT(r1);
	} while (r1 != r1End);
    }
    else while (r2 != r2End)
    {
	MERGERECT(r2);
    }
    return 0;	/* lint */
}

/**
 * @brief Compute the union of two regions and store the result in \a newReg.
 *
 * Several fast paths are checked before the general miRegionOp() path:
 *  - same pointer or empty reg1: copy reg2.
 *  - empty reg2: copy reg1.
 *  - reg1 subsumes reg2 (single rect): copy reg1.
 *  - reg2 subsumes reg1 (single rect): copy reg2.
 *
 * After the general operation fBounds is set to the bounding box of
 * both source regions' bounds (always correct for a union).
 *
 * @param reg1   First source region.
 * @param reg2   Second source region.
 * @param newReg Destination region (may alias reg1 or reg2).
 * @return 1 always.
 */
int
BRegion::Support::XUnionRegion(
    const BRegion* 	  reg1,
    const BRegion*	  reg2,             /* source regions     */
    BRegion* 	  newReg)                  /* destination BRegion* */
{
    /*  checks all the simple cases */

    /*
     * BRegion* 1 and 2 are the same or region 1 is empty
     */
    if ( (reg1 == reg2) || (!(reg1->fCount)) )
    {
        if (newReg != reg2)
            miRegionCopy(newReg, reg2);
        return 1;
    }

    /*
     * if nothing to union (region 2 empty)
     */
    if (!(reg2->fCount))
    {
        if (newReg != reg1)
            miRegionCopy(newReg, reg1);
        return 1;
    }

    /*
     * BRegion* 1 completely subsumes region 2
     */
    if ((reg1->fCount == 1) &&
	(reg1->fBounds.left <= reg2->fBounds.left) &&
	(reg1->fBounds.top <= reg2->fBounds.top) &&
	(reg1->fBounds.right >= reg2->fBounds.right) &&
	(reg1->fBounds.bottom >= reg2->fBounds.bottom))
    {
        if (newReg != reg1)
            miRegionCopy(newReg, reg1);
        return 1;
    }

    /*
     * BRegion* 2 completely subsumes region 1
     */
    if ((reg2->fCount == 1) &&
	(reg2->fBounds.left <= reg1->fBounds.left) &&
	(reg2->fBounds.top <= reg1->fBounds.top) &&
	(reg2->fBounds.right >= reg1->fBounds.right) &&
	(reg2->fBounds.bottom >= reg1->fBounds.bottom))
    {
        if (newReg != reg2)
            miRegionCopy(newReg, reg2);
        return 1;
    }

    miRegionOp (newReg, reg1, reg2, miUnionO,
    		miUnionNonO, miUnionNonO);

    newReg->fBounds.left = min_c(reg1->fBounds.left, reg2->fBounds.left);
    newReg->fBounds.top = min_c(reg1->fBounds.top, reg2->fBounds.top);
    newReg->fBounds.right = max_c(reg1->fBounds.right, reg2->fBounds.right);
    newReg->fBounds.bottom = max_c(reg1->fBounds.bottom, reg2->fBounds.bottom);

    return 1;
}


/*======================================================================
 * 	    	  BRegion* Subtraction
 *====================================================================*/

/**
 * @brief Non-overlap-band callback for the minuend side of XSubtractRegion().
 *
 * Appends each rectangle from region 1's non-overlapping band to \a pReg.
 * Rectangles from region 2's non-overlapping bands are silently discarded
 * (NULL is passed as nonOverlap2Func in XSubtractRegion()).
 *
 * @param pReg   Destination region being built.
 * @param r      First rectangle of the non-overlapping band from region 1.
 * @param rEnd   One past the last rectangle of the band.
 * @param top    Top y coordinate of the band.
 * @param bottom Bottom y coordinate of the band (exclusive).
 * @return 0 (lint).
 */
int
BRegion::Support::miSubtractNonO1 (
    BRegion*	pReg,
    clipping_rect*	r,
    clipping_rect*  	  	rEnd,
    int  	top,
    int   	bottom)
{
    clipping_rect*	pNextRect;

    pNextRect = &pReg->fData[pReg->fCount];

    assert(top<bottom);

    while (r != rEnd)
    {
	assert(r->left<r->right);
	MEMCHECK(pReg, pNextRect, pReg->fData);
	pNextRect->left = r->left;
	pNextRect->top = top;
	pNextRect->right = r->right;
	pNextRect->bottom = bottom;
	pReg->fCount += 1;
	pNextRect++;

	assert(pReg->fCount <= pReg->fDataSize);

	r++;
    }
    return 0;	/* lint */
}

/**
 * @brief Overlap-band callback for XSubtractRegion().
 *
 * Computes the portions of the current minuend band (r1) that are not
 * covered by the current subtrahend band (r2) and appends them to \a pReg.
 * The algorithm maintains a "left fence" that tracks how far right the
 * current minuend segment has been consumed.
 *
 * @param pReg   Destination region being built.
 * @param r1     First rectangle of the current band in the minuend region.
 * @param r1End  One past the last rectangle of the current minuend band.
 * @param r2     First rectangle of the current band in the subtrahend region.
 * @param r2End  One past the last rectangle of the current subtrahend band.
 * @param top    Top y coordinate of the current band.
 * @param bottom Bottom y coordinate of the current band (exclusive).
 * @return 0 (lint).
 */
int
BRegion::Support::miSubtractO(
    BRegion*	pReg,
    clipping_rect*	r1,
    clipping_rect*  	  	r1End,
    clipping_rect*	r2,
    clipping_rect*  	  	r2End,
    int  	top,
    int  	bottom)
{
    clipping_rect*	pNextRect;
    int  	left;

    left = r1->left;

    assert(top<bottom);
    pNextRect = &pReg->fData[pReg->fCount];

    while ((r1 != r1End) && (r2 != r2End))
    {
	if (r2->right <= left)
	{
	    /*
	     * Subtrahend missed the boat: go to next subtrahend.
	     */
	    r2++;
	}
	else if (r2->left <= left)
	{
	    /*
	     * Subtrahend preceeds minuend: nuke left edge of minuend.
	     */
	    left = r2->right;
	    if (left >= r1->right)
	    {
		/*
		 * Minuend completely covered: advance to next minuend and
		 * reset left fence to edge of new minuend.
		 */
		r1++;
		if (r1 != r1End)
		    left = r1->left;
	    }
	    else
	    {
		/*
		 * Subtrahend now used up since it doesn't extend beyond
		 * minuend
		 */
		r2++;
	    }
	}
	else if (r2->left < r1->right)
	{
	    /*
	     * Left part of subtrahend covers part of minuend: add uncovered
	     * part of minuend to region and skip to next subtrahend.
	     */
	    assert(left<r2->left);
	    MEMCHECK(pReg, pNextRect, pReg->fData);
	    pNextRect->left = left;
	    pNextRect->top = top;
	    pNextRect->right = r2->left;
	    pNextRect->bottom = bottom;
	    pReg->fCount += 1;
	    pNextRect++;

	    assert(pReg->fCount<=pReg->fDataSize);

	    left = r2->right;
	    if (left >= r1->right)
	    {
		/*
		 * Minuend used up: advance to new...
		 */
		r1++;
		if (r1 != r1End)
		    left = r1->left;
	    }
	    else
	    {
		/*
		 * Subtrahend used up
		 */
		r2++;
	    }
	}
	else
	{
	    /*
	     * Minuend used up: add any remaining piece before advancing.
	     */
	    if (r1->right > left)
	    {
		MEMCHECK(pReg, pNextRect, pReg->fData);
		pNextRect->left = left;
		pNextRect->top = top;
		pNextRect->right = r1->right;
		pNextRect->bottom = bottom;
		pReg->fCount += 1;
		pNextRect++;
		assert(pReg->fCount<=pReg->fDataSize);
	    }
	    r1++;
	    if (r1 != r1End)
		left = r1->left;
	}
    }

    /*
     * Add remaining minuend rectangles to region.
     */
    while (r1 != r1End)
    {
	assert(left<r1->right);
	MEMCHECK(pReg, pNextRect, pReg->fData);
	pNextRect->left = left;
	pNextRect->top = top;
	pNextRect->right = r1->right;
	pNextRect->bottom = bottom;
	pReg->fCount += 1;
	pNextRect++;

	assert(pReg->fCount<=pReg->fDataSize);

	r1++;
	if (r1 != r1End)
	{
	    left = r1->left;
	}
    }
    return 0;	/* lint */
}

/**
 * @brief Subtract region \a regS from region \a regM and store the result in \a regD.
 *
 * Performs a trivial rejection when the minuend is empty, the subtrahend is
 * empty, or the bounding boxes do not overlap (in which case \a regM is copied
 * to \a regD unchanged).  Otherwise delegates to miRegionOp() with
 * miSubtractO() and miSubtractNonO1() and then recomputes fBounds via
 * miSetExtents().
 *
 * @param regM Minuend region (the region being subtracted from).
 * @param regS Subtrahend region (the region to subtract).
 * @param regD Destination region for the difference (may alias regM or regS).
 * @return 1 always.
 */
int
BRegion::Support::XSubtractRegion(
    const BRegion* 	  	regM,
    const BRegion*	  	regS,
    BRegion*	regD)
{
   /* check for trivial reject */
    if ( (!(regM->fCount)) || (!(regS->fCount))  ||
	(!EXTENTCHECK(&regM->fBounds, &regS->fBounds)) )
    {
	miRegionCopy(regD, regM);
        return 1;
    }

    miRegionOp (regD, regM, regS, miSubtractO,
    		miSubtractNonO1, NULL);

    /*
     * Can't alter newReg's fBounds before we call miRegionOp because
     * it might be one of the source regions and miRegionOp depends
     * on the fBounds of those regions being the unaltered. Besides, this
     * way there's no checking against rectangles that will be nuked
     * due to coalescing, so we have to examine fewer rectangles.
     */
    miSetExtents (regD);
    return 1;
}

/**
 * @brief Compute the symmetric difference (XOR) of two regions.
 *
 * Calculates (sra - srb) union (srb - sra) using two temporary regions.
 * The result contains all pixels that are in exactly one of the two source
 * regions.
 *
 * @param sra First source region.
 * @param srb Second source region.
 * @param dr  Destination region for the symmetric difference.
 * @return 0 on success; 0 also on allocation failure (result will be empty).
 */
int
BRegion::Support::XXorRegion(const BRegion* sra, const BRegion* srb,
	BRegion* dr)
{
    BRegion* tra = NULL;
    BRegion* trb = NULL;

    if ((! (tra = CreateRegion())) || (! (trb = CreateRegion())))
    {
        DestroyRegion(tra);
        DestroyRegion(trb);
		return 0;
    }
    (void) XSubtractRegion(sra,srb,tra);
    (void) XSubtractRegion(srb,sra,trb);
    (void) XUnionRegion(tra,trb,dr);
    DestroyRegion(tra);
    DestroyRegion(trb);
    return 0;
}


/**
 * @brief Test whether a point lies inside a region.
 *
 * Performs an INBOX check against the bounding box first, then linearly
 * scans the rectangle array.  A binary search by y is noted as a
 * future optimisation.
 *
 * @param pRegion Region to test.
 * @param x       X coordinate of the point to test.
 * @param y       Y coordinate of the point to test.
 * @return true if (x, y) is inside any rectangle of \a pRegion, false otherwise.
 */
bool
BRegion::Support::XPointInRegion(
    const BRegion* pRegion,
    int x, int y)
{
	// TODO: binary search by "y"!
    int i;

    if (pRegion->fCount == 0)
        return false;
    if (!INBOX(pRegion->fBounds, x, y))
        return false;
    for (i=0; i<pRegion->fCount; i++)
    {
        if (INBOX (pRegion->fData[i], x, y))
	    return true;
    }
    return false;
}

/**
 * @brief Determine the containment relationship between a rectangle and a region.
 *
 * Scans the y-x-banded rectangle array to classify \a rect as:
 *  - RectangleOut  — no part of rect is inside the region.
 *  - RectanglePart — rect partially overlaps the region.
 *  - RectangleIn   — rect is completely contained within the region.
 *
 * The algorithm uses a two-flag approach (partIn / partOut) and bails out
 * early as soon as both flags are set.
 *
 * @param region Region to test against.
 * @param rect   Rectangle to classify.
 * @return RectangleOut, RectanglePart, or RectangleIn.
 */
int
BRegion::Support::XRectInRegion(
    const BRegion* region,
    const clipping_rect& rect)
{
    clipping_rect* pbox;
    clipping_rect* pboxEnd;
    const clipping_rect* prect = &rect;
    bool      partIn, partOut;

    int rx = prect->left;
    int ry = prect->top;

    /* this is (just) a useful optimization */
    if ((region->fCount == 0) || !EXTENTCHECK(&region->fBounds, prect))
        return(RectangleOut);

    partOut = false;
    partIn = false;

    /* can stop when both partOut and partIn are true, or we reach prect->bottom */
    for (pbox = region->fData, pboxEnd = pbox + region->fCount;
	 pbox < pboxEnd;
	 pbox++)
    {

	if (pbox->bottom <= ry)
	   continue;	/* getting up to speed or skipping remainder of band */

	if (pbox->top > ry)
	{
	   partOut = true;	/* missed part of rectangle above */
	   if (partIn || (pbox->top >= prect->bottom))
	      break;
	   ry = pbox->top;	/* x guaranteed to be == prect->left */
	}

	if (pbox->right <= rx)
	   continue;		/* not far enough over yet */

	if (pbox->left > rx)
	{
	   partOut = true;	/* missed part of rectangle to left */
	   if (partIn)
	      break;
	}

	if (pbox->left < prect->right)
	{
	    partIn = true;	/* definitely overlap */
	    if (partOut)
	       break;
	}

	if (pbox->right >= prect->right)
	{
	   ry = pbox->bottom;	/* finished with this band */
	   if (ry >= prect->bottom)
	      break;
	   rx = prect->left;	/* reset x out to left again */
	} else
	{
	    /*
	     * Because boxes in a band are maximal width, if the first box
	     * to overlap the rectangle doesn't completely cover it in that
	     * band, the rectangle must be partially out, since some of it
	     * will be uncovered in that band. partIn will have been set true
	     * by now...
	     */
	    break;
	}

    }

    return(partIn ? ((ry < prect->bottom) ? RectanglePart : RectangleIn) :
		RectangleOut);
}
