#pragma once

#include <maya/MPxLocatorNode.h>
#include <maya/MTypeId.h>
#include <maya/MString.h>
#include <maya/MUIDrawManager.h>
#include <maya/MMatrix.h>
#include <maya/MColor.h>

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

private:
};