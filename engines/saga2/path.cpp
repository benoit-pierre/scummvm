/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 * Based on the original sources
 *   Faery Tale II -- The Halls of the Dead
 *   (c) 1993-1996 The Wyrmkeep Entertainment Co.
 */

#define FORBIDDEN_SYMBOL_ALLOW_ALL // FIXME: Remove

#include "saga2/std.h"
#include "saga2/dispnode.h"
#include "saga2/tcoords.h"
#include "saga2/tile.h"
#include "saga2/motion.h"
#include "saga2/actor.h"
#include "saga2/player.h"
#include "saga2/pool.h"
#include "saga2/cmisc.h"
#include "saga2/priqueue.h"

namespace Saga2 {

//  Defines to turn on visual debugging aids for path finder.
#define VISUAL1     0
//1 for paths considered
#define VISUAL2     0
//2 unused
#define VISUAL3     0
//3 unused
#define VISUAL4     0
//4 for fetchTileSection
#define VISUAL5     0
//5 for fetchSubMeta
#define VISUAL6     0
//6 for selectNearbySite
#define VISUAL7     0
//7 for checkPath

#if VISUAL1 || VISUAL2 || VISUAL4 || VISUAL5 || VISUAL6 || VISUAL7
void TPLine(const TilePoint &start, const TilePoint &stop);
#endif

/* ===================================================================== *
   Imports
 * ===================================================================== */

extern WorldMapData         *mapList;

/* ===================================================================== *
   Types
 * ===================================================================== */

const int           searchCenter = 13,      // origin point of array
                    searchDiameter = searchCenter * 2;

const int           straightBaseCost    = 4,
                    diagBaseCost        = 6,
                    straightEasyCost    = straightBaseCost + 2,
                    straightNormalCost  = straightBaseCost + 4,
                    straightHardCost    = straightBaseCost + 8,
                    diagEasyCost        = diagBaseCost + 3,
                    diagNormalCost      = diagBaseCost + 6,
                    diagHardCost        = diagBaseCost + 12;

const int           maskSize            = 4;

const int           subMetaSize         = 4,
                    subMetaShift        = 2,
                    subMetaMask         = subMetaSize - 1;



static TilePoint tDirTable[ 8 ] = {
	TilePoint(4, 4, 0),
	TilePoint(0, 4, 0),
	TilePoint(-4, 4, 0),
	TilePoint(-4, 0, 0),
	TilePoint(-4, -4, 0),
	TilePoint(0, -4, 0),
	TilePoint(4, -4, 0),
	TilePoint(4, 0, 0)
};

static TilePoint tDirTable2[ 8 ] = {
	TilePoint(1, 1, 0),
	TilePoint(0, 1, 0),
	TilePoint(-1, 1, 0),
	TilePoint(-1, 0, 0),
	TilePoint(-1, -1, 0),
	TilePoint(0, -1, 0),
	TilePoint(1, -1, 0),
	TilePoint(1, 0, 0)
};

static TilePoint tDirTable3[ 8 ] = {
	TilePoint(16, 16, 0),
	TilePoint(0, 16, 0),
	TilePoint(-16, 16, 0),
	TilePoint(-16,  0, 0),
	TilePoint(-16, -16, 0),
	TilePoint(0, -16, 0),
	TilePoint(16, -16, 0),
	TilePoint(16,  0, 0)
};



/* ===================================================================== *
   The PathTileRegion class
 * ===================================================================== */

struct PathTileInfo {
	TileInfo    *surfaceTile;
	int16       surfaceHeight;
};

typedef PathTileInfo        PathTilePosInfo[ maxPlatforms ];

typedef PathTilePosInfo     PathTilePosArray
[ searchDiameter + 4 ]
[ searchDiameter + 4 ];

typedef uint8               PathSubMetaFlags
[((((searchDiameter
     +   subMetaMask + 4)
    * (searchDiameter
    +   subMetaMask + 4))
   >>  subMetaShift)
  +   7)
 >>  3 ];


//  This class manages an array containing terrain information, which
//  will be loaded on the fly as the path finder needs it.
class PathTileRegion {

	friend class PathRequest;

	int16           mapNum;         //  The map number of the terrain data

	TilePoint       origin,         //  The base tile coords of the array
	                area,           //  The size of the array in tiles
	                subMetaArea;    //  The number of submetatiles overlaying
	//  the array area
	PathTilePosInfo *array;         //  The pointer to the array
	uint8           *subMetaFlags;  //  A bit array indicating which
	//  submetatiles have been loaded

	void fetchSubMeta(const TilePoint &subMeta);

public:

	void init(
	    int16           map,
	    const TilePoint &org,
	    const TilePoint &a,
	    PathTilePosInfo *arr,
	    uint8           *flagArray);

	void fetchTileSection(const TilePoint &org, const TilePoint &a);
	PathTilePosInfo *tilePos(const TilePoint &pos) {
		assert(pos.u >= origin.u && (pos.u - origin.u) < area.u);
		assert(pos.v >= origin.v && (pos.v - origin.v) < area.v);
		return &array[(pos.u - origin.u) * area.v + pos.v - origin.v ];
	}
};


//  Initialize the path tile region
void PathTileRegion::init(
    int16           map,
    const TilePoint &org,
    const TilePoint &a,
    PathTilePosInfo *arr,
    uint8           *flagArray) {
	mapNum = map;
	origin = org;
	area = a;
	array = arr;
	subMetaFlags = flagArray;

	origin.z = area.z = subMetaArea.z = 0;
	subMetaArea.u = (area.u + (origin.u & subMetaMask) + subMetaMask)
	                >> subMetaShift;
	subMetaArea.v = (area.v + (origin.v & subMetaMask) + subMetaMask)
	                >> subMetaShift;

	//  clear all of the submetatile flags
	memset(subMetaFlags, 0, (subMetaArea.u * subMetaArea.v + 7) >> 3);

	//  NULL the tile pointers in the array
	int16           arraySize = area.u * area.v;
	PathTilePosInfo *tiPtr = array;
	for (; arraySize > 0; arraySize--, tiPtr++) {
		PathTilePosInfo &ptpi = *tiPtr;
		for (int i = 0; i < maxPlatforms; i++)
			ptpi[ i ].surfaceTile = NULL;
	}
}


//  This function will load the submetatiles which overlay a specified
//  region into the array, if those submetatiles have not already been
//  loaded.
void PathTileRegion::fetchTileSection(const TilePoint &org,
                                      const TilePoint &a) {
	int16       flagIndex;
	int       u, v;
	TilePoint   secSubMetaOrigin,
	            secSubMetaArea,
	            relSubMeta;

#if VISUAL4
	TilePoint   pt1, pt2;
	pt1 = pt2 = org << tileUVShift;
	pt1.z = pt2.z = 0;
	pt2.u += (a.u << tileUVShift);
	TPLine(pt1, pt2);
	pt1.u = pt2.u;
	pt1.v += (a.v << tileUVShift);
	TPLine(pt1, pt2);
	pt2.v = pt1.v;
	pt2.u -= (a.u << tileUVShift);
	TPLine(pt1, pt2);
	pt1.u = pt2.u;
	pt1.v -= (a.v << tileUVShift);
	TPLine(pt1, pt2);
#endif

	//  Determine the origin and area of the specified tile section
	//  in submetatile coordinates
	secSubMetaOrigin.u = (org.u >> subMetaShift);
	secSubMetaOrigin.v = (org.v >> subMetaShift);
	secSubMetaArea.u = (a.u + (org.u & subMetaMask) + subMetaMask)
	                   >> subMetaShift;
	secSubMetaArea.v = (a.v + (org.v & subMetaMask) + subMetaMask)
	                   >> subMetaShift;

	for (u = 0; u < secSubMetaArea.u; u++) {
		relSubMeta.u = secSubMetaOrigin.u - (origin.u >> subMetaShift) + u;
		relSubMeta.v = secSubMetaOrigin.v - (origin.v >> subMetaShift);
		flagIndex = relSubMeta.u * subMetaArea.v + relSubMeta.v;

		for (v = 0; v < secSubMetaArea.v; v++, flagIndex++) {
			//  Check the submetatile flag in the bit array
			if (!(subMetaFlags[ flagIndex >> 3 ] & (1 << (flagIndex & 7)))) {
				//  Load the submetatile and set its flag
				fetchSubMeta(TilePoint(secSubMetaOrigin.u + u,
				                       secSubMetaOrigin.v + v,
				                       0));
				subMetaFlags[ flagIndex >> 3 ] |= 1 << (flagIndex & 7);
			}
		}
	}
}


//  This function will load a submeta tile in the array
void PathTileRegion::fetchSubMeta(const TilePoint &subMeta) {
	WorldMapData    *map = &mapList[ mapNum ];

	TilePoint       mCoords;
	MetaTile        *mt;

	mCoords.u = subMeta.u >> 1;
	mCoords.v = subMeta.v >> 1;
	mCoords.z = 0;
	mt = map->lookupMeta(mCoords);

#if VISUAL5
	TilePoint   pt1, pt2;
	pt1 = pt2 = subMeta << (subMetaShift + tileUVShift);
	pt1.z = pt2.z = 0;
	pt2.u += (subMetaSize << tileUVShift);
	TPLine(pt1, pt2);
	pt1.u = pt2.u;
	pt1.v += (subMetaSize << tileUVShift);
	TPLine(pt1, pt2);
	pt2.v = pt1.v;
	pt2.u -= (subMetaSize << tileUVShift);
	TPLine(pt1, pt2);
	pt1.u = pt2.u;
	pt1.v -= (subMetaSize << tileUVShift);
	TPLine(pt1, pt2);
#endif

	if (mt) {
		TileRegion  tileReg;
		TilePoint   offset;

		tileReg.min.u = (subMeta.u << subMetaShift) - origin.u;
		offset.u =  tileReg.min.u + subMetaSize < area.u
		            ?   subMetaSize
		            :   area.u - tileReg.min.u;
		tileReg.min.v = (subMeta.v << subMetaShift) - origin.v;
		offset.v =  tileReg.min.v + subMetaSize < area.v
		            ?   subMetaSize
		            :   area.v - tileReg.min.v;

		if (tileReg.min.u < 0) {
			offset.u += tileReg.min.u;
			tileReg.min.u = 0;
		}
		if (tileReg.min.v < 0) {
			offset.v += tileReg.min.v;
			tileReg.min.v = 0;
		}

		//  Compute tile region relative to metatile
		tileReg.min.u = (tileReg.min.u + origin.u) & platMask;
		tileReg.max.u = tileReg.min.u + offset.u;
		tileReg.min.v = (tileReg.min.v + origin.v) & platMask;
		tileReg.max.v = tileReg.min.v + offset.v;

		assert(tileReg.max.u <= platformWidth);
		assert(tileReg.max.v <= platformWidth);

		//  Compute the offset of base tile in metatile to origin
		offset.u = ((subMeta.u >> 1) << platShift) - origin.u;
		offset.v = ((subMeta.v >> 1) << platShift) - origin.v;

		for (int i = 0; i < maxPlatforms; i++) {
			uint16      tpFlags = 0;
			Platform    *p;
			int       u, v;
			TileRef     *tr;
			int16       height;

			if ((p = mt->fetchPlatform(mapNum, i)) == NULL)
				continue;

			if (!(p->flags & plVisible)) continue;

			for (u = tileReg.min.u; u < tileReg.max.u; u++) {
				PathTilePosInfo *arrRow = &array[(u + offset.u) * area.v ];

				assert(u >= 0);
				assert(u < platformWidth);

				for (v = tileReg.min.v; v < tileReg.max.v; v++) {
					int16   flagIndex = ((u & subMetaMask) << subMetaShift) | (v & subMetaMask);

					assert(v >= 0);
					assert(v < platformWidth);

					if (!(tpFlags & (1 << flagIndex))) {
						tpFlags |= (1 << flagIndex);

						tr = &p->tiles[ u ][ v ];
						height = tr->tileHeight << 3;

						if (tr->flags & trTileTAG) {
							ActiveItem  *groupItem,
							            *instanceItem;
							int16       state = 0;
							int16       tagU, tagV;
							int            tempU, tempV;
							TilePoint   absPos;
							TileRegion  subMetaTag;
							TileRef     *stateData;

							assert((uint16)tr->tile <= activeItemIndexNullID);
							groupItem = ActiveItem::activeItemAddress(
							                ActiveItemID(mapNum, tr->tile));

							tagU = u - ((tr->flags >> 1) & 0x07);
							tagV = v - ((tr->flags >> 4) & 0x07);

							subMetaTag.min.u = tagU;
							subMetaTag.max.u = tagU + groupItem->group.uSize;
							subMetaTag.min.v = tagV;
							subMetaTag.max.v = tagV + groupItem->group.vSize;

							if (subMetaTag.min.u < tileReg.min.u)
								subMetaTag.min.u = tileReg.min.u;
							if (subMetaTag.max.u > tileReg.max.u)
								subMetaTag.max.u = tileReg.max.u;
							if (subMetaTag.min.v < tileReg.min.v)
								subMetaTag.min.v = tileReg.min.v;
							if (subMetaTag.max.v > tileReg.max.v)
								subMetaTag.max.v = tileReg.max.v;

							//  Abspos is the absolute position of the
							//  group on the tile map.

							absPos.u = (mCoords.u << platShift) | tagU;
							absPos.v = (mCoords.v << platShift) | tagV;
							absPos.z = height;

							//  Look up the group instance in the hash.
							instanceItem = map->findHashedInstance(absPos, tr->tile);
							if (instanceItem) state = instanceItem->getInstanceState(mapNum);

							stateData = &(map->activeItemData)[
							                groupItem->group.grDataOffset
							                +   state * groupItem->group.animArea ];

							for (tempU = subMetaTag.min.u; tempU < subMetaTag.max.u; tempU++) {
								TileRef         *rowData = &stateData[(tempU - tagU) * groupItem->group.vSize ];
								PathTilePosInfo *tempArrRow = &array[(tempU + offset.u) * area.v ];

								for (tempV = subMetaTag.min.v; tempV < subMetaTag.max.v; tempV++) {
									flagIndex = ((tempU & subMetaMask) << subMetaShift) + (tempV & subMetaMask);

									tpFlags |= (1 << flagIndex);

									if (instanceItem) tr = &rowData[ tempV - tagV ];
#if DEBUG
									else {
										static  TileRef dummyRef = { 1, 0, 0 };
										tr = &dummyRef;
									}
#endif
									tempArrRow[ tempV + offset.v ][ i ].surfaceTile =
									    TileInfo::tileAddress(tr->tile);
									tempArrRow[ tempV + offset.v ][ i ].surfaceHeight =
									    height + (tr->tileHeight << 3);
								}
							}
						} else {
							arrRow[ v + offset.v ][ i ].surfaceTile =
							    TileInfo::tileAddress(tr->tile);
							arrRow[ v + offset.v ][ i ].surfaceHeight =
							    height;
						}
					}
				}
			}
		}
	}
}


/* ===================================================================== *
   The PathArray and QueueItem types
 * ===================================================================== */

//  Represents a single cell on the path array

struct PathCell {
	uint8           direction;              // direction into cell
	int8            platformDelta;          // platform difference of cell
	int16           height;                 // height above floor
	int16           cost;                   // cost to get there
};

//  Virtual array of cells representing the path region we will search.
class PathArray {
public:

