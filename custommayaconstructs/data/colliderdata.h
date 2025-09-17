#pragma once

#include <maya/MPxData.h>
#include <maya/MTypeId.h>
#include <maya/MString.h>
#include <maya/MMatrix.h>

class ColliderData : public MPxData {
public:
    inline static MTypeId id = MTypeId(0x0007F003);
    inline static MString fullName = "ColliderData";

    ColliderData() = default;
    ~ColliderData() = default;

    static void* creator() {
        return new ColliderData();
    }

    MTypeId typeId() const override { return id; }
    MString name() const override { return fullName; }

    void copy(const MPxData& src) override {
        const ColliderData* funcData = static_cast<const ColliderData*>(&src);
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

private:
    MMatrix worldMatrix;

    // Collection of collider parameters. Not all parameters are used by all collider types.
    float width;
    float height;
    float depth;
    float radius;
    bool infinite;
};