///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2012-2013 DreamWorks Animation LLC
//
// All rights reserved. This software is distributed under the
// Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
//
// Redistributions of source code must retain the above copyright
// and license notice and the following restrictions and disclaimer.
//
// *     Neither the name of DreamWorks Animation nor the names of
// its contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// IN NO EVENT SHALL THE COPYRIGHT HOLDERS' AND CONTRIBUTORS' AGGREGATE
// LIABILITY FOR ALL CLAIMS REGARDLESS OF THEIR BASIS EXCEED US$250.00.
//
///////////////////////////////////////////////////////////////////////////
//
/// @file Dense.h
///
/// @brief This file defines a simple dense grid and efficient
/// converters to and from VDB grids.

#ifndef OPENVDB_TOOLS_DENSE_HAS_BEEN_INCLUDED
#define OPENVDB_TOOLS_DENSE_HAS_BEEN_INCLUDED

#include <openvdb/Types.h>
#include <openvdb/Grid.h>
#include <openvdb/Exceptions.h>
#include <tbb/parallel_for.h>

namespace openvdb {
OPENVDB_USE_VERSION_NAMESPACE
namespace OPENVDB_VERSION_NAME {
namespace tools {

// Forward declaration (see definition below)
template<typename ValueT> class Dense;


/// @brief Populate a dense grid with the values of voxels from a sparse grid,
/// where the sparse grid intersects the dense grid.
/// @param sparse  an OpenVDB grid or tree from which to copy values
/// @param dense   the dense grid into which to copy values
/// @param serial  if false, process voxels in parallel
template<typename GridOrTreeT>
void
copyToDense(
    const GridOrTreeT& sparse,
    Dense<typename GridOrTreeT::ValueType>& dense,
    bool serial = false);


/// @brief Populate a sparse grid with the values of all of the voxels of a dense grid.
/// @param dense      the dense grid from which to copy values
/// @param sparse     an OpenVDB grid or tree into which to copy values
/// @param tolerance  values in the dense grid that are within this tolerance of the sparse
///     grid's background value become inactive background voxels or tiles in the sparse grid
/// @param serial     if false, process voxels in parallel
template<typename GridOrTreeT>
void
copyFromDense(
    const Dense<typename GridOrTreeT::ValueType>& dense,
    GridOrTreeT& sparse,
    const typename GridOrTreeT::ValueType& tolerance,
    bool serial = false);


////////////////////////////////////////


/// @brief Dense is a simple dense grid API used by the CopyToDense and
/// CopyFromDense classes defined below.
/// @details Use the Dense class to efficiently produce a dense in-memory
/// representation of an OpenVDB grid.  However, be aware that a dense grid
/// could have a memory footprint that is orders of magnitude larger than
/// the corresponding sparse grid from which it originates.
///
/// @note This class can be used as a simple wrapper for existing dense grid
/// classes if they provide access to the raw data array.
/// @note This implementation assumes a data layout where @e z is the
/// fastest-changing index (because that is the layout used by OpenVDB grids).
template<typename ValueT>
class Dense
{
public:
    /// @brief Construct a dense grid with a given range of coordinates.
    ///
    /// @param bbox  the bounding box of the (signed) coordinate range of this grid
    /// @throw ValueError if the bounding box is empty.
    Dense(const CoordBBox& bbox)
        : mBbox(bbox), mArray(new ValueT[bbox.volume()]), mData(mArray.get()),
          mY(bbox.dim()[2]), mX(mY*bbox.dim()[1])
    {
        if (bbox.empty()) {
            OPENVDB_THROW(ValueError, "can't construct a dense grid with an empty bounding box");
        }
    }

    /// @brief Construct a dense grid that wraps an external array.
    ///
    /// @param bbox  the bounding box of the (signed) coordinate range of this grid
    /// @param data  a raw C-style array whose size is commensurate with
    ///     the coordinate domain of @a bbox
    ///
    /// @note The data array is assumed to have a stride of one in the @e z direction.
    /// @throw ValueError if the bounding box is empty.
    Dense(const CoordBBox& bbox, ValueT* data)
        : mBbox(bbox), mData(data), mY(bbox.dim()[2]), mX(mY*bbox.dim()[1])
    {
        if (bbox.empty()) {
            OPENVDB_THROW(ValueError, "can't construct a dense grid with an empty bounding box");
        }
    }

    /// @brief Construct a dense grid with a given origin and dimensions.
    ///
    /// @param dim  the desired dimensions of the grid
    /// @param min  the signed coordinates of the first voxel in the dense grid
    /// @throw ValueError if any of the dimensions are zero.
    Dense(const Coord& dim, const Coord& min = Coord(0))
        : mBbox(min, min+dim.offsetBy(-1)), mArray(new ValueT[mBbox.volume()]),
          mData(mArray.get()), mY(bbox.dim()[2]), mX(mY*bbox.dim()[1])
    {
        if (bbox.empty()) {
            OPENVDB_THROW(ValueError, "can't construct a dense grid of size zero");
        }
    }

    /// @brief Return a raw pointer to this grid's value array.
    ///
    /// @note This method is required by CopyToDense.
    ValueT* data() { return mData; }