	enum {
		chunkTileDiameter = 4,
		regionChunkDiameter =
		    (searchDiameter + chunkTileDiameter - 1) / chunkTileDiameter,
	};

private:
	//  Search sub-region class.
	struct PathArrayChunk {
		uint16      mask;       //  Mask indicating which cells in chunk are
		//  allocated
		//  The cell array
		PathCell    array[ chunkTileDiameter ][ chunkTileDiameter ];

		PathArrayChunk(void) : mask(0) {}
	};

	//  Master array of chunk pointers
	PathArrayChunk  *array[ maxPlatforms ][ regionChunkDiameter ][ regionChunkDiameter ];
public:

	//  Exception class
	class CellAllocationFailure {};

	//  Constructor
	PathArray(void);

	//  Destructor
	~PathArray(void);

	//  Make a new cell or access an existing cell.  If the specified
	//  cell already exists *newCell will be set to FALSE, else it will
	//  be TRUE.  If it fails to allocate a new cell it will throw
	//  a CellAllocationFailure.
	PathCell *makeCell(int plat, int uCoord, int vCoord, bool *newCell);

	//  Get a pointer to an existing cell.  If the specified cell has
	//  not been created, it will return NULL.
	PathCell *getCell(int plat, int uCoord, int vCoord);

	//  Delete an existing cell
	void deleteCell(int plat, int uCoord, int vCoord);

	//  Delete all existing cells
	void reset(void);
};

//  Constructor
PathArray::PathArray(void) {
	int     plat, chunkU, chunkV;

	for (plat = 0; plat < maxPlatforms; plat++) {
		for (chunkU = 0; chunkU < regionChunkDiameter; chunkU++) {
			for (chunkV = 0; chunkV < regionChunkDiameter; chunkV++)
				array[ plat ][ chunkU ][ chunkV ] = NULL;
		}
	}
}

//  Destructor
PathArray::~PathArray(void) {
	reset();
}


//  Make a new cell or access an existing cell.  If the specified
//  cell already exists *newCell will be set to FALSE, else it will
//  be TRUE.  If it fails to allocate a new cell it will throw
//  a CellAllocationFailure.
PathCell *PathArray::makeCell(int plat, int uCoord, int vCoord, bool *newCell) {
	assert(plat >= 0 && plat < maxPlatforms);
	assert(uCoord >= 0 && uCoord < searchDiameter);
	assert(vCoord >= 0 && vCoord < searchDiameter);
	assert(newCell != NULL);

	//  Compute the chunk coords
	int             chunkUCoord = uCoord >> 2,
	                chunkVCoord = vCoord >> 2;

	//  Get a pointer to the chunk pointer in the array
	PathArrayChunk  **chunkPtrPtr = &array[ plat ][ chunkUCoord ][ chunkVCoord ];

	//  Get existing chunk or allocate a new one
	if (*chunkPtrPtr != NULL || (*chunkPtrPtr = new (RNewPtr(sizeof(PathArrayChunk),
	        NULL, "Path array chunk")) PathArrayChunk) !=  NULL) {
		PathArrayChunk  *chunkPtr = *chunkPtrPtr;
		uint16          chunkCellMask;

		//  Compute the coordinates of the cell relative to the chunk
		uCoord &= chunkTileDiameter - 1;
		vCoord &= chunkTileDiameter - 1;

		//  Compute the bit mask of the cell within the chunk
		chunkCellMask = 1 << ((uCoord << 2) | vCoord);

		//  Determine if this is a new cell
		*newCell = (chunkPtr->mask & chunkCellMask) == 0;

		//  Mark the cell as allocated
		chunkPtr->mask |= chunkCellMask;

		return &chunkPtr->array[ uCoord ][ vCoord ];
	} else {
		//  Failed to allocate cell so throw exception
		error("Cell Allocation failure");

#ifdef _WIN32
		//  Visual C++ 4.0 requires this return because it appearently
		//  does not recognize the 'throw' statement as a return
		//  structure
		return NULL;
#endif
	}
}

//  Get a pointer to an existing cell.  If the specified cell has
//  not been created, it will return NULL.
PathCell *PathArray::getCell(int plat, int uCoord, int vCoord) {
	assert(plat >= 0 && plat < maxPlatforms);
	assert(uCoord >= 0 && uCoord < searchDiameter);
	assert(vCoord >= 0 && vCoord < searchDiameter);

	//  Compute the chunk coords
	int             chunkUCoord = uCoord >> 2,
	                chunkVCoord = vCoord >> 2;
	uint16          chunkCellMask;

	PathArrayChunk  *chunkPtr = array[ plat ][ chunkUCoord ][ chunkVCoord ];

	if (chunkPtr == NULL) return NULL;

	//  Compute the coordinates of the cell relative to the chunk
	uCoord &= chunkTileDiameter - 1;
	vCoord &= chunkTileDiameter - 1;

	//  Compute the bit mask of the cell within the chunk
	chunkCellMask = 1 << ((uCoord << 2) | vCoord);

	//  Determine if cell has been allocated
	if ((chunkPtr->mask & chunkCellMask) == 0) return NULL;

	return &chunkPtr->array[ uCoord ][ vCoord ];
}

void PathArray::deleteCell(int plat, int uCoord, int vCoord) {
	assert(plat >= 0 && plat < maxPlatforms);
	assert(uCoord >= 0 && uCoord < searchDiameter);
	assert(vCoord >= 0 && vCoord < searchDiameter);

	//  Compute the chunk coords
	int             chunkUCoord = uCoord >> 2,
	                chunkVCoord = vCoord >> 2;
	uint16          chunkCellMask;

	PathArrayChunk  *chunkPtr = array[ plat ][ chunkUCoord ][ chunkVCoord ];

	if (chunkPtr == NULL) return;

	//  Compute the coordinates of the cell relative to the chunk
	uCoord &= chunkTileDiameter - 1;
	vCoord &= chunkTileDiameter - 1;

	//  Compute the bit mask of the cell within the chunk
	chunkCellMask = 1 << ((uCoord << 2) | vCoord);

	//  Clear the bit
	chunkPtr->mask &= ~chunkCellMask;
}

//  Delete all existing cells
void PathArray::reset(void) {
	int     plat, chunkU, chunkV;

	for (plat = 0; plat < maxPlatforms; plat++) {
		for (chunkU = 0; chunkU < regionChunkDiameter; chunkU++) {
			for (chunkV = 0; chunkV < regionChunkDiameter; chunkV++) {
				PathArrayChunk      **chunkPtrPtr;

				chunkPtrPtr = &array[ plat ][ chunkU ][ chunkV ];

				if (*chunkPtrPtr != NULL) {
					//  Call destructor
					(*chunkPtrPtr)->~PathArrayChunk();
					RDisposePtr(*chunkPtrPtr);
					*chunkPtrPtr = NULL;
				}
			}
		}
	}
}

//  Represents an entry in the queue

struct QueueItem {
	int16           z;                      // height over terrain
	uint8           u, v;                   // relative coords of cell
	uint8           platform;               // platform number of cell
	Direction       direction;              // direction out of cell
	uint8           pad;
	int16           cost;                   // Cost to get to this cell

	operator int() {
		return cost;
	}
};


/* ===================================================================== *
   The MaskComputer class
 * ===================================================================== */

//  Represents the subtile mask of an object at a single point
struct PointMask {
	TilePoint   size;
	TilePoint   offset;
	uint16      mask[ 16 ];
};


//  Represents the subtile masks of each point on a path in a single
//  direction
class DirMask {
	friend class DirMaskGroup;

	PointMask pathPt[ 4 ];

public:
	PointMask &operator[](int16 index) {
		return pathPt[ index ];
	}
};


//  Represents the subtile masks of each path in all eight direction
class DirMaskGroup {
	friend class MaskComputer;

	uint8   crossSection;
	DirMask dMask[ 8 ];

	void computeMask(uint8 objSection);

public:
	DirMask &operator[](int16 index) {
		return dMask[ index ];
	}
};


//  The class which will compute all of the subtile masks for an object
class MaskComputer {

private:
	DirMaskGroup    array[ 8 ],
	                *ptrArray[ 8 ];
	int16           arraySize;

public:
	MaskComputer(void) : arraySize(0) {}
	void *operator new (size_t, void *p) {
		return p;
	}

