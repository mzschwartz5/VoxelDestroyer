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
#include "../data/colliderdata.h"
#include "../../globalsolver.h"

// Forward declaration
class CreateColliderCommand;

/**
 * UI locator node for collision primitives.
 */
class ColliderLocator : public MPxLocatorNode {
public:

    ColliderLocator() {}
    ~ColliderLocator() override {}

    virtual void draw(MUIDrawManager& drawManager) = 0;
    virtual void prepareForDraw() = 0;

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
        fnCollider.setName(colliderName + "Shape");

        // Connect the transform's worldMatrix to the collider's worldMatrixIn attribute
        MFnDependencyNode fnColliderDep(colliderNodeObj);
        MFnDependencyNode fnTransformDep(colliderParentObj);
        MPlug worldMatrixPlug = fnTransformDep.findPlug("worldMatrix", true);
        MPlug worldMatrixElemPlug = worldMatrixPlug.elementByLogicalIndex(0); // Safe to access 0 element. Plug is array in case of instancing.
        MPlug worldMatrixInPlug = fnColliderDep.findPlug(ColliderLocator::worldMatrixAttrName, true);

        MDGModifier dgMod;
        dgMod.connect(worldMatrixElemPlug, worldMatrixInPlug);
        dgMod.doIt();

        MGlobal::executeCommand(MString("showEditor \"" + fnCollider.name() + "\";"));
        return MStatus::kSuccess;
    }
};