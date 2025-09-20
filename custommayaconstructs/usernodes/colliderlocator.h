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
#include <maya/MFileIO.h>
#include <maya/MMessage.h>
#include <maya/MDagMessage.h>
#include "../data/colliderdata.h"
#include "../../globalsolver.h"

// Forward declarations
class CreateColliderCommand;

/**
 * UI locator node for collision primitives.
 */
class ColliderLocator : public MPxLocatorNode {
public:

    ColliderLocator() {}
    ~ColliderLocator() override {}

    virtual void draw(MUIDrawManager& drawManager) = 0;
    virtual void prepareForDraw() {
        checkShouldDraw();
    }

    virtual void writeDataIntoBuffer(const ColliderData* const data, ColliderBuffer& colliderBuffer, int index = -1) = 0;

    void postConstructor() override {
        MObject thisObj = thisMObject();
        parentTransformAddedCallbackId = MDagMessage::addParentAddedCallback(ColliderLocator::parentAddedCallback, this);

        preRemovalCallbackId = MNodeMessage::addNodePreRemovalCallback(thisMObject(), [](MObject& node, void* clientData) {
                ColliderLocator* colliderLocator = static_cast<ColliderLocator*>(clientData);
                MMessage::removeCallback(colliderLocator->parentTransformAddedCallbackId);
                MMessage::removeCallback(colliderLocator->preRemovalCallbackId);
        }, this);

        // Connections get restored from file, no need to explicitly connect them.
        if (MFileIO::isReadingFile() || MFileIO::isOpeningFile()) {
            return;
        }

        // Connect the colliderData attribute to the global solver's colliderDataArray attribute
        // Note: this is not done in the MPxCommand below, because that would not cover the case of duplicating a collider node.
        MDGModifier dgMod;
        MObject globalSolverNodeObj = GlobalSolver::getOrCreateGlobalSolver();
        MPlug globalSolverColliderDataArrayPlug = MFnDependencyNode(globalSolverNodeObj).findPlug(GlobalSolver::aColliderData, false);
        uint plugIndex = GlobalSolver::getNextArrayPlugIndex(globalSolverColliderDataArrayPlug);
        MPlug globalSolverColliderDataPlug = globalSolverColliderDataArrayPlug.elementByLogicalIndex(plugIndex);
        MPlug colliderDataPlug = MFnDependencyNode(thisObj).findPlug(ColliderLocator::colliderDataAttrName, false);
        dgMod.connect(colliderDataPlug, globalSolverColliderDataPlug);

        dgMod.doIt();
    }

protected:
    bool shouldDraw = false;

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
    MCallbackId parentTransformAddedCallbackId = 0;
    MCallbackId preRemovalCallbackId = 0;

    // Only draw when locator or parent transform is selected
    void checkShouldDraw() {
        MSelectionList selection;
        MGlobal::getActiveSelectionList(selection);

        MDagPath thisDagPath;
        MDagPath::getAPathTo(thisMObject(), thisDagPath);

        if (selection.hasItem(thisDagPath)) {
            shouldDraw = true;
            return;
        }

        MDagPath parentDagPath = thisDagPath;
        parentDagPath.pop();

        if (selection.hasItem(parentDagPath)) {
            shouldDraw = true;
            return;
        }

        shouldDraw = false;
    }

    /**
     * Respond to (re)parenting the collider to a transform node.
     */
    static void parentAddedCallback(MDagPath& child, MDagPath& parent, void* clientData) {
        if (MFileIO::isReadingFile() || MFileIO::isOpeningFile()) return;

        ColliderLocator* colliderNode = static_cast<ColliderLocator*>(clientData);
        MObject thisObj = colliderNode->thisMObject();
        if (child.node() != thisObj) return;
        if (!parent.node().hasFn(MFn::kTransform)) {
            MGlobal::displayWarning("Collider nodes must be parented under a transform node.");
            return;
        }

        // Connect the transform's worldMatrix to the collider's worldMatrixIn attribute
        // Note: this is not done in the MPxCommand below, because that would not cover the case of duplicating nodes.
        //       and it is not done in the postConstructor, because that would not cover the case of reparenting a collider shape.
        MDGModifier dgMod;
        MFnDependencyNode fnColliderDep(thisObj);
        MFnDependencyNode fnTransformDep(parent.node());
        MPlug worldMatrixPlug = fnTransformDep.findPlug("worldMatrix", false);
        MPlug worldMatrixElemPlug = worldMatrixPlug.elementByLogicalIndex(0); // Safe to access 0 element. Plug is array in case of instancing.
        MPlug worldMatrixInPlug = fnColliderDep.findPlug(ColliderLocator::worldMatrixAttrName, false);

        // Disconnect any existing incoming connection(s) to worldMatrixIn
        MPlugArray connected;
        worldMatrixInPlug.connectedTo(connected, true, false);
        for (unsigned int i = 0; i < connected.length(); ++i) {
            dgMod.disconnect(connected[i], worldMatrixInPlug);
        }

        dgMod.connect(worldMatrixElemPlug, worldMatrixInPlug);
        dgMod.doIt();
    }
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

        MSelectionList newSelection;
        MDagPath parentDagPath;
        MDagPath::getAPathTo(colliderParentObj, parentDagPath);
        newSelection.add(parentDagPath);
        MGlobal::setActiveSelectionList(newSelection);

        MGlobal::executeCommand("setToolTo moveSuperContext");
        MGlobal::executeCommand(MString("showEditor \"" + fnCollider.name() + "\";"));
        return MStatus::kSuccess;
    }
};