	DirMaskGroup *computeMask(uint8 objSection);
};


//  return a word with the bit (minBit) to bit (maxBit-1) all set,
//  and all other bits reset.

inline uint32 makeMask16(int minBit, int maxBit) {
	return (1 << maxBit) - (1 << minBit);
}

void DirMaskGroup::computeMask(uint8 objSection) {
	bool        diagExpand;
	int       dir;
	struct {
		int16   min;
		int16   max;
	}           area;

	crossSection = objSection;

	//  Calculate the area in subtiles the object occupies.  Since U and
	//  V coordinates will alway equal each other, there is no need to
	//  calculate both.
	area.min = ((tileUVSize / 2) - objSection) >> subTileShift;
	area.max = ((tileUVSize / 2) + objSection + subTileMask) >> subTileShift;

	//  Determine if the cross section is wide enough that the diaginal
	//  masks need to be expanded outward one subtile
	diagExpand = (objSection - 1) & 0x2;

	for (dir = 0; dir < 8; dir++) {
		int           ptNum;
		TileRegion  baseMaskArea;

		//  Calculate the base mask from which all of the point masks
		//  will be derived
		baseMaskArea.min.u = baseMaskArea.min.v = area.min;
		baseMaskArea.max.u = baseMaskArea.max.v = area.max;

		if (!(dir & 0x1) && diagExpand) {
			switch (dir >> 1) {
			case 0:
				baseMaskArea.min.u--;
				baseMaskArea.max.v--;
				break;
			case 1:
				baseMaskArea.max.u++;
				baseMaskArea.min.v--;
				break;
			case 2:
				baseMaskArea.max.u++;
				baseMaskArea.max.v++;
				break;
			case 3:
				baseMaskArea.min.u--;
				baseMaskArea.max.v++;
			}
		}

		for (ptNum = 0; ptNum < 4; ptNum++) {
			int       u,
			          v;
			TileRegion  ptMaskArea;
			uint16      tempMask[ 16 ];
			PointMask   *ptMask = &dMask[ dir ].pathPt[ ptNum ];

			//  Compute the point mask area
			ptMaskArea.min = baseMaskArea.min + tDirTable2[ dir ] * (ptNum + 1);
			ptMaskArea.max = baseMaskArea.max + tDirTable2[ dir ] * (ptNum + 1);

			ptMask->offset.u = ptMaskArea.min.u >> tileSubShift;
			ptMask->offset.v = ptMaskArea.min.v >> tileSubShift;

			ptMaskArea.max.u -= ptMaskArea.min.u & ~subTileMask;
			ptMaskArea.min.u &= subTileMask;
			ptMaskArea.max.v -= ptMaskArea.min.v & ~subTileMask;
			ptMaskArea.min.v &= subTileMask;

			ptMask->size.u = (ptMaskArea.max.u + tileSubMask) >> tileSubShift;
			ptMask->size.v = (ptMaskArea.max.v + tileSubMask) >> tileSubShift;

			memset(tempMask, 0, sizeof(tempMask));

			uint16  vMask = makeMask16(ptMaskArea.min.v, ptMaskArea.max.v);
			for (u = ptMaskArea.min.u; u < ptMaskArea.max.u; u++)
				tempMask[ u ] = vMask;

			for (u = 0; u < ptMask->size.u; u++) {
				uint16  *srcMask = &tempMask[ u << 2 ];
				uint16  *destMask = &ptMask->mask[ u << 2 ];

				for (v = 0; v < ptMask->size.v; v++) {
					switch (v) {
					case 0:
						destMask[ 0 ] = (srcMask[ 0 ] & 0x000f)       |
						                (srcMask[ 1 ] & 0x000f) <<  4 |
						                (srcMask[ 2 ] & 0x000f) <<  8 |
						                (srcMask[ 3 ] & 0x000f) << 12;
						break;

					case 1:
						destMask[ 1 ] = (srcMask[ 0 ] & 0x00f0) >>  4 |
						                (srcMask[ 1 ] & 0x00f0)       |
						                (srcMask[ 2 ] & 0x00f0) <<  4 |
						                (srcMask[ 3 ] & 0x00f0) <<  8;
						break;

					case 2:

						destMask[ 2 ] = (srcMask[ 0 ] & 0x0f00) >>  8 |
						                (srcMask[ 1 ] & 0x0f00) >>  4 |
						                (srcMask[ 2 ] & 0x0f00)       |
						                (srcMask[ 3 ] & 0x0f00) <<  4;
						break;

					case 3:
						destMask[ 3 ] = (srcMask[ 0 ] & 0xf000) >> 12 |
						                (srcMask[ 1 ] & 0xf000) >>  8 |
						                (srcMask[ 2 ] & 0xf000) >>  4 |
						                (srcMask[ 3 ] & 0xf000);
					}
				}
			}
		}
	}
}

DirMaskGroup *MaskComputer::computeMask(uint8 crossSection) {
	DirMaskGroup    *maskGroup;
	int             i;

	//  Check if this mask group has already been computed
	for (i = 0; i < arraySize; i++) {
		maskGroup = ptrArray[ i ];

		if (maskGroup->crossSection == crossSection) {
			//  This mask group has already been computed
			if (i > 0) {
				//  Move the reference to this mask group up one position
				ptrArray[ i ] = ptrArray[ i - 1 ];
				ptrArray[ i - 1 ] = maskGroup;
			}

			return maskGroup;
		}
	}

	if (arraySize < elementsof(array)) {
		//  Allocate a new place for this mask group
		maskGroup = ptrArray[ arraySize ] = &array[ arraySize ];
		arraySize++;
	} else
		//  Discard last referenced mask group in array
		maskGroup = ptrArray[ elementsof(array) - 1 ];

	//  Compute the new group of masks
	maskGroup->computeMask(crossSection);

	return maskGroup;
}


uint32 tileTerrain(
    PathTilePosInfo *tilePos,
    int16           mask,
    int16           minZ,
    int16           maxZ) {
	uint32          terrain = 0;

	for (int i = 0; i < maxPlatforms; i++) {
		int32           height, tileMinZ, tileMaxZ;
		TileInfo        *ti;

		ti = (*tilePos)[ i ].surfaceTile;

		if (ti) {
			height = (*tilePos)[ i ]. surfaceHeight;
			TileAttrs &attrs = ti->attrs;
			tileMinZ = tileMaxZ = height;
			int32   combinedMask = ti->combinedTerrainMask();

			if (combinedMask & terrainRaised)
				tileMaxZ += attrs.terrainHeight;
			if (combinedMask & terrainWater)
				tileMinZ -= attrs.terrainHeight;

			if (tileMinZ <  maxZ
			        &&  tileMaxZ >= minZ) {
				uint32  terrainResult = 0,
				        tileFgdTerrain = (1 << attrs.fgdTerrain),
				        tileBgdTerrain = (1 << attrs.bgdTerrain);

				//  If only checking the top of raised terrain treat it
				//  as if it were normal terrain.
				if (minZ + maxStepHeight >= tileMaxZ) {
					if (tileFgdTerrain & terrainSupportingRaised)
						tileFgdTerrain = terrainNormal;
					if (tileBgdTerrain & terrainSupportingRaised)
						tileBgdTerrain = terrainNormal;
				}

				if (mask & attrs.terrainMask)
					terrainResult |= tileFgdTerrain;

				if (mask & ~attrs.terrainMask)
					terrainResult |= tileBgdTerrain;

				//  This prevents actors from walking through
				//  catwalks and other surfaces which have no bottom.

				if ((terrainResult & terrainSolidSurface)
				        &&  height > minZ + maxStepHeight) {
					terrainResult |= terrainStone;
				}

				terrain |= terrainResult;
			}
		}
	}
	return terrain;
}


const uint32 terrainSink = terrainInsubstantial | terrainSupportingRaised | terrainWater;

int16 tileSlopeHeight(
    PathTileRegion  &tileArr,
    const TilePoint &pt,
    GameObject      *obj,
    PathTileInfo    *ptiResult,
    uint8           *platformResult) {
	//  Calculate coordinates of tile and subtile
	TilePoint       tileCoords = pt >> tileUVShift,
	                subTile(
	                    (pt.u >> subTileShift) & subTileMask,
	                    (pt.v >> subTileShift) & subTileMask,
	                    0);

	PathTileInfo    highestTile,
	                lowestTile;
	int             supportHeight,
	                highestSupportHeight,
	                lowestSupportHeight;
	uint32
	highestSupportPlatform = 0,
	lowestSupportPlatform = 0;
	bool            highestTileFlag,
	                lowestTileFlag;
	PathTilePosInfo &tilePosInfo = *tileArr.tilePos(tileCoords);

	highestSupportHeight = -100;
	lowestSupportHeight = 0x7FFF;
	highestTileFlag = FALSE;
	lowestTileFlag = FALSE;
	int objProtHt = obj->proto()->height;

	//  Search each platform until we find a tile which is under
	//  the character.

	for (int i = 0; i < maxPlatforms; i++) {
		PathTileInfo    *pti = ((PathTileInfo *)(&tilePosInfo)) + i; // &tilePosInfo[ i ];
		TileInfo        *ti = pti->surfaceTile;

		if (ti) {
			int     height = pti->surfaceHeight;
			TileAttrs &attrs = ti->attrs;
			int16   tileBase = height;
			int32 subTileTerrain =
			    attrs.testTerrain(calcSubTileMask(subTile.u,
			                                      subTile.v));
			if (subTileTerrain & terrainSink) {
				if (subTileTerrain & terrainInsubstantial)
					continue;
				else if (subTileTerrain & terrainSupportingRaised)
					// calculate height of raised surface
					supportHeight = height +
					                attrs.terrainHeight;
				else {
					// calculate depth of water
					supportHeight = height -
					                attrs.terrainHeight;
					tileBase = supportHeight;
				}
			} else
				// calculate height of unraised surface
				supportHeight = height +
				                ptHeight(TilePoint(pt.u & tileUVMask,
				                                   pt.v & tileUVMask,
				                                   0),
				                         attrs.cornerHeight);

			//  See if the tile is a potential supporting surface
			if (tileBase < pt.z + objProtHt
			        &&  supportHeight >= highestSupportHeight
			        && (ti->combinedTerrainMask() &
			            terrainSurface | terrainRaised)) {
				highestTileFlag = TRUE;
				highestTile = *pti;
				highestSupportHeight = supportHeight;
				highestSupportPlatform = i;
			} else if (!highestTileFlag &&
			           supportHeight <= lowestSupportHeight &&
			           (ti->combinedTerrainMask() &
			            terrainSurface | terrainRaised)) {
				lowestTileFlag = TRUE;
				lowestTile = *pti;
				lowestSupportHeight = supportHeight;
				lowestSupportPlatform = i;
			}
		}
	}

	if (highestTileFlag) {
		if (ptiResult) *ptiResult = highestTile;
		if (platformResult) *platformResult = highestSupportPlatform;
		return highestSupportHeight;
	}
	if (lowestTileFlag) {
		if (ptiResult) *ptiResult = lowestTile;
		if (platformResult) *platformResult = lowestSupportPlatform;
		return lowestSupportHeight;
	}

	if (ptiResult) {
		ptiResult->surfaceTile = NULL;
		ptiResult->surfaceHeight = 0;
	}
	if (platformResult) *platformResult = 0;

	return 0;
}

/* ===================================================================== *
   PathRequest management function prototypes
 * ===================================================================== */

//  Allocate a new path request
void *newPathRequest(void);

//  Deallocate a path request
void deletePathRequest(void *p);

/* ===================================================================== *
   PathRequest Class
 * ===================================================================== */

enum PathResult {
	pathNotDone,
	pathDone,
	pathAborted,
};

//  This if the base class for all PathRequests
class PathRequest : public DNode {
	friend void addPathRequestToQueue(PathRequest *pr);

protected:
	Actor           *actor;                 // actor path applies to.
	int16           smartness;              // how intelligent?
	MotionTask      *mTask;                 // which motion task started this
	uint8           flags;

	enum pathFlags {
		aborted     = (1 << 0),             // path request has been aborted
		completed   = (1 << 1),             // pathfinder has found best path
		run         = (1 << 2),
	};

	//  These static members are initialized when the path request
	//  becomes the current active request being serviced.
	static TilePoint        path[ 16 ];
	static int16            pathLength;

	static TilePoint        baseCoords,
	       baseTileCoords,
	       centerPt,       // The current center coordinates
	       bestLoc;        // The best cell coordinates,
	// currently visited

	static uint8            centerPlatform,
	       bestPlatform;

	static int16            fetchRadius;

	static int32            firstTick,
	       lastTick,
	       timeLimit;

