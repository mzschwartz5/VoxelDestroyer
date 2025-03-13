#pragma once
#include <maya/MArgList.h>
#include <maya/MObject.h>
#include <maya/MGlobal.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

// custom Maya command
class plugin : public MPxCommand
{
public:
	plugin() {};
	virtual MStatus doIt(const MArgList& args);
	static void* creator();
	static MSyntax syntax();
};