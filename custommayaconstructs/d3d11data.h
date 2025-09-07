#pragma once
#include <maya/MPxData.h>
#include <maya/MTypeId.h>
#include <maya/MString.h>
#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

// Wrapper around a ComPtr to allow passing it through Maya DG nodes.
// This is intentionally non-serializable.
class D3D11Data : public MPxData
{
public:
    inline static MTypeId id = MTypeId(0x0007F005);
    inline static MString fullName = "D3D11Data";

    D3D11Data() = default;
    ~D3D11Data() override = default;

    static void* creator() {
        return new D3D11Data();
    }

    MTypeId typeId() const override { return id; }
    MString name() const override { return fullName; }

    void copy(const MPxData& src) override {
        const D3D11Data* d3dData = static_cast<const D3D11Data*>(&src);
        srv = d3dData->srv;
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

    void setSRV(const ComPtr<ID3D11ShaderResourceView>& srv) {
        this->srv = srv;
    }

    ComPtr<ID3D11ShaderResourceView> getSRV() const {
        return srv;
    }

private:
    // For now, only need SRVs. Could be expanded to UAVs and/or buffers if needed.
    ComPtr<ID3D11ShaderResourceView> srv;
};