	static DirMaskGroup     *dirMasks;

	static PathTileRegion   tileArray;

	//  Calculates the center point given the base coordinate of the
	//  cell array and a queue item which contains cell coordinates.
	static void calcCenterPt(
	    const TilePoint &baseTileCoords,
	    const QueueItem &qi) {
		centerPt.u = ((baseTileCoords.u + qi.u) << tileUVShift)
		             +   tileUVSize / 2;
		centerPt.v = ((baseTileCoords.v + qi.v) << tileUVShift)
		             +   tileUVSize / 2;
		centerPt.z = qi.z;

		centerPlatform = qi.platform;
	}

	//  Constructor
	PathRequest(Actor *a, int16 howSmart);

public:
	void requestAbort(void) {
		flags |= aborted;
	}

	void *operator new (size_t) {
		return newPathRequest();
	}
	void operator delete (void *p) {
		deletePathRequest(p);
	}

	virtual void initialize(void);
	virtual void finish(void);           // completion method
	virtual void abort(void);                // abnormal termination method

	PathResult findPath(void);

	//  Set and evaluate a new center location.
	virtual bool setCenter(
	    const TilePoint &baseTileCoords,
	    const QueueItem &qi) = 0;

	//  Determine if path request will allow a move to the specified
	//  location
	virtual bool validMove(const TilePoint &testPt) = 0;

	//  Virtual function for evaluating the cost of moving on stairs.
	virtual int16 evaluateStairs(
	    const TilePoint &testPt,
	    Direction moveDir,
	    Direction stairDir,
	    int16 baseAltitute,
	    int16 upperAltitude) = 0;

	//  Virtual function for evaluating the cost of moving from the
	//  current center location to the specified coordinates.
	virtual int16 evaluateMove(const TilePoint &testPt, uint8 testPlatform) = 0;

	// NEW added by Evan 12/3
	virtual bool timeLimitExceeded(void);

};

/* ===================================================================== *
   DestinationPathRequest Class
 * ===================================================================== */

//  This class is used to request a path leading to a specified
//  destination.
class DestinationPathRequest : public PathRequest {
protected:
	TilePoint           destination;    // The destination of the path
	uint8               destPlatform;   // The destination platform

	//  These static members are initialized when the path request
	//  becomes the current active request being serviced.
	static TilePoint    targetCoords;   // The current destination coordinates
	// quantized to the nearest tile
	// center.
	static uint8        targetPlatform;
	static int16        bestDist,       // The distance from the target of
	       // the best cell visited so far.
	       centerCost;     // The distance from the target of
	// the current center coordinates.
public:
	DestinationPathRequest(Actor *a, int16 howSmart);

	//  Initialize the static data members for this path request.
	void initialize(void);

	//  Set and evaluate a new center location.
	bool setCenter(
	    const TilePoint &baseTileCoords,
	    const QueueItem &qi);

	bool validMove(const TilePoint &testPt);

	//  Evaluate the cost of travelling on these stairs
	int16 evaluateStairs(
	    const TilePoint &testPt,
	    Direction moveDir,
	    Direction stairDir,
	    int16 baseAltitude,
	    int16 upperAltitude);

	//  Evaluate the cost of the specified move
	int16 evaluateMove(const TilePoint &testPt, uint8 testPlatform);
};

/* ===================================================================== *
   WanderPathRequest Class
 * ===================================================================== */

class WanderPathRequest : public PathRequest {
protected:
	bool                tethered;       //  Flag indicating if there is a
	//  tether on this path

	//  Tether coordinates
	int16               tetherMinU,
	                    tetherMinV,
	                    tetherMaxU,
	                    tetherMaxV;

	//  These static members are initialized when the path request
	//  becomes the current active request being serviced.
	static TilePoint    startingCoords; // The actor's location at the
	// beginning of the service.
	static int16        bestDist,       // The distance from the target of
	       // the best cell visited so far.
	       centerCost;     // The distance from the target of
	// the current center coordinates.

public:
	//  Constructor
	WanderPathRequest(Actor *a, int16 howSmart);

	//  Initialize the static data members
	void initialize(void);

	//  Set and evaluate a new center location.
	bool setCenter(
	    const TilePoint &baseTileCoords,
	    const QueueItem &qi);

	//  Determine if point is within the tether region if there is a
	//  tether
	bool validMove(const TilePoint &testPt);

	//  Evaluate the cost of moving on the specified stairs.
	int16 evaluateStairs(
	    const TilePoint &testPt,
	    Direction moveDir,
	    Direction stairDir,
	    int16 baseAltitude,
	    int16 upperAltitude);

