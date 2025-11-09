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

enum class VoxelEditMode {
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

    MStatus doIt(const MArgList& args) override {
        MString shapeName;
        int mode = 0;
        MArgDatabase argData(syntax(), args);
        argData.getFlagArgument("-n", 0, shapeName);
        argData.getFlagArgument("-m", 0, mode);

        voxelEditModeChangedEvent.notify(
            EditModeChangedEventArgs{
                static_cast<VoxelEditMode>(mode),
                shapeName
            }
        );

        MGlobal::executeCommandOnIdle("refresh");
        return MS::kSuccess;
    }

private:
    inline static Event<EditModeChangedEventArgs> voxelEditModeChangedEvent;

};