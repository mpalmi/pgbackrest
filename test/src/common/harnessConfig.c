/***********************************************************************************************************************************
Harness for Loading Test Configurations
***********************************************************************************************************************************/
#include "common/harnessDebug.h"
#include "common/harnessLog.h"
#include "common/harnessTest.h"

#include "config/load.h"
#include "config/parse.h"
#include "storage/helper.h"
#include "version.h"

/**********************************************************************************************************************************/
void
harnessCfgLoadRaw(unsigned int argListSize, const char *argList[])
{
    FUNCTION_HARNESS_BEGIN();
        FUNCTION_HARNESS_PARAM(UINT, argListSize);
        FUNCTION_HARNESS_PARAM(CHARPY, argList);
    FUNCTION_HARNESS_END();

    // Free objects in storage helper
    storageHelperFree();

    configParse(argListSize, argList, false);
    cfgLoadUpdateOption();

    FUNCTION_HARNESS_RESULT_VOID();
}

/**********************************************************************************************************************************/
void
harnessCfgLoad(ConfigCommand commandId, const StringList *argOriginalList)
{
    FUNCTION_HARNESS_BEGIN();
        FUNCTION_HARNESS_PARAM(ENUM, commandId);
        FUNCTION_HARNESS_PARAM(STRING_LIST, argOriginalList);
    FUNCTION_HARNESS_END();

    // Make a copy of the arg list that we can modify
    StringList *argList = strLstDup(argOriginalList);

    // Set log path if valid
    if (cfgDefOptionValid(cfgCommandDefIdFromId(commandId), cfgDefOptLogPath))
        strLstInsert(argList, 0, strNewFmt("--" CFGOPT_LOG_PATH "=%s", testDataPath()));

    // Set lock path if valid
    if (cfgDefOptionValid(cfgCommandDefIdFromId(commandId), cfgDefOptLockPath))
        strLstInsert(argList, 0, strNewFmt("--" CFGOPT_LOCK_PATH "=%s/lock", testDataPath()));

    // Insert the command so it does not interfere with parameters
    strLstInsertZ(argList, 0, cfgCommandName(commandId));

    // Insert the project exe
    strLstInsert(argList, 0, STRDEF(testProjectExe()));

    harnessCfgLoadRaw(strLstSize(argList), strLstPtr(argList));

    FUNCTION_HARNESS_RESULT_VOID();
}
