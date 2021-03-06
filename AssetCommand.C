#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MSelectionList.h>
#include <maya/MStatus.h>

#include "Asset.h"
#include "AssetNode.h"
#include "AssetCommand.h"
#include "SubCommand.h"
#include "AssetSubCommandLoadAsset.h"
#include "AssetSubCommandSync.h"
#include "util.h"

#define kListAssetsFlag "-ls"
#define kListAssetsFlagLong "-listAssets"
#define kLoadAssetFlag "-la"
#define kLoadAssetFlagLong "-loadAsset"
#define kSyncFlag "-syn"
#define kSyncFlagLong "-sync"
#define kResetSimulationFlag "-rs"
#define kResetSimulationFlagLong "-resetSimulation"
#define kCookMessagesFlag "-cm"
#define kCookMessagesFlagLong "-cookMessages"
#define kReloadAssetFlag "-rl"
#define kReloadAssetFlagLong "-reloadAsset"
#define kSyncAttributesFlag "-sa"
#define kSyncAttributesFlagLong "-syncAttributes"
#define kSyncOutputsFlag "-so"
#define kSyncOutputsFlagLong "-syncOutputs"
#define kSyncHiddenFlag "-shi"
#define kSyncHiddenFlagLong "-syncHidden"
#define kSyncTemplatedGeosFlag "-stm"
#define kSyncTemplatedGeosFlagLong "-syncTemplatedGeos"

class AssetSubCommandResetSimulation : public SubCommandAsset
{
    public:
        AssetSubCommandResetSimulation(const MObject &assetNodeObj) :
            SubCommandAsset(assetNodeObj)
        {
        }

        virtual MStatus doIt()
        {
            GET_COMMAND_ASSET_OR_RETURN_FAIL();

            asset->resetSimulation();

            return MStatus::kSuccess;
        }
};

class AssetSubCommandCookMessages : public SubCommandAsset
{
    public:
        AssetSubCommandCookMessages(const MObject &assetNodeObj) :
            SubCommandAsset(assetNodeObj)
        {
        }

        virtual MStatus doIt()
        {
            GET_COMMAND_ASSET_OR_RETURN_FAIL();

            MPxCommand::setResult(asset->getCookMessages());

            return MStatus::kSuccess;
        }
};

class AssetSubCommandListAssets : public SubCommand
{
    public:
        AssetSubCommandListAssets(const MString &otlFilePath) :
            myOTLFilePath(otlFilePath)
        {
        }

        virtual MStatus doIt()
        {
            HAPI_Result hapiResult;

            int libraryId = -1;
            hapiResult = HAPI_LoadAssetLibraryFromFile(myOTLFilePath.asChar(), true, &libraryId);
            if(HAPI_FAIL(hapiResult))
            {
                DISPLAY_ERROR("Could not load OTL file: ^1s", myOTLFilePath);
                DISPLAY_ERROR_HAPI_STATUS_CALL();

                return MStatus::kFailure;
            }

            int assetCount = 0;
            hapiResult = HAPI_GetAvailableAssetCount(libraryId, &assetCount);
            CHECK_HAPI_AND_RETURN(hapiResult, MStatus::kFailure);

            std::vector<HAPI_StringHandle> assetNamesSH(assetCount);
            hapiResult = HAPI_GetAvailableAssets(libraryId,
                    &assetNamesSH.front(),
                    assetNamesSH.size());
            CHECK_HAPI_AND_RETURN(hapiResult, MStatus::kFailure);

            for(unsigned int i = 0; i < assetNamesSH.size(); i++)
            {
                MPxCommand::appendToResult(Util::getString(assetNamesSH[i]));
            }

            return MStatus::kSuccess;
        }

    protected:
        MString myOTLFilePath;
};

void* AssetCommand::creator()
{
    return new AssetCommand();
}

