#include <maya/MPxNode.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MTypeId.h>

class VoxelSimulationNode : public MPxNode {
public:
    static MTypeId id;
    static MObject relaxationAttr;
    static MObject edgeUniformityAttr;

    static void* creator() {
        return new VoxelSimulationNode();
    }

    static MStatus initialize() {
        MFnNumericAttribute nAttr;

        // Relaxation attribute (float, range 0 to 1)
        relaxationAttr = nAttr.create("relaxation", "rel", MFnNumericData::kFloat, 0.5);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0f);
        nAttr.setMax(1.0f);
        addAttribute(relaxationAttr);

        // Edge Uniformity attribute (float, range 0 to 1)
        edgeUniformityAttr = nAttr.create("edgeUniformity", "eu", MFnNumericData::kFloat, 0.5);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0f);
        nAttr.setMax(1.0f);
        addAttribute(edgeUniformityAttr);

        return MS::kSuccess;
    }

    VoxelSimulationNode() {}
    ~VoxelSimulationNode() override {}
};

// Unique ID for the node
MTypeId VoxelSimulationNode::id(0x0007F123); // Replace with your unique ID
MObject VoxelSimulationNode::relaxationAttr;
MObject VoxelSimulationNode::edgeUniformityAttr;