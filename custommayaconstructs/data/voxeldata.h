#pragma once
#include <maya/MPxData.h>
#include <maya/MTypeId.h>
#include <maya/MString.h>
#include <maya/MSharedPtr.h>
#include "../../voxelizer.h"

/**
 * This is a custom attribute data type used for storing Voxel data on the PBD node type.
 */
class VoxelData : public MPxData {
public:
    inline static MTypeId id = MTypeId(0x0007F001);
    inline static MString fullName = "VoxelData";
    
    VoxelData() : voxels() {}
    ~VoxelData() = default;

    static void* creator() {
        return new VoxelData();
    }

    // Only serializing the fields of Voxels that this node actually needs
    MStatus writeBinary(std::ostream& out) override {
        if (!voxels) return MS::kFailure;
        double voxelSize = voxels->voxelSize;
        out.write(reinterpret_cast<const char*>(&voxelSize), sizeof(voxelSize));
        
        size_t size = voxels->size();
        out.write(reinterpret_cast<const char*>(&size), sizeof(size));

        out.write(reinterpret_cast<const char*>(voxels->isSurface.data()), size * sizeof(uint));
        out.write(reinterpret_cast<const char*>(voxels->dimensions.data()), size * sizeof(VoxelDimensions));
        out.write(reinterpret_cast<const char*>(voxels->mortonCodes.data()), size * sizeof(uint32_t));

        // If it proves to be too slow to serialize the map entry-by-entry, try copying it first into a vector of pairs for one contiguous write.
        size_t mapSize = voxels->mortonCodesToSortedIdx.size();
        out.write(reinterpret_cast<const char*>(&mapSize), sizeof(mapSize));
        for (const auto& pair : voxels->mortonCodesToSortedIdx) {
            out.write(reinterpret_cast<const char*>(&pair.first), sizeof(uint32_t));
            out.write(reinterpret_cast<const char*>(&pair.second), sizeof(uint32_t));
        }

        // Voxelization Grid
        out.write(reinterpret_cast<const char*>(&voxelizationGrid.gridEdgeLength), sizeof(voxelizationGrid.gridEdgeLength));
        out.write(reinterpret_cast<const char*>(&voxelizationGrid.voxelsPerEdge), sizeof(voxelizationGrid.voxelsPerEdge));
        out.write(reinterpret_cast<const char*>(&voxelizationGrid.gridCenter), sizeof(voxelizationGrid.gridCenter));

        return MS::kSuccess;
    }

    MStatus readBinary(std::istream& in, unsigned int length) override {
        voxels = MSharedPtr<Voxels>::make();
        in.read(reinterpret_cast<char*>(&voxels->voxelSize), sizeof(voxels->voxelSize));

        size_t size;
        in.read(reinterpret_cast<char*>(&size), sizeof(size));
        voxels->resize(static_cast<int>(size));

        in.read(reinterpret_cast<char*>(voxels->isSurface.data()), size * sizeof(uint));
        in.read(reinterpret_cast<char*>(voxels->dimensions.data()), size * sizeof(VoxelDimensions));
        in.read(reinterpret_cast<char*>(voxels->mortonCodes.data()), size * sizeof(uint32_t));

        size_t mapSize;
        in.read(reinterpret_cast<char*>(&mapSize), sizeof(mapSize));
        for (size_t i = 0; i < mapSize; ++i) {
            uint32_t key, value;
            in.read(reinterpret_cast<char*>(&key), sizeof(uint32_t));
            in.read(reinterpret_cast<char*>(&value), sizeof(uint32_t));
            voxels->mortonCodesToSortedIdx[key] = value;
        }

        // Voxelization Grid
        in.read(reinterpret_cast<char*>(&voxelizationGrid.gridEdgeLength), sizeof(voxelizationGrid.gridEdgeLength));
        in.read(reinterpret_cast<char*>(&voxelizationGrid.voxelsPerEdge), sizeof(voxelizationGrid.voxelsPerEdge));
        in.read(reinterpret_cast<char*>(&voxelizationGrid.gridCenter), sizeof(voxelizationGrid.gridCenter));

        return MS::kSuccess;
    }

    MStatus writeASCII(std::ostream& out) override {
        return MS::kNotImplemented;
    }

    MStatus readASCII(const MArgList& argList, unsigned int& endOfTheLastParsedElement) override {
        return MS::kNotImplemented;
    }

    void copy(const MPxData& src) override {
        const VoxelData& voxelData = static_cast<const VoxelData&>(src);
        voxels = voxelData.voxels;
        voxelizationGrid = voxelData.voxelizationGrid;
    }

    MTypeId typeId() const override { return id; }
    MString name() const override { return fullName; }
    MSharedPtr<Voxels> getVoxels() const { return voxels; }
    const VoxelizationGrid& getVoxelizationGrid() const { return voxelizationGrid; }

    void setVoxels(MSharedPtr<Voxels> voxels) {
        this->voxels = voxels;
    }

    void setVoxelizationGrid(const VoxelizationGrid& grid) {
        voxelizationGrid = grid;
    }

private:
    MSharedPtr<Voxels> voxels;
    VoxelizationGrid voxelizationGrid;
};
