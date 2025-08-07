#pragma once

#include <maya/MPxData.h>
#include <maya/MTypeId.h>
#include <maya/MString.h>
#include <vector>

/**
 * Custom attribute data class to hold deformer-related data. This isn't used for communicating between nodes. It's simply state for the deformer node to initialize,
 * and save to / load from file.
 */
class DeformerData : public MPxData {
public:
    inline static MTypeId id = MTypeId(0x0007F003);
    inline static MString fullName = "DeformerData";

    DeformerData() = default;
    ~DeformerData() = default;

    static void* creator() {
        return new DeformerData();
    }

    MStatus writeBinary(std::ostream& out) override {
        unsigned int size = static_cast<unsigned int>(vertexStartIdx.size());
        out.write(reinterpret_cast<const char*>(&size), sizeof(size));
        out.write(reinterpret_cast<const char*>(vertexStartIdx.data()), size * sizeof(uint));
        return MS::kSuccess;
    }

    MStatus readBinary(std::istream& in, unsigned int length) override {
        unsigned int size;
        in.read(reinterpret_cast<char*>(&size), sizeof(size));
        vertexStartIdx.resize(size);
        in.read(reinterpret_cast<char*>(vertexStartIdx.data()), size * sizeof(uint));
        return MS::kSuccess;
    }

    MStatus writeASCII(std::ostream& out) override {
        return MS::kNotImplemented;
    }

    MStatus readASCII(const MArgList& argList, unsigned int& endOfTheLastParsedElement) override {
        return MS::kNotImplemented;
    }

    MTypeId typeId() const override { return id; }
    MString name() const override { return fullName; }

    void copy(const MPxData& src) override {
        const DeformerData* deformerData = static_cast<const DeformerData*>(&src);
        this->vertexStartIdx = deformerData->getVertexStartIdx();
    }

    const std::vector<uint>& getVertexStartIdx() const {
        return vertexStartIdx;
    }

    void setVertexStartIdx(std::vector<uint>&& startIdx) {
        vertexStartIdx = std::move(startIdx);
    }

private:
    std::vector<uint> vertexStartIdx;
};