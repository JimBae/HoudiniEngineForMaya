#include "SyncOutputGeometryPart.h"

#include <maya/MDagPath.h>
#include <maya/MGlobal.h>
#include <maya/MSelectionList.h>

#include <maya/MFnDagNode.h>
#include <maya/MFnGenericAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnNurbsCurve.h>
#include <maya/MFnTypedAttribute.h>

#include "AssetNode.h"
#include "util.h"

static MObject
createAttributeFromDataHandle(
        const MString &attributeName,
        const MDataHandle &dataHandle
        )
{
    MObject attribute;

    bool isNumeric = false;
    bool isNull = false;
    dataHandle.isGeneric(isNumeric, isNull);

    if(isNumeric)
    {
        // This is a singleton generic type. It is not MFnNumericData, and
        // needs to be handled differently. This is stored internally as a
        // double, and there's no way to determine the original type.
        MFnNumericAttribute numericAttribute;
        attribute = numericAttribute.create(
                attributeName,
                attributeName,
                MFnNumericData::kDouble
                );
    }
    else if(dataHandle.numericType() != MFnNumericData::kInvalid)
    {
        MFnNumericAttribute numericAttribute;
        attribute = numericAttribute.create(
                attributeName,
                attributeName,
                dataHandle.numericType()
                );
    }
    else if(dataHandle.type() != MFnData::kInvalid)
    {
        MFnTypedAttribute typedAttribute;
        attribute = typedAttribute.create(
                attributeName,
                attributeName,
                dataHandle.type()
                );
    }

    return attribute;
}

SyncOutputGeometryPart::SyncOutputGeometryPart(
        const MPlug &outputPlug,
        const MObject &objectTransform
        ) :
    myOutputPlug(outputPlug),
    myObjectTransform(objectTransform)
{
}

SyncOutputGeometryPart::~SyncOutputGeometryPart()
{
}

MStatus
SyncOutputGeometryPart::doIt()
{
    MStatus status;

    MString partName = myOutputPlug.child(AssetNode::outputPartName).asString();
    if(partName.length())
    {
        partName = Util::sanitizeStringForNodeName(partName);
    }
    else
    {
        partName = "emptyPart";
    }

    MFnDagNode objectTransformFn(myObjectTransform);

    // create part
    MObject partTransform = Util::findDagChild(objectTransformFn, partName);
    if(partTransform.isNull())
    {
        status = createOutputPart(myObjectTransform, partName, partTransform);
    }

    // create material
    MPlug materialPlug = myOutputPlug.child(AssetNode::outputPartMaterial);
    MPlug materialExistsPlug = materialPlug.child(AssetNode::outputPartMaterialExists);
    if(materialExistsPlug.asBool())
    {
        //TODO: check if material already exists
        status = createOutputMaterial(materialPlug, partTransform);
        CHECK_MSTATUS_AND_RETURN_IT(status);
    }
    else
    {
        MFnDagNode partTransformFn(partTransform);
        status = myDagModifier.commandToExecute("assignSG \"lambert1\" \"" + partTransformFn.fullPathName() + "\";");
        CHECK_MSTATUS_AND_RETURN_IT(status);
    }

    return redoIt();
}

MStatus
SyncOutputGeometryPart::undoIt()
{
    MStatus status;

    status = myDagModifier.undoIt();
    CHECK_MSTATUS_AND_RETURN_IT(status);

    return MStatus::kSuccess;
}

MStatus
SyncOutputGeometryPart::redoIt()
{
    MStatus status;

    status = myDagModifier.doIt();
    CHECK_MSTATUS_AND_RETURN_IT(status);

    return MStatus::kSuccess;
}

bool
SyncOutputGeometryPart::isUndoable() const
{
    return true;
}