    /// @brief Return a raw pointer to this grid's value array.
    ///
    /// @note This method is required by CopyFromDense.
    const ValueT* data() const { return mData; }

    /// @brief Return the bounding box of the signed index domain of this grid.
    ///
    /// @note This method is required by both CopyToDense and CopyFromDense.
    const CoordBBox& bbox() const { return mBbox; }

    /// @brief Return the stride of the array in the x direction ( = dimY*dimZ).
    ///
    /// @note This method is required by both CopyToDense and CopyFromDense.
    size_t xStride() const { return mX; }

    /// @brief Return the stride of the array in the y direction ( = dimZ).
    ///
    /// @note This method is required by both CopyToDense and CopyFromDense.
    size_t yStride() const { return mY; }

    /// @brief Return the number of voxels contained in this grid.
    size_t valueCount() const { return mBbox.volume(); }

    /// @brief Set the value of the voxel at the given array offset.
    void setValue(size_t offset, const ValueT& value) { mData[offset] = value; }
    /// @brief Return the value of the voxel at the given array offset.
    const ValueT& getValue(size_t offset) const { return mData[offset]; }
    /// @brief Set the value of the voxel at unsigned index coordinates (i, j, k).
    /// @note This is somewhat slower than using an array offset.
    void setValue(size_t i, size_t j, size_t k, const ValueT& value)
    {
        mData[this->coordToOffset(i,j,k)] = value;
    }
    /// @brief Return the value of the voxel at unsigned index coordinates (i, j, k).
    /// @note This is somewhat slower than using an array offset.
    const ValueT& getValue(size_t i, size_t j, size_t k) const
    {
        return mData[this->coordToOffset(i,j,k)];
    }
    /// @brief Set the value of the voxel at the given signed coordinates.
    /// @note This is slower than using either an array offset or unsigned index coordinates.
    void setValue(const Coord& xyz, const ValueT& value)
    {
        mData[this->coordToOffset(xyz)] = value;
    }

    /// @brief Return the value of the voxel at the given signed coordinates.
    /// @note This is slower than using either an array offset or unsigned index coordinates.
    const ValueT& getValue(const Coord& xyz) const
    {
        return mData[this->coordToOffset(xyz)];
    }

    /// @brief Fill this grid with a constant value.
    void fill(const ValueT& value)
    {
        size_t size = this->valueCount();
        ValueT* a = mData;
        while(size--) *a++ = value;
    }

    /// @brief Return the linear offset into this grid's value array given by
    /// unsigned coordinates (i, j, k), i.e., coordinates relative to
    /// the origin of this grid's bounding box.
    ///
    /// @note This method reflects the fact that we assume the same layout
    /// of values as an OpenVDB grid, i.e., the fastest coordinate is @e k.
    inline size_t coordToOffset(size_t i, size_t j, size_t k) const
    {
        return k + j*mY + i*mX;
    }

    /// @brief Return the linear offset into this grid's value array given by
    /// the specified signed coordinates, i.e., coordinates in the space of
    /// this grid's bounding box.
    ///
    /// @note This method reflects the fact that we assume the same
    /// layout of values as an OpenVDB grid, i.e., the fastest coordinate is @e z.
    inline size_t coordToOffset(Coord xyz) const
    {
        assert(mBbox.isInside(xyz));
        xyz -= mBbox.min();
        return this->coordToOffset(size_t(xyz[0]), size_t(xyz[1]), size_t(xyz[2]));
    }

private:

    const CoordBBox mBbox;
    boost::shared_array<ValueT> mArray;
    ValueT*         mData;
    const size_t    mY, mX;//strides in x and y
};// end of Dense


////////////////////////////////////////


/// @brief Copy an OpenVDB tree into an existing dense grid.
///
/// @note Only the voxels enclosed by the existing dense grid are copied
/// from the OpenVDB tree.
template<typename TreeT>
class CopyToDense
{
public:
    typedef typename TreeT::ValueType    ValueT;

    CopyToDense(const TreeT& tree, Dense<ValueT> &dense)
        : mRoot(tree.root()), mDense(dense) {}

    void copy(bool serial = false) const
    {
        if (serial) {
            mRoot.copyToDense(mDense.bbox(), mDense);
        } else {
            tbb::parallel_for(mDense.bbox(), *this);
        }
    }

    /// @brief Public method called by tbb::parallel_for
    void operator()(const CoordBBox& bbox) const
    {
        mRoot.copyToDense(bbox, mDense);
    }

private:
    const typename TreeT::RootNodeType &mRoot;
    Dense<ValueT>                      &mDense;
};// CopyToDense


// Convenient wrapper function for the CopyToDense class
template<typename GridOrTreeT>
void
copyToDense(const GridOrTreeT& sparse, Dense<typename GridOrTreeT::ValueType>& dense, bool serial)
{
    typedef TreeAdapter<GridOrTreeT> Adapter;
    typedef typename Adapter::TreeType TreeT;

    CopyToDense<TreeT> op(Adapter::constTree(sparse), dense);
    op.copy(serial);
}


////////////////////////////////////////


/// @brief Copy the values from a dense grid into an OpenVDB tree.
///
/// @details Values in the dense grid that are within a tolerance of
/// the background value are truncated to inactive background voxels or tiles.
/// This allows the tree to form a sparse representation of the dense grid.
///
/// @note Since this class allocates leaf nodes concurrently it is recommended
/// to use a scalable implementation of @c new like the one provided by TBB,
/// rather than the mutex-protected standard library @c new.
template<typename TreeT>
class CopyFromDense
{
public:
    typedef typename TreeT::ValueType    ValueT;
    typedef typename TreeT::LeafNodeType LeafT;

