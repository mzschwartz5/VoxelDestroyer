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
        MObject parentObj = (selectedDagPath.length() > 0) ? selectedDagPath.node() : MObject::kNullObj;
        MObject colliderParentObj = Utils::createDagNode("transform", parentObj, colliderName + "Transform");

        // Create the collider shape node under the transform
        MObject colliderNodeObj = Utils::createDagNode(colliderName, colliderParentObj, colliderName + "Shape#");

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