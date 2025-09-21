#pragma once

#include <maya/MPxData.h>
#include <maya/MTypeId.h>
#include <maya/MString.h>
#include <maya/MPoint.h>
#include <vector>

struct ParticleDataContainer {
    int numParticles = 0;
    std::vector<MFloatPoint>* particlePositionsCPU = nullptr;
    std::vector<uint>* isSurface = nullptr;
    float particleRadius = 0.0f;
};

/**
 * Custom attribute data class to hold particle-related data. This data is sent between the PBD node and the deformer node
 * after voxels and particles have been created, to precipitate the creation of GPU buffer resources for deformation.
 * 
 * Note: attributes of this type are not meant to be storable. This is just for communication / initialization of resources.
 */
class ParticleData : public MPxData {
public:
    inline static MTypeId id = MTypeId(0x0007F002);
    inline static MString fullName = "ParticleData";

    ParticleData() = default;
    ~ParticleData() = default;

    static void* creator() {
        return new ParticleData();
    }

    MTypeId typeId() const override { return id; }
    MString name() const override { return fullName; }

    void copy(const MPxData& src) override {
        const ParticleData* particleData = static_cast<const ParticleData*>(&src);
        dataContainer = particleData->dataContainer;
    }

    MStatus writeASCII(std::ostream& out) override {
        return MS::kNotImplemented;
    }

    MStatus readASCII(const MArgList& argList, unsigned int& endOfTheLastParsedElement) override {
        return MS::kNotImplemented;
    }

    MStatus writeBinary(std::ostream& out) override {
        return MS::kNotImplemented;
    }

    MStatus readBinary(std::istream& in, unsigned int length) override {
        return MS::kNotImplemented;
    }

    void setData(const ParticleDataContainer& container) {
        dataContainer = container;
    }

    const ParticleDataContainer& getData() const {
        return dataContainer;
    }

private:
    ParticleDataContainer dataContainer;
};