#pragma once
#include <maya/MGlobal.h>
#include <maya/MPxCommand.h>
#include <maya/MSelectionList.h>
#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MSyntax.h>
#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include "../../event.h"
#include "../../utils.h"
#include <maya/MString.h>
#include <maya/MUuid.h>

enum class VoxelEditMode : int {
    Selection,
    FacePaint,
    VertexPaint,
    Object
};

struct EditModeChangedEventArgs {
    VoxelEditMode newMode;
    MString shapeName;
};

/**
 * Command to change the voxel edit mode of a voxel shape. 
 * E.g. switching between selection mode, paint mode, object mode, etc.
 */
class ChangeVoxelEditModeCommand : public MPxCommand {
public:
    inline static const MString commandName = MString("changeVoxelEditMode");
    inline static const std::unordered_map<VoxelEditMode, MString> modeToContextCommand = {
        {VoxelEditMode::Selection, "selectSuperContext"},
        {VoxelEditMode::FacePaint, "`voxelPaintContextCommand`"},
        {VoxelEditMode::VertexPaint, "`voxelPaintContextCommand`"},
        {VoxelEditMode::Object, "selectSuperContext"}
    };
    inline static const std::unordered_map<VoxelEditMode, MString> modeToComponentMaskCommand = {
        {VoxelEditMode::Selection, "SelectFacetMask"},
        {VoxelEditMode::FacePaint, "SelectFacetMask"},
        {VoxelEditMode::VertexPaint, "SelectVertexMask"},
        {VoxelEditMode::Object, "selectMode -object"}
    };
    inline static int sEditMode = 3; // Object mode

    MString shapeName;
    MUuid shapeUUID;
    int newMode = 0;
    int currentEditMode = 0;
    MString currentContext;

    static EventBase::Unsubscribe subscribe(
        const Event<const EditModeChangedEventArgs&>::Listener& listener) 
    {
        return voxelEditModeChangedEvent.subscribe(listener);
    }

    static void* creator() {
        return new ChangeVoxelEditModeCommand();
    }

    static MSyntax syntax() {
        MSyntax syntax;
        syntax.addFlag("-n", "-name", MSyntax::kString);
        syntax.addFlag("-m", "-mode", MSyntax::kLong);
        return syntax;
    }

    bool isUndoable() const override {
        return true;
    }

    MStatus doIt(const MArgList& args) override {
        MArgDatabase argData(syntax(), args);
        argData.getFlagArgument("-n", 0, shapeName);
        argData.getFlagArgument("-m", 0, newMode);

        // Cache current state for undo
        MObject shapeObj = Utils::getNodeFromName(shapeName);
        currentEditMode = sEditMode;
        sEditMode = newMode;
        shapeUUID = MFnDependencyNode(shapeObj).uuid();
        MGlobal::executeCommand("currentCtx", currentContext);

        redoIt();
        return MS::kSuccess;
    }

    MStatus undoIt() override {
        selectShapeByUUID(shapeUUID);

        VoxelEditMode modeEnum = static_cast<VoxelEditMode>(currentEditMode);
        MString setComponentCmd = modeToComponentMaskCommand.at(modeEnum);
        MGlobal::executeCommand(setComponentCmd, false, false);
        MGlobal::executeCommand("setToolTo " + currentContext, false, false);

        voxelEditModeChangedEvent.notify(
            EditModeChangedEventArgs{
                static_cast<VoxelEditMode>(currentEditMode),
                shapeName
            }
        );

        MGlobal::executeCommandOnIdle("refresh");
        return MS::kSuccess;
    }

    MStatus redoIt() override {
        voxelEditModeChangedEvent.notify(
            EditModeChangedEventArgs{
                static_cast<VoxelEditMode>(newMode),
                shapeName
            }
        );

        VoxelEditMode modeEnum = static_cast<VoxelEditMode>(newMode);
        MString setComponentCmd = modeToComponentMaskCommand.at(modeEnum);
        MString context = modeToContextCommand.at(modeEnum);
        
        selectShapeByUUID(shapeUUID);
        MGlobal::executeCommand(setComponentCmd, false, false);
        MGlobal::executeCommand("setToolTo " + context, false, false);
        MGlobal::executeCommandOnIdle("refresh");
        return MS::kSuccess;
    }

    // Select by UUID to be robust against name or dag paths changing between undo/redo
    // For some reason, MGlobal::setActiveSelectionList and MGlobal::selectCommand are not sufficient
    // to make the following commands work, so we resort to MEL's `select -r`
    void selectShapeByUUID(const MUuid& uuid) {
        MSelectionList selectionList;
        selectionList.add(shapeUUID);
        MObject depNode;
        selectionList.getDependNode(0, depNode);
        MString shapeNodeName = MFnDependencyNode(depNode).name();
        MGlobal::executeCommand("select -r " + shapeNodeName, true, false);
    }

private:
    inline static Event<EditModeChangedEventArgs> voxelEditModeChangedEvent;

};