#pragma once

#include <maya/MPxLocatorNode.h>
#include <maya/MTypeId.h>
#include <maya/MString.h>
#include <maya/MStatus.h>
#include <maya/MUIDrawManager.h>
#include <maya/MMatrix.h>
#include <maya/MColor.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MDagModifier.h>
#include <maya/MDGModifier.h>
#include <maya/MPxCommand.h>
#include <maya/MSelectionList.h>
#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MSyntax.h>
#include <maya/MFnDagNode.h>
#include "../data/colliderdata.h"
#include "../../globalsolver.h"

// Forward declaration
class CreateColliderCommand;


// Hard-limit number of colliders to 256. This is partly because dynamic-sized arrays
// are not supported by constant buffers. But also, collider primitives aren't optimized for performance.
// If there's ever a use case for more, would need to optimize collision code. Cbuffer can hold more, but could also use structured buffer.
#define MAX_COLLIDERS 256
struct ColliderBuffer {
    float worldMatrix[MAX_COLLIDERS][4][4];
    int numSpheres = 0;
    int numBoxes = 0;
    int numPlanes = 0;
    int numCylinders = 0;
    int numCapsules = 0;
    int padding[3]; // Padding to ensure 16-byte alignment
    float sphereRadius[MAX_COLLIDERS];
    float boxWidth[MAX_COLLIDERS];
    float boxHeight[MAX_COLLIDERS];
    float boxDepth[MAX_COLLIDERS];
    float planeWidth[MAX_COLLIDERS];
    float planeHeight[MAX_COLLIDERS];
    float cylinderRadius[MAX_COLLIDERS];
    float cylinderHeight[MAX_COLLIDERS];
    float capsuleRadius[MAX_COLLIDERS];
    float capsuleHeight[MAX_COLLIDERS];
};

/**
 * UI locator node for collision primitives.
 */
class ColliderLocator : public MPxLocatorNode {
public:

    ColliderLocator() {}
    ~ColliderLocator() override {}

    virtual void draw(MUIDrawManager& drawManager) = 0;
    virtual void prepareForDraw() = 0;
    virtual void writeDataIntoBuffer(const ColliderData* const data, ColliderBuffer& colliderBuffer) = 0;

protected:
    static MStatus initializeColliderDataAttribute(
        MObject& colliderDataAttr,
        MObject& worldMatrix
    ) {
        MStatus status;
        MFnTypedAttribute tAttr;
        colliderDataAttr = tAttr.create(colliderDataAttrName, "cd", ColliderData::id);
        tAttr.setStorable(false);
        tAttr.setReadable(true);
        tAttr.setWritable(false);

        status = addAttribute(colliderDataAttr);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        MFnMatrixAttribute mAttr;
        worldMatrix = mAttr.create(worldMatrixAttrName, "wmi", MFnMatrixAttribute::kDouble);
        mAttr.setStorable(false);
        mAttr.setReadable(false);
        mAttr.setWritable(true);

        status = addAttribute(worldMatrix);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        
        status = attributeAffects(worldMatrix, colliderDataAttr);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        return status;
    }

private:
    friend class CreateColliderCommand;
    inline static const MString colliderDataAttrName = MString("colliderData");
    // Can't use the name "worldMatrix" here because Maya uses that already for an _output_ attribute
    inline static const MString worldMatrixAttrName = MString("worldMatrixIn");
};


/**
 * Callable command from MEL shelf button to create collider nodes.
 */
class CreateColliderCommand : public MPxCommand {
public:
    inline static const MString commandName = MString("createCollider");

	static void* creator() {
        return new CreateColliderCommand();
    }
    
    static MSyntax syntax() {
        MSyntax syntax;
        syntax.addFlag("-n", "-name", MSyntax::kString);
        return syntax;
    }

    // Create a collider of a given type (by type name)
	MStatus doIt(const MArgList& args) override {
        MString colliderName;
        MArgDatabase argData(syntax(), args);
        argData.getFlagArgument("-n", 0, colliderName);

        // Get the selected object to parent the collider under
        MSelectionList selection;
        MGlobal::getActiveSelectionList(selection);
        MDagPath selectedDagPath;
        if (!selection.isEmpty()) {
            selection.getDagPath(0, selectedDagPath);
        }

        // Create a transform under the selected object (or world if nothing selected)
        MDagModifier dagMod;
        MObject parentObj = (selectedDagPath.length() > 0) ? selectedDagPath.node() : MObject::kNullObj;
        MObject colliderParentObj = dagMod.createNode("transform", parentObj);
        dagMod.doIt();
        MFnDagNode fnParent(colliderParentObj);
        fnParent.setName(colliderName + "Transform");

        // Create the collider shape node under the transform
        MObject colliderNodeObj = dagMod.createNode(colliderName, colliderParentObj);
        dagMod.doIt();
        MFnDagNode fnCollider(colliderNodeObj);
        fnCollider.setName(colliderName + "Shape#");

        // Connect the transform's worldMatrix to the collider's worldMatrixIn attribute
        MFnDependencyNode fnColliderDep(colliderNodeObj);
        MFnDependencyNode fnTransformDep(colliderParentObj);
        MPlug worldMatrixPlug = fnTransformDep.findPlug("worldMatrix", false);
        MPlug worldMatrixElemPlug = worldMatrixPlug.elementByLogicalIndex(0); // Safe to access 0 element. Plug is array in case of instancing.
        MPlug worldMatrixInPlug = fnColliderDep.findPlug(ColliderLocator::worldMatrixAttrName, false);

        MDGModifier dgMod;
        dgMod.connect(worldMatrixElemPlug, worldMatrixInPlug);
        dgMod.doIt();

        // Connect the colliderData attribute to the global solver's colliderDataArray attribute
        MObject globalSolverNodeObj = GlobalSolver::getOrCreateGlobalSolver();
        MPlug globalSolverColliderDataArrayPlug = MFnDependencyNode(globalSolverNodeObj).findPlug(GlobalSolver::aColliderData, false);
        uint plugIndex = GlobalSolver::getNextArrayPlugIndex(globalSolverColliderDataArrayPlug);
        MPlug globalSolverColliderDataPlug = globalSolverColliderDataArrayPlug.elementByLogicalIndex(plugIndex);
        
        MPlug colliderDataPlug = fnColliderDep.findPlug(ColliderLocator::colliderDataAttrName, false);
        dgMod.connect(colliderDataPlug, globalSolverColliderDataPlug);
        dgMod.doIt();

        MGlobal::executeCommand(MString("showEditor \"" + fnCollider.name() + "\";"));
        return MStatus::kSuccess;
    }
};