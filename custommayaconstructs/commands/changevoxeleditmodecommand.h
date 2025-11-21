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
#include <maya/M3dView.h>

enum class VoxelEditMode : int {
    Selection,
    FacePaint,
    VertexPaint,
    Object,
    None
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
        currentEditMode = (sEditMode == 4) ? static_cast<int>(VoxelEditMode::Object) : sEditMode; // default to Object mode if none
        shapeUUID = MFnDependencyNode(shapeObj).uuid();
        MGlobal::executeCommand("currentCtx", currentContext);

        redoIt();
        return MS::kSuccess;
    }

    MStatus undoIt() override {
        sEditMode = currentEditMode;
        sShapeUUID = shapeUUID;
        
        selectShapeByUUID(shapeUUID);

        VoxelEditMode modeEnum = static_cast<VoxelEditMode>(currentEditMode);
        MString setComponentCmd = modeToComponentMaskCommand.at(modeEnum);
        MGlobal::executeCommand(setComponentCmd);
        MGlobal::executeCommand("setToolTo " + currentContext);

        voxelEditModeChangedEvent.notify(
            EditModeChangedEventArgs{
                static_cast<VoxelEditMode>(currentEditMode),
                shapeName
            }
        );

        M3dView::active3dView().refresh(false, true);
        return MS::kSuccess;
    }

    MStatus redoIt() override {
        sEditMode = newMode;
        sShapeUUID = shapeUUID;

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
        MGlobal::executeCommand(setComponentCmd);
        MGlobal::executeCommand("setToolTo " + context);

        M3dView::active3dView().refresh(false, true);
        return MS::kSuccess;
    }

    /**
     * Callback to invoke on tool change event triggered externally (not as a result of this command).
     * It will update the edit mode of the currently selected voxel shape, or the last edited voxel shape, in that order.
     */
    static void onExternalToolChange(void* clientData) {
        MString currentTool;
        MGlobal::executeCommand("currentCtx", currentTool);
        if (currentTool.isEmpty()) return;

        MString currentToolCommandName = modeToContextCommand.at(
            static_cast<VoxelEditMode>(sEditMode)
        );

        // We're already in the correct mode
        currentToolCommandName.substitute("$", ""); // strip $
        if (currentTool.indexW(currentToolCommandName) != -1) return;

        // TODO: this will find the first match... but there could be multiple (e.g. paint tool - vertex or face mode?)
        bool foundMatch = false;
        for (const auto& pair : modeToContextCommand) {
            MString contextCommand = pair.second;
            contextCommand.substitute("$", ""); // strip $
            if (currentTool.indexW(contextCommand) == -1) continue;

            sEditMode = static_cast<int>(pair.first);
            foundMatch = true;
            break;
        }

        // The switched-to tool is not one of the voxel edit modes
        if (!foundMatch) {
            sEditMode = static_cast<int>(VoxelEditMode::None);
            return;
        }

        MString shapeNodeName = getActiveShapeNodeName();
        if (shapeNodeName.isEmpty()) return;

        VoxelEditMode modeEnum = static_cast<VoxelEditMode>(sEditMode);
        MString setComponentCmd = modeToComponentMaskCommand.at(modeEnum);
        MGlobal::executeCommand(setComponentCmd);

        voxelEditModeChangedEvent.notify(
            EditModeChangedEventArgs{
                static_cast<VoxelEditMode>(sEditMode),
                shapeNodeName
            }
        );

        M3dView::active3dView().refresh(false, true);
    }

private:
    inline static Event<EditModeChangedEventArgs> voxelEditModeChangedEvent;
    inline static const std::unordered_map<VoxelEditMode, MString> modeToContextCommand = {
        {VoxelEditMode::Selection, "selectSuperContext"},
        {VoxelEditMode::FacePaint, "$voxelPaintContext"},
        {VoxelEditMode::VertexPaint, "$voxelPaintContext"},
        {VoxelEditMode::Object, "selectSuperContext"},
        {VoxelEditMode::None, ""}
    };
    inline static const std::unordered_map<VoxelEditMode, MString> modeToComponentMaskCommand = {
        {VoxelEditMode::Selection, "SelectFacetMask"},
        {VoxelEditMode::FacePaint, "SelectFacetMask"},
        {VoxelEditMode::VertexPaint, "SelectVertexMask"},
        {VoxelEditMode::Object, "selectMode -object"},
        {VoxelEditMode::None, ""}
    };
    inline static int sEditMode = 4;
    inline static MUuid sShapeUUID;

    MString shapeName;
    MUuid shapeUUID;
    int newMode = 0;
    int currentEditMode = 0;
    MString currentContext;

    // Select by UUID to be robust against name or dag paths changing between undo/redo
    // For some reason, MGlobal::setActiveSelectionList and MGlobal::selectCommand are not sufficient
    // to make the following commands work, so we resort to MEL's `select -r`
    static void selectShapeByUUID(const MUuid& uuid, MString& shapeNodeName = MString()) {
        MSelectionList selectionList;
        selectionList.add(uuid);
        MObject depNode;
        selectionList.getDependNode(0, depNode);
        shapeNodeName = MFnDependencyNode(depNode).name();
        MGlobal::executeCommand("select -r " + shapeNodeName, true, false);
    }

    static MString getActiveShapeNodeName() {
        MString shapeNodeName;

        // First try active selection
        MObject activeObj = Utils::getMostRecentlySelectedObject();
        MDagPath shapePath;
        bool hasShape = Utils::tryGetShapePathFromObject(activeObj, shapePath);
        
        // TODO: clean up this logic a bit, remove hardcoded dependency on "VoxelShape"
        if (hasShape) {
            MObject shapeObj = shapePath.node();
            MFnDependencyNode shapeDepNode(shapeObj);
            MString shapeTypeName = shapeDepNode.typeName();

            if (shapeTypeName == "VoxelShape") {
                shapeNodeName = shapeDepNode.name();
                sShapeUUID = shapeDepNode.uuid();
                return shapeNodeName;
            }
        }
        // TODO: if something _else_ (non-voxel object) is selected, maybe we should clear sShapeUUID?
        
        // There's no active shape yet - that's fine, do nothing.
        if (!sShapeUUID.valid()) return shapeNodeName;

        selectShapeByUUID(sShapeUUID, shapeNodeName);
        return shapeNodeName;
    }
};