MSyntax
AssetCommand::newSyntax()
{
    MSyntax syntax;

    // -listAssets list assets in an OTL file
    CHECK_MSTATUS(syntax.addFlag(kListAssetsFlag, kListAssetsFlagLong,
                MSyntax::kString));

    // -loadAsset load an otl file
    // expected arguments: otl_file_name - the name of the otl file to load
    CHECK_MSTATUS(syntax.addFlag(kLoadAssetFlag, kLoadAssetFlagLong,
                MSyntax::kString,
                MSyntax::kString));

    // -sync synchronize the Maya nodes with the asset's state
    // expected arguments:
    //        asset node
    CHECK_MSTATUS(syntax.addFlag(kSyncFlag, kSyncFlagLong,
                MSyntax::kSelectionItem));

    // -resetSimulation resets the simulation state for an asset.  This will clear
    // the DOPs cache for the asset.
    CHECK_MSTATUS(syntax.addFlag(kResetSimulationFlag,
                                 kResetSimulationFlagLong,
                                 MSyntax::kSelectionItem));

    // -cookMessages get the cook messages for an asset
    CHECK_MSTATUS(syntax.addFlag(kCookMessagesFlag,
                                 kCookMessagesFlagLong,
                                 MSyntax::kSelectionItem));

    // -reloadAsset will unload and immediate reload the asset.  If an otl file
    // has changed due to an edit in Houdini, this should pick up the change
        // Note that this won't refresh the AE, you need to that separately after
        // running this, with refreshEditorTemplates
    CHECK_MSTATUS(syntax.addFlag(kReloadAssetFlag,
                                 kReloadAssetFlagLong,
                                 MSyntax::kString));

    CHECK_MSTATUS(syntax.addFlag(kSyncAttributesFlag, kSyncAttributesFlagLong));
    CHECK_MSTATUS(syntax.addFlag(kSyncOutputsFlag, kSyncOutputsFlagLong));

    // -syncHidden will cause hidden objects to be sync'ed
    CHECK_MSTATUS(syntax.addFlag(kSyncHiddenFlag,
                                 kSyncHiddenFlagLong,
                                 MSyntax::kNoArg));

    // -syncTemplatedGeos will cause templated geos to be sync'ed
    CHECK_MSTATUS(syntax.addFlag(kSyncTemplatedGeosFlag,
                                 kSyncTemplatedGeosFlagLong,
                                 MSyntax::kNoArg));

    return syntax;
}

AssetCommand::AssetCommand() :
    mySubCommand(NULL)
{
}

AssetCommand::~AssetCommand()
{
    delete mySubCommand;
}

