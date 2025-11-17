#pragma once
#include <maya/MGlobal.h>
#include <maya/MPxCommand.h>
#include <maya/MSelectionList.h>
#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MSyntax.h>
#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include "../../utils.h"

/**
 * Callable command from MEL shelf button to create collider nodes.
 */
class CreateColliderCommand : public MPxCommand {
public:
    inline static const MString commandName = MString("createCollider");
    MSelectionList activeSelectionList;
    MString colliderName;
    MDagModifier dagModifier;

	static void* creator() {
        return new CreateColliderCommand();
    }
    
    static MSyntax syntax() {
        MSyntax syntax;
        syntax.addFlag("-n", "-name", MSyntax::kString);
        return syntax;
    }

    bool isUndoable() const override {
        return true;
    }

    // Create a collider of a given type (by type name)
	MStatus doIt(const MArgList& args) override {
        MArgDatabase argData(syntax(), args);
        argData.getFlagArgument("-n", 0, colliderName);

        return redoIt();
    }

    MStatus undoIt() override {
        // Undo twice to remove both shape and transform
        dagModifier.undoIt();
        dagModifier.undoIt(); 
        // Restore what was selected before
        MGlobal::setActiveSelectionList(activeSelectionList);
        return MStatus::kSuccess;
    }

    MStatus redoIt() override {
        MGlobal::getActiveSelectionList(activeSelectionList);
        MDagPath selectedDagPath;
        if (!activeSelectionList.isEmpty()) {
            activeSelectionList.getDagPath(activeSelectionList.length() - 1, selectedDagPath);
        }

        // Create a transform under the selected object (or world if nothing selected)
        MObject parentObj = (selectedDagPath.length() > 0) ? selectedDagPath.node() : MObject::kNullObj;
        MObject colliderParentObj = Utils::createDagNode("transform", parentObj, colliderName + "Transform", &dagModifier);

        // Create the collider shape node under the transform
        MObject colliderNodeObj = Utils::createDagNode(colliderName, colliderParentObj, colliderName + "Shape#", &dagModifier);
        MSelectionList newSelection;
        MDagPath parentDagPath;
        MDagPath::getAPathTo(colliderParentObj, parentDagPath);
        newSelection.add(parentDagPath);
        MGlobal::setActiveSelectionList(newSelection);

        MGlobal::executeCommand("setToolTo moveSuperContext");
        MGlobal::executeCommand(MString("showEditor \"" + MFnDagNode(colliderNodeObj).name() + "\";"));

        return MStatus::kSuccess;
    }
};