MStatus
SyncOutputGeometryPart::createOutputPart(
        const MObject &objectTransform,
        const MString &partName,
        MObject &partTransform
        )
{
    MStatus status;

    // create partTransform
    partTransform = myDagModifier.createNode("transform", objectTransform, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    // rename partTransform
    status = myDagModifier.renameNode(partTransform, partName);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    // create mesh
    MPlug hasMeshPlug = myOutputPlug.child(AssetNode::outputPartHasMesh);
    if(hasMeshPlug.asBool())
    {
        status = createOutputMesh(
                partTransform,
                partName,
                myOutputPlug.child(AssetNode::outputPartMesh)
                );
    }

    // create particle
    MPlug particleExistsPlug = myOutputPlug.child(AssetNode::outputPartHasParticles);
    if(particleExistsPlug.asBool())
    {
        status = createOutputParticle(
                partTransform,
                partName,
                myOutputPlug.child(AssetNode::outputPartParticle)
                );
        CHECK_MSTATUS_AND_RETURN_IT(status);
    }

    // create curves
    MPlug curveIsBezier = myOutputPlug.child(AssetNode::outputPartCurvesIsBezier);
    createOutputCurves(myOutputPlug.child(AssetNode::outputPartCurves),
                       partTransform,
                       partName,
                       curveIsBezier.asBool());

    // doIt
    // Need to do it here right away because otherwise the top level
    // transform won't be shared.
    status = myDagModifier.doIt();
    CHECK_MSTATUS_AND_RETURN_IT(status);

    return MStatus::kSuccess;
}

MStatus
SyncOutputGeometryPart::createOutputMesh(
        const MObject &partTransform,
        const MString &partName,
        const MPlug &meshPlug
        )
{
    MStatus status;

    // create mesh
    MObject meshShape = myDagModifier.createNode("mesh", partTransform, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    // rename mesh
    status = myDagModifier.renameNode(meshShape, partName + "Shape");
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MFnDependencyNode partMeshFn(meshShape, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    // set mesh.displayColors
    myDagModifier.newPlugValueBool(
            partMeshFn.findPlug("displayColors"),
            true
            );

    // connect mesh attributes
    {
        MPlug srcPlug;
        MPlug dstPlug;

        srcPlug = myOutputPlug.child(AssetNode::outputPartMesh);
        dstPlug = partMeshFn.findPlug("inMesh");
        status = myDagModifier.connect(srcPlug, dstPlug);
        CHECK_MSTATUS_AND_RETURN_IT(status);
    }

    createOutputExtraAttributes(meshShape);

    return MStatus::kSuccess;
}

MStatus
SyncOutputGeometryPart::createOutputCurves(
        MPlug curvesPlug,
        const MObject &partTransform,
        const MString &partName,
        bool isBezier
        )
{
    MStatus status;

    int numCurves = curvesPlug.evaluateNumElements();
    for(int i=0; i<numCurves; i++)
    {
        MPlug curvePlug = curvesPlug[i];

        MString curveTransformName;
        MString curveShapeName;
        curveTransformName.format("curve^1s", MString() + (i + 1));
        curveShapeName.format("curveShape^1s", MString() + (i + 1));

        // create curve transform
        MObject curveTransform = myDagModifier.createNode(
                "transform",
                partTransform,
                &status
                );
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // rename curve transform
        status = myDagModifier.renameNode(curveTransform, curveTransformName);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // create curve shape
        MObject curveShape = myDagModifier.createNode(
                isBezier ? "bezierCurve" : "nurbsCurve",
                curveTransform,
                &status
                );
        CHECK_MSTATUS(status);

        // rename curve shape
        status = myDagModifier.renameNode(curveShape, curveShapeName);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        MFnDependencyNode curveShapeFn(curveShape, &status);
        MPlug dstPlug = curveShapeFn.findPlug("create");
        CHECK_MSTATUS(status);

        myDagModifier.connect(curvePlug, dstPlug);
    }

    return MStatus::kSuccess;
}

MStatus
SyncOutputGeometryPart::createOutputMaterial(
        const MPlug &materialPlug,
        const MObject &partTransform
        )
{
    MStatus status;

    MFnDagNode partTransformFn(partTransform, &status);

    // create shader
    MFnDependencyNode shaderFn;
    {
        MObject shader;
        status = Util::createNodeByModifierCommand(
                myDagModifier,
                "shadingNode -asShader phong",
                shader
                );
        CHECK_MSTATUS_AND_RETURN_IT(status);

        status = shaderFn.setObject(shader);
        CHECK_MSTATUS_AND_RETURN_IT(status);
    }

    // rename the shader
    MPlug namePlug = materialPlug.child(AssetNode::outputPartMaterialName);
    myDagModifier.renameNode(shaderFn.object(), namePlug.asString());
    CHECK_MSTATUS_AND_RETURN_IT(myDagModifier.doIt());

    // assign shader
    status = myDagModifier.commandToExecute(
            "assignSG " + shaderFn.name() + " " + partTransformFn.fullPathName()
            );
    CHECK_MSTATUS_AND_RETURN_IT(status);

    // create file node if texture exists
    MPlug texturePathPlug = materialPlug.child(AssetNode::outputPartTexturePath);
    MString texturePath = texturePathPlug.asString();
    MFnDependencyNode textureFileFn;
    if(texturePath.length())
    {
        MObject textureFile;
        status = Util::createNodeByModifierCommand(
                myDagModifier,
                "shadingNode -asTexture file",
                textureFile
                );
        CHECK_MSTATUS_AND_RETURN_IT(status);

        status = textureFileFn.setObject(textureFile);
        CHECK_MSTATUS_AND_RETURN_IT(status);
    }

    // connect shader attributse
    {
        MPlug srcPlug;
        MPlug dstPlug;

        // color
        if(textureFileFn.object().isNull())
        {
            srcPlug = materialPlug.child(AssetNode::outputPartDiffuseColor);
            dstPlug = shaderFn.findPlug("color");
            status = myDagModifier.connect(srcPlug, dstPlug);
            CHECK_MSTATUS_AND_RETURN_IT(status);
        }
        else
        {
            dstPlug = textureFileFn.findPlug("fileTextureName");
            status = myDagModifier.connect(texturePathPlug, dstPlug);
            CHECK_MSTATUS_AND_RETURN_IT(status);

            srcPlug = textureFileFn.findPlug("outColor");
            dstPlug = shaderFn.findPlug("color");
            status = myDagModifier.connect(srcPlug, dstPlug);
            CHECK_MSTATUS_AND_RETURN_IT(status);
        }

        // specularColor
        srcPlug = materialPlug.child(AssetNode::outputPartSpecularColor);
        dstPlug = shaderFn.findPlug("specularColor");
        status = myDagModifier.connect(srcPlug, dstPlug);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // ambientColor
        srcPlug = materialPlug.child(AssetNode::outputPartAmbientColor);
        dstPlug = shaderFn.findPlug("ambientColor");
        status = myDagModifier.connect(srcPlug, dstPlug);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // transparency
        srcPlug = materialPlug.child(AssetNode::outputPartAlphaColor);
        dstPlug = shaderFn.findPlug("transparency");
        status = myDagModifier.connect(srcPlug, dstPlug);
        CHECK_MSTATUS_AND_RETURN_IT(status);
    }

    // doIt
    status = myDagModifier.doIt();
    CHECK_MSTATUS_AND_RETURN_IT(status);

    return MStatus::kSuccess;
}

MStatus
SyncOutputGeometryPart::createOutputParticle(
        const MObject &partTransform,
        const MString &partName,
        const MPlug &particlePlug
        )
{
    MStatus status;

    // create nParticle
    MObject particleShapeObj = myDagModifier.createNode(
            "nParticle",
            partTransform,
            &status
            );
    CHECK_MSTATUS_AND_RETURN_IT(status);

    // rename particle
    status = myDagModifier.renameNode(particleShapeObj, partName + "Shape");
    CHECK_MSTATUS_AND_RETURN_IT(status);
    status = myDagModifier.doIt();
    CHECK_MSTATUS_AND_RETURN_IT(status);

    myDagModifier.commandToExecute(
            "setupNParticleConnections " +
            MFnDagNode(partTransform).fullPathName()
            );

    MPlug srcPlug;
    MPlug dstPlug;

    MFnDependencyNode particleShapeFn(particleShapeObj);

    // connect nParticleShape attributes
    status = myDagModifier.connect(srcPlug, dstPlug);
    srcPlug = particlePlug.child(AssetNode::outputPartParticleCurrentTime);
    dstPlug = particleShapeFn.findPlug("currentTime");
    status = myDagModifier.connect(srcPlug, dstPlug);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    srcPlug = particlePlug.child(AssetNode::outputPartParticlePositions);
    dstPlug = particleShapeFn.findPlug("positions");
    status = myDagModifier.connect(srcPlug, dstPlug);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    srcPlug = particlePlug.child(AssetNode::outputPartParticleArrayData);
    dstPlug = particleShapeFn.findPlug("cacheArrayData");
    status = myDagModifier.connect(srcPlug, dstPlug);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    // set particleRenderType to points
    status = myDagModifier.newPlugValueInt(
            particleShapeFn.findPlug("particleRenderType"),
            3);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    // set playFromCache to true
    status = myDagModifier.newPlugValueBool(
            particleShapeFn.findPlug("playFromCache"),
            true);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    createOutputExtraAttributes(particleShapeObj);

    return MStatus::kSuccess;
}

MStatus
SyncOutputGeometryPart::createOutputExtraAttributes(
        const MObject &dstNode
        )
{
    MStatus status;

    MFnDependencyNode dstNodeFn(dstNode);

    MPlug extraAttributesPlug = myOutputPlug.child(
            AssetNode::outputPartExtraAttributes
            );

    bool isParticle = dstNode.hasFn(MFn::kParticle);

    int numExtraAttributes = extraAttributesPlug.numElements();
    for(int i = 0; i < numExtraAttributes; i++)
    {
        MPlug extraAttributePlug = extraAttributesPlug[i];
        MPlug extraAttributeNamePlug = extraAttributePlug.child(
                AssetNode::outputPartExtraAttributeName
                );
        MPlug extraAttributeOwnerPlug = extraAttributePlug.child(
                AssetNode::outputPartExtraAttributeOwner
                );
        MPlug extraAttributeDataTypePlug = extraAttributePlug.child(
                AssetNode::outputPartExtraAttributeDataType
                );
        MPlug extraAttributeTuplePlug = extraAttributePlug.child(
                AssetNode::outputPartExtraAttributeTuple
                );
        MPlug extraAttributeDataPlug = extraAttributePlug.child(
                AssetNode::outputPartExtraAttributeData
                );

        MString dstAttributeName = extraAttributeNamePlug.asString();

        MString owner = extraAttributeOwnerPlug.asString();
        MString dataType = extraAttributeDataTypePlug.asString();
        int tuple = extraAttributeTuplePlug.asInt();

        bool isPerParticleAttribute = false;
        if(isParticle)
        {
            // Not every attribute type can be supported on a particle node.
            bool canSupportAttribute = false;

            if(owner == "detail")
            {
                // Make sure the data is not array type.
                if((dataType == "float"
                            || dataType == "int")
                        && tuple <= 3)
                {
                    canSupportAttribute = true;
                }
                else if(dataType == "string"
                        && tuple == 1)
                {
                    canSupportAttribute = true;
                }
            }
            else if(owner == "primitive")
            {
                // Shouldn't be possible.
            }
            else if(owner == "point")
            {
                // Particles don't support int and string attributes. Also,
                // make sure the one particle maps to one element of the array.
                if((dataType == "float" && tuple == 1)
                        || (dataType == "float" && tuple == 3))
                {
                    isPerParticleAttribute = true;
                    canSupportAttribute = true;
                }
            }
            else if(owner == "vertex")
            {
                // Shouldn't be possible.
            }

            if(!canSupportAttribute)
            {
                DISPLAY_WARNING(
                        "The particle node:\n"
                        "    ^1s\n"
                        "cannot support the attribute:\n"
                        "    owner: ^2s, name: ^3s, type: ^4s, tuple: ^5s\n"
                        "from the plug:"
                        "    ^6s\n",
                        dstNodeFn.name(),
                        owner, dstAttributeName, dataType, MString() + tuple,
                        extraAttributeDataPlug.name()
                        );
                continue;
            }
        }

        // Some special cases to remap certain attributes.
        if(owner != "detail" && dstAttributeName == "v")
        {
            // "v" means velocity in Houdini. However, in Maya, "v" happens to
            // be the short name of the "visibility" attribute. Instead of
            // connecting velocity to visibility, which would be very confusing
            // for the user, we rename "v" to "velocity".
            dstAttributeName = "velocity";
        }

        // Use existing attribute if it exists.
        MObject dstAttribute = dstNodeFn.attribute(dstAttributeName);

        // If it doesn't exist, create it.
        if(dstAttribute.isNull())
        {
            MDataHandle dataHandle = extraAttributeDataPlug.asMDataHandle();

            dstAttribute = createAttributeFromDataHandle(
                    dstAttributeName,
                    dataHandle
                    );

            if(!dstAttribute.isNull())
            {
                CHECK_MSTATUS(myDagModifier.addAttribute(
                            dstNode,
                            dstAttribute
                            ));
                CHECK_MSTATUS(myDagModifier.doIt());
            }
        }

        // If there's still no attribute, skip it.
        if(dstAttribute.isNull())
        {
            DISPLAY_WARNING(
                    "Cannot create attribute on the node:\n"
                    "    ^1s\n"
                    "for the attribute:\n"
                    "    owner: ^2s, name: ^3s, type: ^4s, tuple: ^5s\n"
                    "from the plug:\n"
                    "    ^6s\n",
                    dstNodeFn.name(),
                    owner, dstAttributeName, dataType, MString() + tuple,
                    extraAttributeDataPlug.name()
                    );

            continue;
        }

        // Per-particle attributes does not need to be, and cannot be,
        // connected.
        if(isParticle && isPerParticleAttribute)
        {
            continue;
        }

        MPlug dstPlug(dstNode, dstAttribute);
        CHECK_MSTATUS(myDagModifier.connect(
                    extraAttributeDataPlug,
                    dstPlug
                    ));
        status = (myDagModifier.doIt());
        if(!status)
        {
            DISPLAY_WARNING(
                    "Failed to connect:\n"
                    "    ^1s\n"
                    "    to:\n"
                    "    ^2s\n",
                    extraAttributeDataPlug.name(),
                    dstPlug.name()
                    );
            CHECK_MSTATUS(status);
        }
    }

    return status;
}