MStatus
AssetCommand::parseArgs(const MArgList &args)
{
    MStatus status;
    MArgDatabase argData(syntax(), args, &status);
    if(!status)
    {
        return status;
    }

    if(!(argData.isFlagSet(kListAssetsFlag)
                ^ argData.isFlagSet(kLoadAssetFlag)
                ^ argData.isFlagSet(kSyncFlag)
                ^ argData.isFlagSet(kResetSimulationFlag)
                ^ argData.isFlagSet(kCookMessagesFlag)
                ^ argData.isFlagSet(kReloadAssetFlag)))
    {
        displayError("Exactly one of these flags must be specified:\n"
                kLoadAssetFlagLong "\n"
                kSyncFlagLong "\n"
                kResetSimulationFlagLong "\n"
                kCookMessagesFlagLong "\n"
                kReloadAssetFlagLong "\n");
        return MStatus::kInvalidParameter;
    }

    if(argData.isFlagSet(kListAssetsFlag))
    {
        MString otlFilePath;
        {
            status = argData.getFlagArgument(kListAssetsFlag, 0, otlFilePath);
            if(!status)
            {
                displayError("Invalid argument for \"" kListAssetsFlagLong "\".");
                return status;
            }
        }

        mySubCommand = new AssetSubCommandListAssets(
                otlFilePath
                );
    }

    if(argData.isFlagSet(kLoadAssetFlag))
    {
        MString otlFilePath;
        {
            status = argData.getFlagArgument(kLoadAssetFlag, 0, otlFilePath);
            if(!status)
            {
                displayError("Invalid argument for \"" kLoadAssetFlagLong "\".");
                return status;
            }
        }

        MString assetName;
        {
            status = argData.getFlagArgument(kLoadAssetFlag, 1, assetName);
            if(!status)
            {
                displayError("Invalid argument for \"" kLoadAssetFlagLong "\".");
                return status;
            }
        }

        mySubCommand = new AssetSubCommandLoadAsset(
                otlFilePath,
                assetName
                );
    }

    if(argData.isFlagSet(kReloadAssetFlag))
    {
        MString assetPath;
        {
            status = argData.getFlagArgument(kReloadAssetFlag, 0, assetPath);
            if(!status)
            {
                displayError("Invalid argument for \"" kReloadAssetFlagLong "\".");
                return status;
            }
        }

        MSelectionList selList;
        MObject assetNodeObj;

        selList.add(assetPath);
        selList.getDependNode(0, assetNodeObj);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        MFnDependencyNode assetNodeFn(assetNodeObj);
        AssetNode* assetNode = dynamic_cast<AssetNode*>(assetNodeFn.userNode());
        assetNode->rebuildAsset();

        mySubCommand = new AssetSubCommandSync(assetNodeObj);
    }

    if(argData.isFlagSet(kSyncFlag))
    {
        MObject assetNodeObj;
        {
            MSelectionList selection;

            status = argData.getFlagArgument(kSyncFlag, 0, selection);
            if(!status)
            {
                displayError("Invalid first argument for \"" kSyncFlagLong "\".");
                return status;
            }

            selection.getDependNode(0, assetNodeObj);
            CHECK_MSTATUS_AND_RETURN_IT(status);
        }

        AssetSubCommandSync* subCommand = new AssetSubCommandSync(
                assetNodeObj
                );

        if(argData.isFlagSet(kSyncAttributesFlag))
        {
            subCommand->setSyncAttributes();
        }

        if(argData.isFlagSet(kSyncOutputsFlag))
        {
            subCommand->setSyncOutputs();
        }

        if(argData.isFlagSet(kSyncHiddenFlag))
        {
            subCommand->setSyncOutputHidden();
        }

        if(argData.isFlagSet(kSyncTemplatedGeosFlag))
        {
            subCommand->setSyncOutputTemplatedGeos();
        }

        mySubCommand = subCommand;
    }

    if(argData.isFlagSet(kResetSimulationFlag))
    {
        MObject assetNodeObj;
        {
            MSelectionList selection;

            status = argData.getFlagArgument(kResetSimulationFlag, 0, selection);
            if(!status)
            {
                displayError("Invalid first argument for \"" kResetSimulationFlagLong "\".");
                return status;
            }

            selection.getDependNode(0, assetNodeObj);
            CHECK_MSTATUS_AND_RETURN_IT(status);
        }

        mySubCommand = new AssetSubCommandResetSimulation(assetNodeObj);
    }

    if(argData.isFlagSet(kCookMessagesFlag))
    {
        MObject assetNodeObj;
        {
            MSelectionList selection;

            status = argData.getFlagArgument(kCookMessagesFlag, 0, selection);
            if(!status)
            {
                displayError("Invalid first argument for \"" kCookMessagesFlagLong "\".");
                return status;
            }

            selection.getDependNode(0, assetNodeObj);
            CHECK_MSTATUS_AND_RETURN_IT(status);
        }

        mySubCommand = new AssetSubCommandCookMessages(assetNodeObj);
    }

    return MStatus::kSuccess;
}

MStatus AssetCommand::doIt(const MArgList& args)
{
    MStatus status;

    status = parseArgs(args);
    if(!status)
    {
        return status;
    }

    return mySubCommand->doIt();
}

MStatus AssetCommand::redoIt()
{
    return mySubCommand->redoIt();
}

MStatus AssetCommand::undoIt()
{
    return mySubCommand->undoIt();
}

bool AssetCommand::isUndoable() const
{
    return mySubCommand->isUndoable();
}
