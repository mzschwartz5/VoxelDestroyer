#pragma once

#include <maya/MPxData.h>
#include <functional>
#include <maya/MTypeId.h>
#include <maya/MString.h>

class FunctionalData : public MPxData {
public:
    using FunctionType = std::function<void()>;
    inline static MTypeId id = MTypeId(0x0007F004);
    inline static MString fullName = "FunctionalData";

    FunctionalData() = default;
    ~FunctionalData() = default;

    static void* creator() {
        return new FunctionalData();
    }

    MTypeId typeId() const override { return id; }
    MString name() const override { return fullName; }

    void copy(const MPxData& src) override {
        const FunctionalData* funcData = static_cast<const FunctionalData*>(&src);
        func = funcData->func;
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

    void setFunction(const FunctionType& f) {
        func = f;
    }

    const FunctionType& getFunction() const {
        return func;
    }

private:
    FunctionType func;
};