    CopyFromDense(const Dense<ValueT>& dense, TreeT& tree, const ValueT& tolerance)
        : mDense(dense), mTree(tree), mBlocks(NULL), mTolerance(tolerance)
    {
    }

    /// @brief Copy values from the dense grid to the sparse tree.
    void copy(bool serial = false)
    {
        std::vector<Block> blocks;
        mBlocks = &blocks;
        const CoordBBox& bbox = mDense.bbox();
        // Pre-process: Construct a list of blocks alligned with (potential) leaf nodes
        for (CoordBBox sub=bbox; sub.min()[0] <= bbox.max()[0]; sub.min()[0] = sub.max()[0] + 1) {
            for (sub.min()[1] = bbox.min()[1]; sub.min()[1] <= bbox.max()[1];
                sub.min()[1] = sub.max()[1] + 1)
            {
                for (sub.min()[2] = bbox.min()[2]; sub.min()[2] <= bbox.max()[2];
                    sub.min()[2] = sub.max()[2] + 1)
                {
                    sub.max() = Coord::minComponent(bbox.max(),
                        (sub.min()&(~(LeafT::DIM-1u))).offsetBy(LeafT::DIM-1u));
                    blocks.push_back(Block(sub));
                }
            }
        }
        // Multi-threaded process: Convert dense grid into leaf nodes and tiles
        if (serial) {
            (*this)(tbb::blocked_range<size_t>(0, blocks.size()));
        } else {
            tbb::parallel_for(tbb::blocked_range<size_t>(0, blocks.size()), *this);
        }
        // Post-process: Insert leaf nodes and tiles into the tree, and prune the tiles only!
        /*
        for (size_t m=0, size = blocks.size(); m<size; ++m) {
            Block& block = blocks[m];
            if (block.leaf) {
                mTree.root().addLeaf(block.leaf);
            } else if (block.tile.second) {//only background tiles are inactive
                mTree.root().template addTile<1>(block.bbox.min(), block.tile.first, true);//leaf tile
            }
            }
        */
        ////////////////
        tree::ValueAccessor<TreeT> acc(mTree);
        for (size_t m=0, size = blocks.size(); m<size; ++m) {
            Block& block = blocks[m];
            if (block.leaf) {
                acc.addLeaf(block.leaf);
            } else if (block.tile.second) {//only background tiles are inactive
                acc.template addTile<1>(block.bbox.min(), block.tile.first, true);//leaf tile
            }
        }
        ////////////////
        mTree.root().pruneTiles(mTolerance);
    }

    /// @brief Public method called by tbb::parallel_for
    void operator()(const tbb::blocked_range<size_t> &r) const
    {
        LeafT* leaf = NULL;

        for (size_t m=r.begin(), n=0, end = r.end(); m != end; ++m, ++n) {

            if (leaf == NULL) leaf = new LeafT();

            Block& block = (*mBlocks)[m];
            const CoordBBox &bbox = block.bbox;

            leaf->copyFromDense(bbox, mDense, mTree.background(), mTolerance);

            if (!leaf->isConstant(block.tile.first, block.tile.second, mTolerance)) {
                leaf->setOrigin(bbox.min());
                block.leaf = leaf;
                leaf = NULL;
            }
        }// loop over blocks

        delete leaf;
    }

private:
    struct Block {
        CoordBBox               bbox;
        LeafT*                  leaf;
        std::pair<ValueT, bool> tile;
        Block(const CoordBBox& b) : bbox(b), leaf(NULL) {}
    };

    const Dense<ValueT>& mDense;
    TreeT&               mTree;
    std::vector<Block>*  mBlocks;
    ValueT               mTolerance;
};// CopyFromDense


// Convenient wrapper function for the CopyFromDense class
template<typename GridOrTreeT>
void
copyFromDense(const Dense<typename GridOrTreeT::ValueType>& dense, GridOrTreeT& sparse,
    const typename GridOrTreeT::ValueType& tolerance, bool serial)
{
    typedef TreeAdapter<GridOrTreeT> Adapter;
    typedef typename Adapter::TreeType TreeT;

    CopyFromDense<TreeT> op(dense, Adapter::tree(sparse), tolerance);
    op.copy(serial);
}

} // namespace tools
} // namespace OPENVDB_VERSION_NAME
} // namespace openvdb

#endif // OPENVDB_TOOLS_DENSE_HAS_BEEN_INCLUDED

// Copyright (c) 2012-2013 DreamWorks Animation LLC
// All rights reserved. This software is distributed under the
// Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
