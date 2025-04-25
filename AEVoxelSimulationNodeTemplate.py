import maya.cmds as cmds

def AEVoxelSimulationNodeTemplate(nodeName):
    cmds.editorTemplate(beginScrollLayout=True)

    # Suppress built-in MPxNode attributes
    cmds.editorTemplate(suppressAll=True)

    # Add your attributes
    cmds.editorTemplate(addControl="relaxation", label="Relaxation")
    cmds.editorTemplate(addControl="edgeUniformity", label="Edge Uniformity")

    cmds.editorTemplate(addExtraControls=False)
    cmds.editorTemplate(endScrollLayout=True)