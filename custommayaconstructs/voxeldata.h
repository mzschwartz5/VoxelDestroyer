#pragma once
#include <maya/MPxData.h>
#include <maya/MTypeId.h>
#include <maya/MString.h>
#include "../voxelizer.h"

class VoxelData : public MPxData {
public:
    static MTypeId id;

    VoxelData() = default;
    virtual ~VoxelData() = default;

    virtual MStatus writeBinary(std::ostream& out) override {
        size_t size = voxels.size();
        out.write(reinterpret_cast<const char*>(&size), sizeof(size));

        // Special treatment for the occupied vector because, in the standard library, vector<bool> is not a true vector but a bitset.
        // If this becomes a performance concern, we can change the type to std::vector<uint8_t>.
        for (size_t i = 0; i < size; ++i) {
            bool value = voxels.occupied[i];
            out.write(reinterpret_cast<const char*>(&value), sizeof(bool));
        }
        
        out.write(reinterpret_cast<const char*>(voxels.isSurface.data()), size * sizeof(uint));
        out.write(reinterpret_cast<const char*>(voxels.corners.data()), size * sizeof(VoxelPositions));
        out.write(reinterpret_cast<const char*>(voxels.vertStartIdx.data()), size * sizeof(uint));
        out.write(reinterpret_cast<const char*>(voxels.mortonCodes.data()), size * sizeof(uint32_t));

        size_t mapSize = voxels.mortonCodesToSortedIdx.size();
        out.write(reinterpret_cast<const char*>(&mapSize), sizeof(mapSize));
        for (const auto& pair : voxels.mortonCodesToSortedIdx) {
            out.write(reinterpret_cast<const char*>(&pair.first), sizeof(uint32_t));
            out.write(reinterpret_cast<const char*>(&pair.second), sizeof(uint32_t));
        }

        return MS::kSuccess;
    }

    virtual MStatus readBinary(std::istream& in, unsigned int length) override {
        size_t size;
        in.read(reinterpret_cast<char*>(&size), sizeof(size));
        voxels.resize(static_cast<int>(size));

        // Special treatment - see above comment
        for (size_t i = 0; i < size; ++i) {
            bool value;
            in.read(reinterpret_cast<char*>(&value), sizeof(bool));
            voxels.occupied[i] = value;
        }

        in.read(reinterpret_cast<char*>(voxels.isSurface.data()), size * sizeof(uint));
        in.read(reinterpret_cast<char*>(voxels.corners.data()), size * sizeof(VoxelPositions));
        in.read(reinterpret_cast<char*>(voxels.vertStartIdx.data()), size * sizeof(uint));
        in.read(reinterpret_cast<char*>(voxels.mortonCodes.data()), size * sizeof(uint32_t));

        size_t mapSize;
        in.read(reinterpret_cast<char*>(&mapSize), sizeof(mapSize));
        for (size_t i = 0; i < mapSize; ++i) {
            uint32_t key, value;
            in.read(reinterpret_cast<char*>(&key), sizeof(uint32_t));
            in.read(reinterpret_cast<char*>(&value), sizeof(uint32_t));
            voxels.mortonCodesToSortedIdx[key] = value;
        }

        return MS::kSuccess;
    }

    // This is sufficient for deep-copy so long as all members of the Voxels struct themselves continue to support deep-copy.
    virtual void copy(const MPxData& src) override {
        const VoxelData& voxelData = dynamic_cast<const VoxelData&>(src);
        voxels = voxelData.voxels;
    }

    // Override typeId and name methods
    virtual MTypeId typeId() const override { return id; }
    virtual MString name() const override { return "VoxelData"; }

private:
    Voxels voxels;
};