	//  Evaluate the cost of moving from the current center location
	//  to the specified location.
	int16 evaluateMove(const TilePoint &testPt, uint8 testPlatform);
};

/* ===================================================================== *
                            Globals
 * ===================================================================== */

const int                   numPathRequests = 32;   // up to 32 messages allowed

typedef uint8 PathRequestPlaceHolder[ sizeof(WanderPathRequest) ];
typedef RPool< PathRequestPlaceHolder, numPathRequests > PathRequestPool;

PathRequestPool             pathRequestPool;

DList                       pathQueue;
PathRequest                 *currentRequest = NULL;

static PathTilePosArray     *pathTileArray;
static PathSubMetaFlags     subMetaFlags;

static MaskComputer         *maskComp;

static PriorityQueue<QueueItem, 192> queue;
static PathArray            *cellArray;

static TileRegion           objectVolumeArray[ 128 ];

struct VolumeLookupNode {
	VolumeLookupNode        *next;
	TileRegion              *volume;
};

static VolumeLookupNode     volumeLookupNodePool[ 256 ];
static VolumeLookupNode     *volumeLookupTable[ searchDiameter ][ searchDiameter ];

TilePoint       PathRequest::path[ 16 ];
int16           PathRequest::pathLength;

TilePoint       PathRequest::baseCoords,
                PathRequest::baseTileCoords,
                PathRequest::centerPt,      // The current center coordinates
                PathRequest::bestLoc;       // The best cell coordinates,
// currently visited
uint8           PathRequest::centerPlatform,
                PathRequest::bestPlatform;

int16           PathRequest::fetchRadius;

int32           PathRequest::firstTick,
                PathRequest::timeLimit;

DirMaskGroup    *PathRequest::dirMasks;

PathTileRegion  PathRequest::tileArray;

TilePoint       DestinationPathRequest::targetCoords;
uint8           DestinationPathRequest::targetPlatform;
int16           DestinationPathRequest::bestDist,
                DestinationPathRequest::centerCost;

TilePoint       WanderPathRequest::startingCoords;
int16           WanderPathRequest::bestDist,
                WanderPathRequest::centerCost;

/* ===================================================================== *
   PathQueue member functions
 * ===================================================================== */

static void push(
    const   TilePoint &tp,
    uint8   platform,
    int     cost,
    int     direction,
    int8    platformDelta) {
	assert(cellArray != NULL);

	PathCell        *cellPtr;
	bool            newCell;
	QueueItem       newItem;

	//  Don't search beyond map edges
	if (tp.u < 1 || tp.u >= searchDiameter - 1
	        ||  tp.v < 1 || tp.v >= searchDiameter - 1)
		return;

	cellPtr = cellArray->makeCell(platform, tp.u, tp.v, &newCell);

	assert(cellPtr != NULL);

	//  If the cell is already visited, only
	//  update it if it was less cost to get here.
	if (!newCell && cellPtr->cost <= cost) return;

	newItem.u = tp.u;                       // coords of this point
	newItem.v = tp.v;                       //
	newItem.z = tp.z;
	newItem.platform = platform;
	newItem.cost = cost;
	newItem.direction = direction;

	if (queue.insert(newItem)) {
		cellPtr->direction = direction;
		cellPtr->platformDelta = platformDelta;
		cellPtr->cost = cost;
		cellPtr->height = tp.z;
	} else {
		if (newCell)
			cellArray->deleteCell(platform, tp.u, tp.v);
	}
}


/* ===================================================================== *
   Path finder management functions
 * ===================================================================== */

void initPathFinder(void) {
	pathTileArray = (PathTilePosArray *)RNewPtr(sizeof * pathTileArray, NULL, "path tile array");
	maskComp = new (RNewPtr(sizeof * maskComp, NULL, "path mask comp")) MaskComputer;
	cellArray = new (RNewPtr(sizeof(PathArray), NULL, "path cell array")) PathArray;
}

void cleanupPathFinder(void) {
	if (pathTileArray) {
		RDisposePtr(pathTileArray);
		pathTileArray = NULL;
	}
	if (maskComp) {
		RDisposePtr(maskComp);
		maskComp = NULL;
	}
	if (cellArray != NULL) {
		//  Call destructor
		cellArray->~PathArray();
		RDisposePtr(cellArray);
		cellArray = NULL;
	}
}

/* ===================================================================== *
   Member Functions
 * ===================================================================== */

/* ===================================================================== *
   PathRequest Class
 * ===================================================================== */

PathRequest::PathRequest(Actor *a, int16 howSmart) {
	actor       = a;
	smartness   = howSmart;
	mTask       = actor->moveTask;
	flags       = mTask->flags & MotionTask::requestRun ? run : 0;

	mTask->pathFindTask = this;
}

void PathRequest::initialize(void) {
	ProtoObj        *proto = actor->proto();
	TilePoint       startingCoords = actor->getLocation();
	int             uCoord, vCoord;
	int             objectVolumes = 0;
	int             nextAvailableLookupNode = 0;
	int           curTileRegU,
	              curTileRegV,
	              minTileRegU,
	              minTileRegV,
	              maxTileRegU,
	              maxTileRegV;
	uint8 pCross = proto->crossSection;

	firstTick = gameTime,
	timeLimit = /*flags & run ? ticksPerSecond / 4 :*/ ticksPerSecond;

	fetchRadius =
	    ((tileUVSize / 2 + pCross) >> tileUVShift) + 1;

	dirMasks = maskComp->computeMask(pCross);

	//  Set the best location to the starting location
	bestLoc = Nowhere;

	//  Calculate where search cells will be projected onto map
	baseTileCoords.u = (startingCoords.u >> tileUVShift) - searchCenter;
	baseTileCoords.v = (startingCoords.v >> tileUVShift) - searchCenter;
	baseTileCoords.z = 0;

	baseCoords.u = baseTileCoords.u << tileUVShift;
	baseCoords.v = baseTileCoords.v << tileUVShift;
	baseCoords.z = 0;

	//  Clear the priority queue
	queue.clear();

	//  Initialize the tile array
	tileArray.init(
	    actor->getMapNum(),
	    TilePoint(
	        baseTileCoords.u - 2,
	        baseTileCoords.v - 2,
	        0),
	    TilePoint(
	        searchDiameter + 4,
	        searchDiameter + 4,
	        0),
	    (PathTilePosInfo *)pathTileArray,
	    subMetaFlags);

	for (uCoord = 0; uCoord < searchDiameter; uCoord++) {
		for (vCoord = 0; vCoord < searchDiameter; vCoord++)
			volumeLookupTable[ uCoord ][ vCoord ] = NULL;
	}

	RegionalObjectIterator  iter(
	    currentWorld,
	    baseCoords,
	    TilePoint(
	        baseCoords.u
	        + (searchCenter << tileUVShift) * 2,
	        baseCoords.v
	        + (searchCenter << tileUVShift) * 2,
	        0));
	GameObject              *obj;

	for (iter.first(&obj);
	        obj != NULL;
	        iter.next(&obj)) {
		TilePoint       objLoc = obj->getLocation() - baseCoords;
		ProtoObj        *objProto = obj->proto();
		TileRegion      *objRegion = &objectVolumeArray[ objectVolumes ];
		uint8 poCross = objProto->crossSection;

		//  Obviously, we shouldn't block ourselves.
		if (obj == actor || obj->isInvisible()) continue;

		//  Dead corpses and invisible actors are not considered
		//  obstacles.
		if (isActor(obj)
		        && (((Actor *)obj)->isDead()
		            || ((Actor *)obj)->hasEffect(actorInvisible)))
			continue;

		//  Compute the objects volume
		objRegion->min.u = objLoc.u - poCross;
		objRegion->min.v = objLoc.v - poCross;
		objRegion->min.z = objLoc.z;
		objRegion->max.u = objLoc.u + poCross;
		objRegion->max.v = objLoc.v + poCross;
		objRegion->max.z = objLoc.z + objProto->height;

		//  Compute the tile region which this object overlays
		minTileRegU =   MAX(objRegion->min.u >> tileUVShift, 0);
		maxTileRegU =   MIN(
		                    (objRegion->max.u + tileUVMask) >> tileUVShift,
		                    searchDiameter);
		minTileRegV =   MAX(objRegion->min.v >> tileUVShift, 0);
		maxTileRegV =   MIN(
		                    (objRegion->max.v + tileUVMask) >> tileUVShift,
		                    searchDiameter);

		for (curTileRegU = minTileRegU;
		        curTileRegU < maxTileRegU;
		        curTileRegU++) {
			assert(curTileRegU >= 0 && curTileRegU < searchDiameter);

			for (curTileRegV = minTileRegV;
			        curTileRegV < maxTileRegV;
			        curTileRegV++) {
				assert(curTileRegV >= 0 && curTileRegV < searchDiameter);

				VolumeLookupNode    *node;
				VolumeLookupNode    **tablePtrPtr;

				//  Get the next lookup node
				node = &volumeLookupNodePool[ nextAvailableLookupNode++ ];
				tablePtrPtr = &volumeLookupTable[ curTileRegU ][ curTileRegV ];

				//  Link into lookup table
				node->volume = objRegion;
				node->next = *tablePtrPtr;
				*tablePtrPtr = node;

				if (nextAvailableLookupNode
				        >=  elementsof(volumeLookupNodePool))
					goto big_break;
			}
		}

		if (++objectVolumes >= elementsof(objectVolumeArray)) break;
	}
big_break:

	//  Compute the actor's tile region
	minTileRegU = (startingCoords.u - pCross) >> tileUVShift;
	minTileRegV = (startingCoords.v - pCross) >> tileUVShift;
	maxTileRegU = (startingCoords.u + pCross + tileUVMask)
	              >>  tileUVShift;
	maxTileRegV = (startingCoords.v + pCross + tileUVMask)
	              >>  tileUVShift;

	for (curTileRegU = minTileRegU;
	        curTileRegU < maxTileRegU;
	        curTileRegU++) {
		for (curTileRegV = minTileRegV;
		        curTileRegV < maxTileRegV;
		        curTileRegV++) {
			TilePoint       quantizedCoords,
			                offsetVector;
			uint8           platform;
			int16           dist,
			                zDist,
			                cost;

			//  Quantize this tile position to the tile center
			quantizedCoords.u = (curTileRegU << tileUVShift) + tileUVSize / 2;
			quantizedCoords.v = (curTileRegV << tileUVShift) + tileUVSize / 2;
			quantizedCoords.z = startingCoords.z;
			quantizedCoords.z = tileSlopeHeight(
			                        quantizedCoords,
			                        actor,
			                        NULL,
			                        &platform);

			//  If the height difference is too great skip this tile
			//  position
			if (abs(quantizedCoords.z - startingCoords.z) > maxStepHeight)
				continue;

			//  Compute initial cost based upon the distance from the
			//  starting location
			offsetVector = quantizedCoords - startingCoords;
			dist = offsetVector.quickHDistance();
			zDist = abs(offsetVector.z);
			cost = dist + zDist;

			//  Push this point
			push(
			    TilePoint(
			        curTileRegU - baseTileCoords.u,
			        curTileRegV - baseTileCoords.v,
			        quantizedCoords.z),
			    platform,
			    cost,
			    dirInvalid,
			    0);
		}
	}
}

void PathRequest::finish(void) {
	Direction           prevDir;
	int16               prevHeight;
	TilePoint           *resultSteps = path,
	                     coords;
	int16               stepCount = 0;
	TilePoint           *res;
	PathCell            *cell;

	static TilePoint    tempResult[ 32 ];

	if (bestLoc != Nowhere) {
		cell = cellArray->getCell(bestPlatform, bestLoc.u, bestLoc.v);
		assert(cell != NULL);

		if (cell->direction != dirInvalid) {
			res = &tempResult[ elementsof(tempResult) ];

			prevDir = dirInvalid;

			for (;;) {
				int16       reverseDir;

				cell = cellArray->getCell(bestPlatform, bestLoc.u, bestLoc.v);
				assert(cell != NULL);

				if (cell->direction != dirInvalid) {
					if (cell->direction != prevDir
					        ||  abs(cell->height - prevHeight) > maxStepHeight) {
						if (res <= tempResult) break;

						coords.u =
						    (bestLoc.u << tileUVShift)
						    +   baseCoords.u
						    +   tileUVSize / 2;
						coords.v =
						    (bestLoc.v << tileUVShift)
						    +   baseCoords.v
						    +   tileUVSize / 2;
						coords.z = cell->height;
						*--res = coords;

						prevDir = cell->direction;
						prevHeight = cell->height;
					}

					reverseDir = (cell->direction + 4) & 0x07;
					bestLoc += tDirTable2[ reverseDir ];
					assert(bestLoc.u >= 0 && bestLoc.u < searchDiameter);
					assert(bestLoc.v >= 0 && bestLoc.v < searchDiameter);
					bestPlatform -= cell->platformDelta;
					assert(bestPlatform >= 0 && bestPlatform < maxPlatforms);
				} else
					break;
			}

			if (resultSteps) {
				while (stepCount < elementsof(path)
				        &&  res < &tempResult[ elementsof(tempResult) ]) {
					*resultSteps++ = *res++;
					stepCount++;
				}
			}
		} else
			//  if pathfinder didn't get anywhere, we're done
			flags |= completed;
	}

	pathLength = stepCount;

	if (mTask->pathFindTask == this && mTask->isWalk()) {
		memcpy(mTask->pathList, path, pathLength * sizeof path[ 0 ]);
		mTask->pathCount = pathLength;
		mTask->pathIndex = 0;
		mTask->flags |= MotionTask::reset;
		if (flags & completed) mTask->flags |= MotionTask::finalPath;
		mTask->pathFindTask = NULL;
	}
}

void PathRequest::abort(void) {
	if (mTask->pathFindTask == this)
		mTask->pathFindTask = NULL;
}


static uint32 severePathFinderOverruns = 0;

PathResult PathRequest::findPath(void) {
	assert(cellArray != NULL);

	static const uint8 costTable[] =
	{ 4, 10, 12, 16, 12, 10, 4, 0, 4, 10, 12, 16, 12, 10, 4, 0 };

	int32           lastTick;
	ProtoObj        *proto = actor->proto();
	QueueItem       qi;
	uint8 pCross = proto->crossSection;

	if (flags & aborted) return pathAborted;

	lastTick = gameTime;

	while (queue.remove(qi)) {
		assert(cellArray->getCell(qi.platform, qi.u, qi.v) != NULL);
		assert(qi.u >= 1 && qi.u < searchDiameter - 1);
		assert(qi.v >= 1 && qi.v < searchDiameter - 1);

		TilePoint   centerTileCoords;
		TilePoint   *tDir;
		int32       i,
		            dir,
		            endDir;


		//  Limit the total path length to avoid maze-solving
		if (qi.cost > smartness) continue;

		//  Set a new center and determine if we're done
		if (setCenter(baseTileCoords, qi)) return pathDone;

		//  Calculate the coordinates of the center in tiles
		centerTileCoords.u = qi.u + baseTileCoords.u;
		centerTileCoords.v = qi.v + baseTileCoords.v;
		centerTileCoords.z = 0;

		if (qi.direction == dirInvalid) {
			//  If this is the starting position, check all directions
			i = dir = 0;
			tDir = tDirTable2;
			endDir = 8;

			tileArray.fetchTileSection(
			    TilePoint(qi.u - fetchRadius, qi.v - fetchRadius, 0)
			    +   baseTileCoords,
			    TilePoint(
			        (fetchRadius << 1) + 1,
			        (fetchRadius << 1) + 1,
			        0));
		} else {
			//  Check only the forward directions
			i = dir = (qi.direction + 6) & 0x7;
			tDir = &tDirTable2[ dir ];
			endDir = i + 5;

			switch (qi.direction) {
			case 0:
				tileArray.fetchTileSection(
				    TilePoint(
				        qi.u + fetchRadius,
				        qi.v - fetchRadius,
				        0)
				    +   baseTileCoords,
				    TilePoint(1, fetchRadius << 1, 0));
			case 1:     // FALL THROUGH
				tileArray.fetchTileSection(
				    TilePoint(
				        qi.u - fetchRadius,
				        qi.v + fetchRadius,
				        0)
				    +   baseTileCoords,
				    TilePoint((fetchRadius << 1) + 1, 1, 0));
				break;
			case 2:
				tileArray.fetchTileSection(
				    TilePoint(
				        qi.u - fetchRadius + 1,
				        qi.v + fetchRadius,
				        0)
				    +   baseTileCoords,
				    TilePoint(fetchRadius << 1, 1, 0));
			case 3:     // FALL THROUGH
				tileArray.fetchTileSection(
				    TilePoint(
				        qi.u - fetchRadius,
				        qi.v - fetchRadius,
				        0)
				    +   baseTileCoords,
				    TilePoint(1, (fetchRadius << 1) + 1, 0));
				break;
			case 4:
				tileArray.fetchTileSection(
				    TilePoint(
				        qi.u - fetchRadius,
				        qi.v - fetchRadius + 1,
				        0)
				    +   baseTileCoords,
				    TilePoint(1, fetchRadius << 1, 0));
			case 5:     // FALL THROUGH
				tileArray.fetchTileSection(
				    TilePoint(
				        qi.u - fetchRadius,
				        qi.v - fetchRadius,
				        0)
				    +   baseTileCoords,
				    TilePoint((fetchRadius << 1) + 1, 1, 0));
				break;
			case 6:
				tileArray.fetchTileSection(
				    TilePoint(
				        qi.u - fetchRadius,
				        qi.v - fetchRadius,
				        0)
				    +   baseTileCoords,
				    TilePoint(fetchRadius << 1, 1,  0));
			case 7:     // FALL THROUGH
				tileArray.fetchTileSection(
				    TilePoint(
				        qi.u + fetchRadius,
				        qi.v - fetchRadius,
				        0)
				    +   baseTileCoords,
				    TilePoint(1, (fetchRadius << 1) + 1, 0));
			}

		}

		for (;
		        i < endDir;
		        tDir = &tDirTable2[(dir = (++i & 0x7)) ]) {
			if (!validMove(centerPt + tDirTable3[ dir ]))
				continue;

			PathTileInfo    pti;
			TilePoint       testPt = centerPt;
			uint8           testPlatform;
			uint32          terrain = 0;
			int32           cost;
			DirMask         &dMask = (*dirMasks)[ dir ];
			int16           prevZ = centerPt.z;

			for (int d = 0; d < 4; d++) {
				int       u, v;
				uint8       maskU, maskV;
				PointMask   &ptMask = dMask[ d ];
				TileRegion  maskReg,
				            actorVolume;

				maskReg.min.u = centerTileCoords.u + ptMask.offset.u;
				maskReg.max.u = maskReg.min.u + ptMask.size.u;
				maskReg.min.v = centerTileCoords.v + ptMask.offset.v;
				maskReg.max.v = maskReg.min.v + ptMask.size.v;

				testPt.u += tDirTable[ dir ].u;
				testPt.v += tDirTable[ dir ].v;
				testPt.z =  tileSlopeHeight(
				                tileArray,
				                testPt,
				                actor,
				                &pti,
				                &testPlatform);

				//  Determine if elevation change is too great
				if (abs(testPt.z - prevZ) <= maxStepHeight)
					prevZ = testPt.z;
				else
					goto big_continue;

				//  Compute the actor's volume at this point relative to
				//  the base of the search region
				actorVolume.min.u =     testPt.u
				                        -   baseCoords.u
				                        -   pCross;
				actorVolume.max.u = actorVolume.min.u + pCross * 2;
				actorVolume.min.v =     testPt.v
				                        -   baseCoords.v
				                        -   pCross;
				actorVolume.max.v = actorVolume.min.v + pCross * 2;
				actorVolume.min.z = testPt.z;
				actorVolume.max.z = testPt.z + proto->height;
				int16 aph = actor->proto()->height;

				for (u = maskReg.min.u, maskU = 0;
				        u < maskReg.max.u;
				        u++, maskU++) {
					PathTilePosInfo *arrRow =
					    &tileArray.array[
					        (u - tileArray.origin.u)
					        *   tileArray.area.v ];

					for (v = maskReg.min.v, maskV = 0;
					        v < maskReg.max.v;
					        v++, maskV++) {
						VolumeLookupNode    *node;

						//  Lookup any potentially intersecting object
						//  volumes
						for (node = volumeLookupTable
						            [ u - baseTileCoords.u ]
						            [ v - baseTileCoords.v ];
						        node != NULL;
						        node = node->next) {
							TileRegion *trv = node->volume;
							//  Check for volume intersection
							if (trv->min.u < actorVolume.max.u
							        &&  actorVolume.min.u < trv->max.u
							        &&  trv->min.v < actorVolume.max.v
							        &&  actorVolume.min.v < trv->max.v
							        &&  trv->min.z < actorVolume.max.z
							        &&      actorVolume.min.z + maxStepHeight
							        <   trv->max.z)
								goto big_continue;
						}

						terrain |=  tileTerrain(
						                &arrRow[ v - tileArray.origin.v ],
						                ptMask.mask[(maskU << 2) | maskV ],
						                testPt.z,
						                testPt.z + aph);
					}
				}
			}

			if (terrain & (terrainImpassable | terrainRaised)) continue;

			//  determine the height of the center of the tile
			//  that we're testing.

			//  Assign costs based on the direction of travel

			if (terrain & terrainSlow) {
				cost = dir & 1 ? straightHardCost : diagHardCost;
			} else if (terrain & terrainAverage) {
				cost = dir & 1 ? straightNormalCost : diagNormalCost;
			} else {
				cost = dir & 1 ? straightEasyCost : diagEasyCost;
			}

			//  We must treat stairs as a special case

			if (pti.surfaceTile != NULL
			        && (pti.surfaceTile->combinedTerrainMask() & terrainStair)) {
				uint8   *cornerHeight = pti.surfaceTile->attrs.cornerHeight;
				uint8   stairDir;
				int16   stairHeight;

				//  Determine the direction and upper altitude of the
				//  stairs

				if (*((uint16 *)&cornerHeight[ 0 ]) == 0) {
					stairDir = 1;
					stairHeight = pti.surfaceHeight + cornerHeight[ 2 ];
				} else if (*((uint16 *)&cornerHeight[ 1 ]) == 0) {
					stairDir = 3;
					stairHeight = pti.surfaceHeight + cornerHeight[ 3 ];
				} else if (*((uint16 *)&cornerHeight[ 2 ]) == 0) {
					stairDir = 5;
					stairHeight = pti.surfaceHeight + cornerHeight[ 0 ];
				} else if (cornerHeight[ 0 ] == 0 && cornerHeight[ 3 ] == 0) {
					stairDir = 7;
					stairHeight = pti.surfaceHeight + cornerHeight[ 1 ];
				} else continue;

				//  Do not go onto the stair at a right angle

				if (stairDir == 1 || stairDir == 5) {
					if (dir == 3 || dir == 7) continue;
				} else if (stairDir == 3 || stairDir == 7) {
					if (dir == 1 || dir == 5) continue;
				} else continue;

				//  Add any additional costs for travelling on these
				//  stairs.
				cost += evaluateStairs(
				            testPt,
				            dir,
				            stairDir,
				            pti.surfaceHeight,
				            stairHeight);

			}

			//  Assign additional costs based on having to travel
			//  uphill or having to jump down steep slopes
			else if (testPt.z > centerPt.z
			         ||  testPt.z < centerPt.z - 12) {
				cost += ((testPt.z - centerPt.z)
				         * (testPt.z - centerPt.z)) >> 5;
			}

			//  If the drop-off is too much, then don't go there!
			//  (i.e. don't jmup off of cliffs). Also we can
			//  only climb steps below a certain height.

//			if (   testPt.z < centerPt.z - maxJumpStep
//				||    testPt.z > centerPt.z + maxStepHeight)
//					continue;

			//  Turns are expensive, the sharper turns are more so.

			cost += costTable[
			            7
			            +   dir
			            - (qi.direction != dirInvalid
			               ?   qi.direction
			               :   actor->currentFacing) ];

#if VISUAL1
			TPLine(centerPt, testPt);
#endif
			//  Determine the final cost of moving to this cell
			cost = clamp(0L,
			             (int32)qi.cost
			             + (int32)cost
			             + (int32)evaluateMove(testPt, testPlatform),
			             (int32)maxint16);

			//  Cost should never be less than previous cost
			cost = MAX<int16>(cost, qi.cost);

			//  Push the new point onto the queue.

			push(
			    TilePoint(
			        qi.u + tDir->u,
			        qi.v + tDir->v,
			        testPt.z),
			    testPlatform,
			    cost,
			    dir,
			    testPlatform - centerPlatform);
			assert(cellArray->getCell(centerPlatform, qi.u, qi.v) != NULL);

big_continue:
			;
		}

		if ((gameTime - lastTick) >= 4) {         // JEFFKLUDGE
			if (timeLimitExceeded())
				return pathDone;
		}
	}

	return pathDone;
}

//-------------------------------------------------------------------
// Path finder time management:
//   Originally all requests had a time limit of 72 and IQ of 400
//   I have rewired it to use an IQ of 400 for player actors, and
//   100 for NPCs. That IQ also affects the time limit now. Players
//   will generally get (72/2)+(400/10) or 76 ticks. NPCs will
//   usually get (72/2)+(100/10) or 46 ticks.


bool PathRequest::timeLimitExceeded(void) {
#ifdef OLD_PATHFINDER_TIME_MGMT
	return (gameTime - firstTick >= timeLimit);
#else
	uint32 cutoff = smartness / (queue.getCount() ? 5 : 8);
	return (gameTime - firstTick >= cutoff);
#endif
}


/* ===================================================================== *
   DestinationPathRequest Class
 * ===================================================================== */

DestinationPathRequest::DestinationPathRequest(Actor *a, int16 howSmart) :
	PathRequest(a, howSmart) {
	//  Quantize the target destination to the nearest tile center.
	mTask->finalTarget.u = (mTask->finalTarget.u & ~tileUVMask) + tileUVSize / 2;
	mTask->finalTarget.v = (mTask->finalTarget.v & ~tileUVMask) + tileUVSize / 2;
	mTask->finalTarget.z =  tileSlopeHeight(
	                            mTask->finalTarget,
	                            a,
	                            NULL,
	                            &destPlatform);

	destination = mTask->finalTarget;
}

//  Initialize the static data members
void DestinationPathRequest::initialize(void) {
	PathRequest::initialize();

	//  Initialize bestDist to the highest possible value.
	bestDist = maxint16;

	//  Quantize the target coordinates to the nearest tile center.
	targetCoords.u = (destination.u & ~tileUVMask) + tileUVSize / 2;
	targetCoords.v = (destination.v & ~tileUVMask) + tileUVSize / 2;
	targetCoords.z = destination.z;
	targetPlatform = destPlatform;
}

//  Set and evaluate a new center location.
bool DestinationPathRequest::setCenter(
    const TilePoint &baseTileCoords,
    const QueueItem &qi) {
	int16       dist,
	            zDist,
	            platDiff;
	TilePoint   targetDelta;

	//  Calculate the center coordinates.
	calcCenterPt(baseTileCoords, qi);

	//  Determine the target vector in order to calculate distance.
	targetDelta = (targetCoords - centerPt);
	dist = targetDelta.quickHDistance();
	zDist = abs(targetDelta.z);
	platDiff = abs(centerPlatform - targetPlatform);
	centerCost = dist + zDist * (platDiff + 1);

	//  Determine if this location is closer than any location we have
	//  previously visited.
	if (centerCost < bestDist) {
		//  Save closest point encountered.

		bestLoc.u = qi.u;
		bestLoc.v = qi.v;
		bestLoc.z = qi.z;
		bestPlatform = qi.platform;
		bestDist = centerCost;

		//  If we're at target square, then we're done!
		if (dist == 0 && zDist <= maxStepHeight) {
			flags |= PathRequest::completed;

			//  Return TRUE to indicate that the path finding is done.
			return TRUE;
		}
	}

	return FALSE;
}

bool DestinationPathRequest::validMove(const TilePoint &) {
	return TRUE;
}

//  Evaluate the cost of moving on the specified stairs in the specified
//  direction
int16 DestinationPathRequest::evaluateStairs(
    const TilePoint &testPt,    //  Coordinates of location we are testing
    Direction moveDir,          //  Direction of movement
    Direction stairDir,         //  Direction of up stairs
    int16 baseAltitude,         //  Altitude of bottom of the stairs
    int16 upperAltitude) {      //  Altitude of the top of the stairs
	int16   cost = 0;

	//  Determine if the stairs are going towards the
	//  altitude of the target coordinates.  If not, assign
	//  additional costs.

	if (targetCoords.z >= upperAltitude) {
		if (moveDir != stairDir) {
			cost = ((testPt.z - centerPt.z)
			        * (testPt.z - centerPt.z)) >> 4;
		}
	} else if (targetCoords.z <= baseAltitude) {
		if (moveDir == stairDir) {
			cost = ((testPt.z - centerPt.z)
			        * (testPt.z - centerPt.z)) >> 4;
		}
	}

	return cost;
}

//  Evaluate the cost of moving to the specified point from the
//  current center location.
int16 DestinationPathRequest::evaluateMove(
    const TilePoint &testPt,
    uint8           testPlatform) {
	int16       dist,
	            zDist,
	            platDiff;
	TilePoint   targetDelta;

	//  Determine the target vector of the specified coordinates, in
	//  order to calculate the distance.
	targetDelta = targetCoords - testPt;
	dist = targetDelta.quickHDistance();
	zDist = abs(targetDelta.z);
	platDiff = abs(testPlatform - targetPlatform);

	return (dist + zDist * (platDiff + 1) - centerCost) >> 2;
}

/* ===================================================================== *
   WanderPathRequest Class
 * ===================================================================== */

WanderPathRequest::WanderPathRequest(
    Actor *a,
    int16 howSmart) :
	PathRequest(a, howSmart) {
	if (mTask->flags & MotionTask::tethered) {
		tethered = TRUE;
		tetherMinU = mTask->tetherMinU;
		tetherMinV = mTask->tetherMinV;
		tetherMaxU = mTask->tetherMaxU;
		tetherMaxV = mTask->tetherMaxV;
	} else
		tethered = FALSE;
}

//  Initialize the static data members
void WanderPathRequest::initialize(void) {
	PathRequest::initialize();

	//  Initialize bestDist to zero.
	bestDist = 0;
	startingCoords = actor->getLocation();
}


//  Set and evaluate a new center location.
bool WanderPathRequest::setCenter(
    const TilePoint &baseTileCoords,
    const QueueItem &qi) {
	int16       dist,
	            zDist;
	TilePoint   movementDelta;

	//  Calculate the center coordinates.
	calcCenterPt(baseTileCoords, qi);

	//  Determine the movement vector in order to calculate distance.
	movementDelta = (startingCoords - centerPt);
	dist = movementDelta.quickHDistance();
	zDist = abs(movementDelta.z);
	centerCost = dist + zDist;

	//  Determine if this location is farther than any location we have
	//  previously visited.
	if (centerCost > bestDist) {
		//  Save farthest point encountered.

		bestLoc.u = qi.u;
		bestLoc.v = qi.v;
		bestLoc.z = qi.z;
		bestPlatform = qi.platform;
		bestDist = centerCost;
	}

	return FALSE;
}

bool WanderPathRequest::validMove(const TilePoint &testPt) {
	return      !tethered
	            || (testPt.u >= tetherMinU
	                &&  testPt.u < tetherMaxU
	                &&  testPt.v >= tetherMinV
	                &&  testPt.v < tetherMaxV);
}

//  There will be no additional costs for travelling on stairs
int16 WanderPathRequest::evaluateStairs(
    const TilePoint &,
    Direction,
    Direction,
    int16,
    int16) {
	return 0;
}

//  Evaluate the cost of moving to the specified point from the
//  current center location.
int16 WanderPathRequest::evaluateMove(const TilePoint &testPt, uint8) {
	int16       dist,
	            zDist;
	TilePoint   movementDelta;

	//  Determine the movement vector of the specified coordinates, in
	//  order to calculate the distance.
	movementDelta = startingCoords - testPt;
	dist = movementDelta.quickHDistance();
	zDist = abs(movementDelta.z) >> 1;

	return (centerCost - (dist + zDist)) >> 1;
}

/* ===================================================================== *
   Misc. functions
 * ===================================================================== */

void *newPathRequest(void) {
	return pathRequestPool.alloc();
}

void deletePathRequest(void *p) {
	pathRequestPool.free(p);
}

void runPathFinder(void) {
	if (currentRequest == NULL) {
		currentRequest = (PathRequest *)pathQueue.first();
		if (currentRequest != NULL) {
			currentRequest->remove();
			currentRequest->initialize();
		}
	}

	if (currentRequest != NULL) {
		PathResult  result;

		result = currentRequest->findPath();

		if (result != pathNotDone) {
			if (result == pathDone)
				currentRequest->finish();
			else
				currentRequest->abort();

			delete currentRequest;
			currentRequest = NULL;

			cellArray->reset();
		}
	}
}

void addPathRequestToQueue(PathRequest *pr) {
	Actor           *a = pr->actor;
	Actor           *centerActor = getCenterActor();

	if (a == centerActor)
		pathQueue.addHead(*pr);
	else {
		if (isPlayerActor(a)) {
			PathRequest     *prInQueue;

			for (prInQueue = (PathRequest *)pathQueue.first();
			        prInQueue != NULL;
			        prInQueue = (PathRequest *)prInQueue->next()) {
				Actor       *prActor = prInQueue->actor;

				if (prActor != centerActor || !isPlayerActor(prActor))
					break;
			}

			if (prInQueue != NULL)
				pathQueue.insert(*pr, *prInQueue);
			else
				pathQueue.addTail(*pr);
		} else
			pathQueue.addTail(*pr);
	}
}

void RequestPath(MotionTask *mTask, int16 smartness) {
	DestinationPathRequest      *pr;
	Actor                       *a = (Actor *)mTask->object;

	if ((pr = new DestinationPathRequest(a, smartness)) != NULL)
		addPathRequestToQueue(pr);
}

void RequestWanderPath(MotionTask *mTask, int16 smartness) {
	WanderPathRequest           *pr;
	Actor                       *a = (Actor *)mTask->object;

	if ((pr = new WanderPathRequest(a, smartness)) != NULL)
		addPathRequestToQueue(pr);
}

void abortPathFind(MotionTask *mTask) {
	if (mTask->pathFindTask) {
		PathRequest     *pr = mTask->pathFindTask;

		if (pr == currentRequest)
			pr->requestAbort();
		else {
			pr->remove();
			delete pr;
		}

		mTask->pathFindTask = NULL;
	}
}

/* ===================================================================== *
   A special pathfinder to select a random site reachable from another one.
 * ===================================================================== */

/*  Specs:

    1.  Find a locations which is reachable from 'center', and which
        is between minDist and maxDist tiles away (preferably midway
        between them).
    2.  Terrain cost and direction of travel need not be considered.
    3.  Don't put creatures on top of houses, but do put them
        on top of hills.
    4.  To simplify even further, checking in diagonal directions
        is not needed.
    5.  This routine should probably run synchronously, and therefore
        cannot share the data structures of the real pathfinder.
*/

enum cellStates {
	cellUnvisited = 0,
	cellOccupied = (1 << 0),
	cellVisited = (1 << 1),
};

typedef uint8       SimpleCellArray[ searchDiameter ][ searchDiameter ];

static PriorityQueue<QueueItem, 128> squeue;

static void spush(const TilePoint &tp, int cost, int direction) {
	QueueItem       newItem;

	//  Don't search beyond map edges

	if (tp.u < 1 || tp.u >= searchDiameter - 1
	        ||  tp.v < 1 || tp.v >= searchDiameter - 1)
		return;

	newItem.u = tp.u;                       // coords of this point
	newItem.v = tp.v;                       //
	newItem.z = tp.z;
	newItem.cost = cost;
	newItem.direction = direction;

	squeue.insert(newItem);
}

/*  ordering of bits:

    u3v3 u3v2 u3v1 u3v0                 +U
    u2v3 u2v2 u2v1 u2v0    +V <---> -V   |
    u1v3 u1v2 u1v1 u1v0                  |
    u0v3 u0v2 u0v1 u0v0                 -U

     15      14      13      12      11      10      09      08
    ----    ----    ----    ----    ----    ----    ----    ----
    u3v3    u3v2    u3v1    u3v0    u2v3    u2v2    u2v1    u2v0

     07      06      05      04      03      02      01      00
    ----    ----    ----    ----    ----    ----    ----    ----
    u1v3    u1v2    u1v1    u1v0    u0v3    u0v2    u0v1    u0v0
*/

/*  0000 0110 0000 0000 <--transition masks
    0110 0110 0111 1110
    0110 0110 0111 1110
    0110 0000 0000 0000
     -U   +U   -V   +V

    0666 6660 0770 0ee0
*/

static const uint16
posUMask = 0x6660,
negUMask = 0x0666,
posVMask = 0x0770,
negVMask = 0x0ee0;

uint16 sTerrainMasks[ 8 ] = {
	posUMask, negUMask,             //  dirUpLeft (U+)
	negVMask, posVMask,             //  dirDownLeft (V-)
	negUMask, posUMask,             //  dirDownRight (U-)
	posVMask, negVMask,             //  dirUpRight (V+)
};

TilePoint selectNearbySite(
    ObjectID        worldID,
    const TilePoint &startingCoords,
    int32           minDist,
    int32           maxDist,
    bool            offScreenOnly) {        // TRUE if we want it off-screen
	assert(isWorld(worldID));

	TilePoint       baseCoords,
	                baseTileCoords,
	                centerPt,               // The current center coordinates
	                bestLoc;                // The best cell coordinates,
	// currently visited
	int16           mapNum = GameWorld::IDtoMapNum(worldID);

	int32           bestRating = -100,
	                bestPossible = (maxDist - minDist) / 2;

	SimpleCellArray *cellArray;

	QueueItem       qi;

	//  Allocate the array of cells
	cellArray = (SimpleCellArray *)
	            RNewPtr(sizeof * cellArray, NULL, "path cell array");

	//  Nowhere indicates failure of the algorithm.
	bestLoc = Nowhere;

	//  Calculate where search cells will be projected onto map
	baseTileCoords.u = (startingCoords.u >> tileUVShift) - searchCenter;
	baseTileCoords.v = (startingCoords.v >> tileUVShift) - searchCenter;
	baseTileCoords.z = 0;

	baseCoords.u = baseTileCoords.u << tileUVShift;
	baseCoords.v = baseTileCoords.v << tileUVShift;
	baseCoords.z = 0;

	//  Clear the search array and the queue
	memset(cellArray, cellUnvisited, sizeof(*cellArray));
	squeue.clear();

	//  Iterate through all actors in the region and mark areas
	//  as occupied.
	RegionalObjectIterator  iter(
	    (GameWorld *)GameObject::objectAddress(worldID),
	    baseCoords,
	    TilePoint(
	        baseCoords.u
	        + (searchCenter << tileUVShift) * 2,
	        baseCoords.v
	        + (searchCenter << tileUVShift) * 2,
	        0));
	GameObject              *obj;

	for (iter.first(&obj);
	        obj != NULL;
	        iter.next(&obj)) {
		TilePoint       objLoc = obj->getLocation();
		ProtoObj        *objProto = obj->proto();

		//  If the object is higher than the actor's head, or
		//  low enough to step over, then ignore it.
		if (objLoc.z >= startingCoords.z + 80
		        ||  objLoc.z + objProto->height <= startingCoords.z + maxStepHeight / 2) {
			continue;
		}

		//  Calculate which tile actor is standing on.
		objLoc = (objLoc - baseCoords) >> tileUVShift;

		//  If that tile is in the search area, then mark it.
		if (objLoc.u >= 0 && objLoc.u < searchDiameter
		        &&  objLoc.v >= 0 && objLoc.v < searchDiameter) {
			(*cellArray)[ objLoc.u ][ objLoc.v ] = cellOccupied;
		}
	}

	//  Push the starting location in the center of the array.

	spush(TilePoint(searchCenter, searchCenter, startingCoords.z),
	      1,                              // initial cost is 1
	      0);                             // facing irrelevant

	while (squeue.remove(qi)) {
		TilePoint   centerTileCoords,
		            distVector;
		TilePoint   *tDir;
		int16       dir;
		int32       distFromCenter,
		            rating;

		//  Calculate the distance between the current point and
		//  the center of the search
		distFromCenter =
		    quickDistance(
		        Point32(qi.u - searchCenter, qi.v - searchCenter));
//				max( abs( qi.u - searchCenter ), abs( qi.v - searchCenter ) );

		//  Calculate the "goodness" of this cell -- is it in the
		//  middle of the band between min and max?
		rating = MIN(distFromCenter - minDist, maxDist - distFromCenter);

		//  Calculate the coordinates of the center in tiles
		centerTileCoords.u = qi.u + baseTileCoords.u;
		centerTileCoords.v = qi.v + baseTileCoords.v;
		centerTileCoords.z = 0;

		centerPt.u = (centerTileCoords.u << tileUVShift) + tileUVSize / 2;
		centerPt.v = (centerTileCoords.v << tileUVShift) + tileUVSize / 2;
		centerPt.z = qi.z;

		//  If this is the best cell found so far, and it is not
		//  occupied, then mark it as the best cell.
		if (rating > bestRating
		        &&  !((*cellArray)[ qi.u ][ qi.v ] & cellOccupied)) {
			bool    cellOK = TRUE;

			//  if this point is on-screen, we might want to reject it...
			if (offScreenOnly) {
				Point16 screenCoords;           // screen coordinates
				extern Rect16 tileRect;

				//  Convert to XY coords.
				TileToScreenCoords(centerPt, screenCoords);

				//  If the point is on-screen, then reject it.
				//  (We want the monsters to walk in from off-screen,
				//  not 'pop in').
				if (screenCoords.x >= -16 && screenCoords.x <= tileRect.width + 16
				        &&  screenCoords.y >= -16 && screenCoords.y <= tileRect.height + 80) {
					cellOK = FALSE;
				}
			}

			//  Save closest point encountered.
			if (cellOK) {
				bestLoc.u = qi.u;
				bestLoc.v = qi.v;
				bestLoc.z = qi.z;
				bestRating = rating;

				if (rating >= bestPossible) break;
			}
		}

		if (distFromCenter >= maxDist) continue;

		for (dir = dirUpLeft;
		        dir <= dirUpRight;
		        dir += 2) {
			uint32          terrain;
			uint8           *cell;
			TilePoint       testPt;
			StandingTileInfo sti;
			TilePoint       fromSubPt,
			                toSubPt;
			bool            traversable = TRUE;
			int16           i;

			uint16          *moveMask = &sTerrainMasks[ dir - 1 ];

			tDir = &tDirTable2[ dir ];
			cell = &(*cellArray)[ qi.u + tDir->u ][ qi.v + tDir->v ];

			//  Only visit each cell once. Do this before terrain
			//  is checked, to save time.
			if (*cell & cellVisited) continue;

			testPt = centerPt + tDirTable3[ dir ];

			//  Get info about the terrain at that point
			terrain =   tileTerrain(mapNum,
			                        centerTileCoords,
			                        moveMask[ 0 ],
			                        centerPt.z + 8,
			                        centerPt.z + 68);

			//  Reject if we can't move
			if (terrain & (terrainImpassable | terrainRaised)) {
				//  But only if this is isn't the center point
				if (distFromCenter > 0) continue;
			}

			//  Get the height of the terrain at the new point
			testPt.z =  tileSlopeHeight(testPt, mapNum, 68, &sti);

			//  If it's too high to step, then don't continue
//			if (testPt.z - qi.z > maxStepHeight) continue;
			fromSubPt = centerPt;
			for (i = 0; i < tileSubSize; i++) {
				int16       deltaZ;

				//  Next sub tile
				toSubPt = fromSubPt + tDirTable[ dir ];
				toSubPt.z = tileSlopeHeight(toSubPt, mapNum, 68);

				deltaZ = toSubPt.z - fromSubPt.z;

				//  If it's too high to step, then don't continue
				if (deltaZ > maxStepHeight || deltaZ < -(maxStepHeight * 2)) {
					traversable = FALSE;
					break;
				}

				fromSubPt = toSubPt;
			}

			if (!traversable) continue;


			//  Get info about terrain at new point
			terrain =   tileTerrain(mapNum,
			                        centerTileCoords + *tDir,
			                        moveMask[ 1 ],
			                        testPt.z + 8,
			                        testPt.z + 68);

			//  Reject if terrain allows no entry
			if (terrain & (terrainImpassable | terrainRaised)) continue;

#if VISUAL6
			TPLine(centerPt, testPt);
#endif

			*cell |= cellVisited;

			//  Push the new point onto the queue.
			//  (Cost is random so as to allow site selection to
			//  be somewhat non-deterministic).
			spush(TilePoint(qi.u + tDir->u,
			                qi.v + tDir->v,
			                testPt.z),
			      qi.cost + (rand() & 3),
			      dir);
		}
	}

	RDisposePtr(cellArray);

	return  bestLoc != Nowhere
	        ?   TilePoint(
	            ((bestLoc.u + baseTileCoords.u) << tileUVShift) + tileUVSize / 2,
	            ((bestLoc.v + baseTileCoords.v) << tileUVShift) + tileUVSize / 2,
	            bestLoc.z)
	        :   Nowhere;
}

#if 0

//  Put a and b in ascending order.

inline void order(int16 &a, int16 &b) {
	if (a > b) {
		int16 t = a;
		a = b;
		b = t;
	}
}

/*
template<class T> inline void order( T &a, T &b )
{
    if (a > b) { T t = a; a = b; b = t; }
}
*/

TilePoint selectDistantSite(
    Object          worldID,
    TilePoint       minCoords,              // coords in tiles (ul of rect)
    TilePoint       maxCoords,              // coords in tiles (lr of rect)
    int             metaProperties) {
	GameWorld       *world = (GameWorld *)GameObject::objectAddress(worldID);
	int32           u, v;
	int32           mapSize = mapList[ world->mapNum ].mapSize * platformWidth;
	int             matchCount = 0;

	//  Make sure the location spec'd is within the bounds of the map
	minCoords.u = clamp(0, minCoords.u, mapSize);
	minCoords.v = clamp(0, minCoords.v, mapSize);
	maxCoords.u = clamp(0, maxCoords.u, mapSize);
	maxCoords.v = clamp(0, maxCoords.v, mapSize);

	//  Make sure that the coords are in order
	order(minCoords.u, maxCoords.u);
	order(minCoords.v, maxCoords.v);

	//  convert to metatile coords
	minCoords.u = minCoords.u >> platformShift;
	minCoords.v = minCoords.v >> platformShift;
	maxCoords.u = (maxCoords.u + platformWidth - 1) >> platformShift;
	maxCoords.v = (maxCoords.v + platformWidth - 1) >> platformShift;

	//  Now, scan that area for metatiles...
	for (u = minCoords.u; u < maxCoords.u; u++) {
		for (v = minCoords.v; v < maxCoords.v; v++) {
			//  Check the metatile at this location,
			//  and determine if it has the correct properties.

			//  Need to ask David R. how to use meta properties...
		}
	}
}


#endif

bool checkPath(
    ObjectID            worldID,
    uint8               height,
    const TilePoint     &startingPt,
    const TilePoint     &destPt) {
	TilePoint       startingCoords = startingPt,
	                destCoords = destPt,
	                startingTileCoords,
	                destTileCoords;
	TilePoint       baseCoords,
	                baseTileCoords,
	                centerPt;               // The current center coordinates
	int           minTileRegU,
	              minTileRegV,
	              maxTileRegU,
	              maxTileRegV,
	              curTileRegU,
	              curTileRegV;

	int16           mapNum = GameWorld::IDtoMapNum(worldID);

	SimpleCellArray *cellArray;

	QueueItem       qi;

	StandingTileInfo sti;

	startingTileCoords.u = startingCoords.u >> tileUVShift;
	startingTileCoords.v = startingCoords.v >> tileUVShift;
	startingTileCoords.z = 0;

	destTileCoords.u = destCoords.u >> tileUVShift;
	destTileCoords.v = destCoords.v >> tileUVShift;
	destTileCoords.z = 0;

	//  Quantize destination coords to nearest tile center
	destCoords.u = (destTileCoords.u << tileUVShift) + tileUVSize / 2;
	destCoords.v = (destTileCoords.v << tileUVShift) + tileUVSize / 2;
	destCoords.z = tileSlopeHeight(destCoords, mapNum, height);

	//  Determine if destination is outside the search region
	if (destTileCoords.u < startingTileCoords.u - searchCenter
	        ||  destTileCoords.u >= startingTileCoords.u + searchCenter
	        ||  destTileCoords.v < startingTileCoords.v - searchCenter
	        ||  destTileCoords.v >= startingTileCoords.v + searchCenter)
		return FALSE;

	//  Allocate the array of cells
	cellArray = (SimpleCellArray *)
	            RNewPtr(sizeof * cellArray, NULL, "path cell array");
	if (cellArray == NULL) return FALSE;

	//  Calculate where search cells will be projected onto map
	baseTileCoords.u = startingTileCoords.u - searchCenter;
	baseTileCoords.v = startingTileCoords.v - searchCenter;
	baseTileCoords.z = 0;

	baseCoords.u = baseTileCoords.u << tileUVShift;
	baseCoords.v = baseTileCoords.v << tileUVShift;
	baseCoords.z = 0;

	//  Clear the search array and the queue
	memset(cellArray, cellUnvisited, sizeof(*cellArray));
	squeue.clear();

	//  Push the starting location in the center of the array.
	minTileRegU = (startingCoords.u - tileUVSize / 2) >> tileUVShift;
	minTileRegV = (startingCoords.v - tileUVSize / 2) >> tileUVShift;
	maxTileRegU = (startingCoords.u + tileUVSize / 2 + tileUVMask)
	              >>  tileUVShift;
	maxTileRegV = (startingCoords.v + tileUVSize / 2 + tileUVMask)
	              >>  tileUVShift;

	for (curTileRegU = minTileRegU;
	        curTileRegU < maxTileRegU;
	        curTileRegU++) {
		for (curTileRegV = minTileRegV;
		        curTileRegV < maxTileRegV;
		        curTileRegV++) {
			TilePoint       quantizedCoords,
			                offsetVector;
			int16           dist,
			                zDist,
			                cost;

			//  Quantize this tile position to the tile center
			quantizedCoords.u = (curTileRegU << tileUVShift) + tileUVSize / 2;
			quantizedCoords.v = (curTileRegV << tileUVShift) + tileUVSize / 2;
			quantizedCoords.z = startingCoords.z;
			quantizedCoords.z = tileSlopeHeight(quantizedCoords, mapNum, height);

			//  If the height difference is too great skip this tile
			//  position
			if (abs(quantizedCoords.z - startingCoords.z) > maxStepHeight)
				continue;

			//  Compute initial cost based upon the distance from the
			//  starting location
			offsetVector = quantizedCoords - startingCoords;
			dist = offsetVector.quickHDistance();
			zDist = abs(offsetVector.z);
			cost = dist + zDist;

			//  Push this point
			spush(
			    TilePoint(
			        curTileRegU - baseTileCoords.u,
			        curTileRegV - baseTileCoords.v,
			        quantizedCoords.z),
			    cost + 1,
			    0);
		}
	}

	while (squeue.remove(qi)) {
		TilePoint   centerTileCoords;
		TilePoint   *tDir;
		int16       centerDistFromDest;
		int       dir;

		//  Calculate the coordinates of the center in tiles
		centerTileCoords.u = qi.u + baseTileCoords.u;
		centerTileCoords.v = qi.v + baseTileCoords.v;
		centerTileCoords.z = 0;

		centerPt.u = (centerTileCoords.u << tileUVShift) + tileUVSize / 2;
		centerPt.v = (centerTileCoords.v << tileUVShift) + tileUVSize / 2;
		centerPt.z = qi.z;

		centerDistFromDest = (centerPt - destCoords).quickHDistance();

		for (dir = dirUpLeft;
		        dir <= dirUpRight;
		        dir += 2) {
			uint32          terrain;
			uint8           *cell;
			TilePoint       testPt,
			                testTileCoords,
			                fromSubPt,
			                toSubPt;
			int16           testDistFromDest,
			                deltaDistFromDest;
			int           i;
			bool            traversable = TRUE;

			uint16          *moveMask = &sTerrainMasks[ dir - 1 ];

			tDir = &tDirTable2[ dir ];

			testTileCoords.u = centerTileCoords.u + tDir->u;
			testTileCoords.v = centerTileCoords.v + tDir->v;
			testTileCoords.z = 0;

			cell = &(*cellArray)[ qi.u + tDir->u ][ qi.v + tDir->v ];

			//  Only visit each cell once..
			if (*cell & cellVisited) continue;

			testPt = centerPt + tDirTable3[ dir ];

			testDistFromDest = (testPt - destCoords).quickHDistance();
			deltaDistFromDest = testDistFromDest - centerDistFromDest;

			//  Get info about the terrain at that point
			terrain =   tileTerrain(mapNum,
			                        centerTileCoords,
			                        moveMask[ 0 ],
			                        centerPt.z + 8,
			                        centerPt.z + height);

			//  Reject if we can't move
			if (terrain & (terrainImpassable | terrainRaised)) continue;

			//  Get the height of the terrain at the new point
			testPt.z =  tileSlopeHeight(testPt, mapNum, height, &sti);

			fromSubPt = centerPt;
			for (i = 0; i < tileSubSize; i++) {
				int16       deltaZ;

				//  Next sub tile
				toSubPt = fromSubPt + tDirTable[ dir ];
				toSubPt.z = tileSlopeHeight(toSubPt, mapNum, height);

				deltaZ = toSubPt.z - fromSubPt.z;

				//  If it's too high to step, then don't continue
				if (deltaZ > maxStepHeight || deltaZ < -(maxStepHeight * 2)) {
					traversable = FALSE;
					break;
				}

				fromSubPt = toSubPt;
			}

			if (!traversable) continue;

			//  Get info about terrain at new point
			terrain =   tileTerrain(mapNum,
			                        centerTileCoords + *tDir,
			                        moveMask[ 1 ],
			                        testPt.z + 8,
			                        testPt.z + height);

			//  Reject if terrain allows no entry
			if (terrain & (terrainImpassable | terrainRaised)) continue;

#if VISUAL7
			TPLine(centerPt, testPt);
#endif

			*cell |= cellVisited;

			//  If we're there, we're done
			if (testTileCoords == destTileCoords) {
				RDisposePtr(cellArray);

				//  If the resulting height is significantly different
				//  from the destination height, assume we're on a
				//  different level and return FALSE.
				return abs(testPt.z - destCoords.z) <= maxStepHeight;
			}


			//  Push the new point onto the queue.
			spush(TilePoint(qi.u + tDir->u,
			                qi.v + tDir->v,
			                testPt.z),
			      qi.cost + (deltaDistFromDest + tileUVSize) / 4,
			      dir);
		}
	}

	RDisposePtr(cellArray);

	//  If we're here we've haven't found a path
	return FALSE;
}

} // end of namespace Saga2